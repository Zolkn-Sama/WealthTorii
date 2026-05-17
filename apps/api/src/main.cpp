#include "auth.hpp"
#include "json_support.hpp"
#include "openapi.hpp"

#include "budget_config.hpp"
#include "rules_config.hpp"

#include <wealthtorii/analytics/analytics.hpp>
#include <wealthtorii/budget/allocation.hpp>
#include <wealthtorii/budget/budget.hpp>
#include <wealthtorii/budget/category.hpp>
#include <wealthtorii/import/bp_csv.hpp>
#include <wealthtorii/ledger/account.hpp>
#include <wealthtorii/ledger/journal.hpp>
#include <wealthtorii/money/money.hpp>
#include <wealthtorii/storage/connection.hpp>
#include <wealthtorii/storage/migrations.hpp>
#include <wealthtorii/storage/repository.hpp>

#include <drogon/drogon.h>
#include <drogon/MultiPart.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using namespace wealthtorii;
    using drogon::HttpRequestPtr;
    using drogon::HttpResponse;
    using drogon::HttpResponsePtr;
    using Callback = std::function<void(const HttpResponsePtr&)>;

    void send_json(const Callback& cb, const Json::Value& body,
                   drogon::HttpStatusCode code = drogon::k200OK) {
        auto resp = HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(code);
        cb(resp);
    }

    void send_error(const Callback& cb, std::string_view message,
                    drogon::HttpStatusCode code = drogon::k400BadRequest) {
        send_json(cb, api::error_json(message), code);
    }

    // Forward declarations — auth/tenancy/config helpers are defined alongside
    // the CRUD section but used earlier by the DB-backed handlers. Budget and
    // rules are per-user in Postgres (no longer the global ~/.wealthtorii files).
    std::string uid_of(const HttpRequestPtr& req);
    bool require_account_owner(const HttpRequestPtr& req, const Callback& cb,
                               const std::string& account);
    std::optional<cli::BudgetConfig> db_load_budget(const HttpRequestPtr& req,
                                                     const Callback& cb);
    bool db_save_budget(const HttpRequestPtr& req, const Callback& cb,
                        const cli::BudgetConfig& cfg);
    std::optional<cli::RulesConfig> db_load_rules(const HttpRequestPtr& req,
                                                  const Callback& cb);
    bool db_save_rules(const HttpRequestPtr& req, const Callback& cb,
                       const cli::RulesConfig& cfg);
    std::optional<import_::Categorizer> db_make_categorizer(
        const HttpRequestPtr& req, const Callback& cb);

    ledger::Journal journal_from_csv(std::istream& in, std::string account,
                                     money::Currency currency,
                                     const import_::Categorizer& overrides) {
        import_::ImportOptions opts;
        opts.account_id = std::move(account);
        opts.currency = currency;
        opts.overrides = &overrides;
        const auto imp = import_::import_bp_csv(in, opts);
        ledger::Journal j;
        for (const auto& tx : imp.transactions) {
            j.add(tx);
        }
        return j;
    }

    // Loads a journal from Postgres. On failure returns nullopt and fills `err`.
    std::optional<ledger::Journal> journal_from_db(std::string_view account,
                                                   std::string& err) {
        const auto url = storage::Connection::database_url_from_env();
        if (!url.has_value()) {
            err = "$DATABASE_URL is not set on the server";
            return std::nullopt;
        }
        try {
            storage::Connection conn{*url};
            const storage::TransactionRepository repo{conn};
            return repo.load_journal(account);
        } catch (const std::exception& e) {
            err = e.what();
            return std::nullopt;
        }
    }

    std::string format_pct_pair(std::int64_t num, std::int64_t den) {
        if (den == 0) {
            return "-";
        }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1)
            << (100.0 * static_cast<double>(num) / static_cast<double>(den)) << '%';
        return oss.str();
    }

    bool is_transfer(const budget::CategoryRegistry& reg,
                     const std::optional<std::string>& cat) {
        if (!cat.has_value()) {
            return false;
        }
        const auto* c = reg.find(*cat);
        return c != nullptr && c->bucket() == budget::Bucket::TRANSFERS;
    }

    // ---- entity (de)serialisation -------------------------------------------

    std::string format_iso_date(std::chrono::year_month_day d) {
        char buf[11];
        std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u", static_cast<int>(d.year()),
                      static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
        return std::string(buf);
    }

    std::optional<std::chrono::year_month_day> parse_iso_date(std::string_view s) {
        if (s.size() != 10 || s[4] != '-' || s[7] != '-') {
            return std::nullopt;
        }
        int y = 0;
        int mo = 0;
        int d = 0;
        if (std::from_chars(s.data(), s.data() + 4, y).ec != std::errc{} ||
            std::from_chars(s.data() + 5, s.data() + 7, mo).ec != std::errc{} ||
            std::from_chars(s.data() + 8, s.data() + 10, d).ec != std::errc{}) {
            return std::nullopt;
        }
        const std::chrono::year_month_day ymd{std::chrono::year{y},
                                              std::chrono::month{static_cast<unsigned>(mo)},
                                              std::chrono::day{static_cast<unsigned>(d)}};
        if (!ymd.ok()) {
            return std::nullopt;
        }
        return ymd;
    }

    Json::Value account_json(const ledger::Account& a) {
        Json::Value v(Json::objectValue);
        v["id"] = a.id();
        v["name"] = a.name();
        v["currency"] = std::string(money::to_string(a.currency()));
        v["type"] = std::string(ledger::to_string(a.type()));
        v["is_active"] = a.is_active();
        return v;
    }

    money::Currency currency_or_eur(const std::string& code) {
        return money::currency_from_string(code).value_or(money::Currency::EUR);
    }

    // Account view enriched with opening balance and derived current balance.
    Json::Value balance_json(const storage::AccountBalance& b) {
        const auto cur = currency_or_eur(b.currency);
        Json::Value v(Json::objectValue);
        v["id"] = b.id;
        v["name"] = b.name;
        v["currency"] = b.currency;
        v["opening_balance"] = api::money_json(money::Money(b.opening_balance, cur));
        v["balance"] = api::money_json(money::Money(b.current_balance, cur));
        return v;
    }

    Json::Value transaction_json(const ledger::Transaction& t) {
        Json::Value v(Json::objectValue);
        v["id"] = t.id();
        v["account_id"] = t.account_id();
        v["date"] = format_iso_date(t.date());
        v["amount"] = api::money_json(t.amount());
        v["description"] = t.description();
        v["category_id"] = t.category_id().has_value() ? Json::Value(*t.category_id())
                                                       : Json::Value(Json::nullValue);
        v["bp_category"] = t.bp_category().has_value() ? Json::Value(*t.bp_category())
                                                       : Json::Value(Json::nullValue);
        v["bp_subcategory"] = t.bp_subcategory().has_value()
                                  ? Json::Value(*t.bp_subcategory())
                                  : Json::Value(Json::nullValue);
        v["type_operation"] = t.type_operation();
        v["is_reconciled"] = t.is_reconciled();
        return v;
    }

    // Builds an Account from a JSON body. Returns an error string on validation failure.
    std::optional<ledger::Account> account_from_json(const Json::Value& b, std::string id,
                                                     std::string& err) {
        if (id.empty() || !b.isMember("name")) {
            err = "'id' and 'name' are required";
            return std::nullopt;
        }
        auto currency = money::Currency::EUR;
        if (b.isMember("currency")) {
            const auto c = money::currency_from_string(b["currency"].asString());
            if (!c.has_value()) {
                err = "unknown currency";
                return std::nullopt;
            }
            currency = *c;
        }
        auto type = ledger::AccountType::CASH;
        if (b.isMember("type")) {
            const auto t = ledger::account_type_from_string(b["type"].asString());
            if (!t.has_value()) {
                err = "unknown account type (CASH|BROKERAGE|CRYPTO|SAVINGS|EXTERNAL)";
                return std::nullopt;
            }
            type = *t;
        }
        const bool active = !b.isMember("is_active") || b["is_active"].asBool();
        return ledger::Account(std::move(id), b["name"].asString(), currency, type, active);
    }

    // Parses an optional "opening_balance": a signed amount string ("1 200,50")
    // or integer minor units. Defaults to 0. nullopt + err on a bad string.
    std::optional<std::int64_t> opening_balance_from_json(const Json::Value& b,
                                                          money::Currency currency,
                                                          std::string& err) {
        if (!b.isMember("opening_balance") || b["opening_balance"].isNull()) {
            return 0;
        }
        const auto& ob = b["opening_balance"];
        if (ob.isString()) {
            const auto m = money::Money::from_string(ob.asString(), currency);
            if (!m.has_value()) {
                err = "'opening_balance' is not a valid amount";
                return std::nullopt;
            }
            return m->minor_units();
        }
        return ob.asInt64();
    }

    // Builds a Transaction from a JSON body. Accepts either {amount:"-12,34 EUR"} or
    // {minor_units:-1234, currency:"EUR"}. Returns an error string on validation failure.
    std::optional<ledger::Transaction> transaction_from_json(const Json::Value& b,
                                                             std::string id,
                                                             std::string& err) {
        if (id.empty() || !b.isMember("account_id") || !b.isMember("date")) {
            err = "'id', 'account_id' and 'date' are required";
            return std::nullopt;
        }
        const auto date = parse_iso_date(b["date"].asString());
        if (!date.has_value()) {
            err = "'date' must be YYYY-MM-DD";
            return std::nullopt;
        }
        std::optional<money::Money> amount;
        if (b.isMember("amount")) {
            auto cur = money::Currency::EUR;
            if (b.isMember("currency")) {
                const auto c = money::currency_from_string(b["currency"].asString());
                if (!c.has_value()) {
                    err = "unknown currency";
                    return std::nullopt;
                }
                cur = *c;
            }
            amount = money::Money::from_string(b["amount"].asString(), cur);
        } else if (b.isMember("minor_units")) {
            auto cur = money::Currency::EUR;
            if (b.isMember("currency")) {
                const auto c = money::currency_from_string(b["currency"].asString());
                if (!c.has_value()) {
                    err = "unknown currency";
                    return std::nullopt;
                }
                cur = *c;
            }
            amount = money::Money(b["minor_units"].asInt64(), cur);
        }
        if (!amount.has_value()) {
            err = "provide 'amount' (e.g. \"-12,34 EUR\") or 'minor_units' + 'currency'";
            return std::nullopt;
        }
        auto opt_str = [&](const char* k) -> std::optional<std::string> {
            if (b.isMember(k) && !b[k].isNull()) return b[k].asString();
            return std::nullopt;
        };
        const std::string desc = b.isMember("description") ? b["description"].asString() : "";
        const std::string type_op =
            b.isMember("type_operation") ? b["type_operation"].asString() : "";
        const bool reconciled = b.isMember("is_reconciled") && b["is_reconciled"].asBool();
        try {
            return ledger::Transaction(std::move(id), *date, b["account_id"].asString(),
                                       *amount, desc, opt_str("category_id"),
                                       opt_str("bp_category"), opt_str("bp_subcategory"),
                                       type_op, reconciled);
        } catch (const std::exception& e) {
            err = e.what();
            return std::nullopt;
        }
    }


    std::chrono::year_month_day today_ymd() {
        const auto days =
            std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        return std::chrono::year_month_day{std::chrono::sys_days{days}};
    }

    // Whole months from `from` to `to` (>= 0). Day-of-month rounds down.
    int months_until(std::chrono::year_month_day from,
                     std::chrono::year_month_day to) {
        int m = (static_cast<int>(to.year()) - static_cast<int>(from.year())) * 12 +
                (static_cast<int>(static_cast<unsigned>(to.month())) -
                 static_cast<int>(static_cast<unsigned>(from.month())));
        if (static_cast<unsigned>(to.day()) < static_cast<unsigned>(from.day())) {
            --m;
        }
        return m > 0 ? m : 0;
    }

    Json::Value goal_json(const storage::GoalProgress& gp) {
        const auto cur = currency_or_eur(gp.goal.currency);
        const auto target = gp.goal.target_minor;
        const auto saved = gp.saved_minor;
        const auto remaining = saved >= target ? std::int64_t{0} : target - saved;

        Json::Value v(Json::objectValue);
        v["id"] = gp.goal.id;
        v["name"] = gp.goal.name;
        v["currency"] = gp.goal.currency;
        v["target"] = api::money_json(money::Money(target, cur));
        v["saved"] = api::money_json(money::Money(saved, cur));
        v["remaining"] = api::money_json(money::Money(remaining, cur));
        v["progress_pct"] =
            target > 0 ? 100.0 * static_cast<double>(saved) /
                             static_cast<double>(target)
                       : 0.0;
        v["reached"] = saved >= target;
        if (gp.goal.target_date.has_value()) {
            v["target_date"] = *gp.goal.target_date;
            const auto td = parse_iso_date(*gp.goal.target_date);
            if (td.has_value()) {
                const int ml = months_until(today_ymd(), *td);
                v["months_left"] = ml;
                const std::int64_t req =
                    (remaining > 0 && ml > 0)
                        ? (remaining + ml - 1) / ml  // ceil
                        : remaining;
                v["required_monthly"] =
                    api::money_json(money::Money(req, cur));
            }
        } else {
            v["target_date"] = Json::Value(Json::nullValue);
        }
        return v;
    }

    Json::Value contribution_json(const storage::Contribution& c,
                                  money::Currency cur) {
        Json::Value v(Json::objectValue);
        v["id"] = c.id;
        v["date"] = c.occurred_on;
        v["amount"] = api::money_json(money::Money(c.minor_units, cur));
        v["note"] = c.note;
        return v;
    }

    // Builds a SavingsGoal from JSON. id is supplied by the caller (generated
    // on create, path param on update). Returns err on validation failure.
    std::optional<storage::SavingsGoal> goal_from_json(const Json::Value& b,
                                                       std::string id,
                                                       std::string& err) {
        if (!b.isMember("name") || !b.isMember("target")) {
            err = "'name' and 'target' are required";
            return std::nullopt;
        }
        storage::SavingsGoal g;
        g.id = std::move(id);
        g.name = b["name"].asString();
        auto cur = money::Currency::EUR;
        if (b.isMember("currency")) {
            const auto c = money::currency_from_string(b["currency"].asString());
            if (!c.has_value()) {
                err = "unknown currency";
                return std::nullopt;
            }
            cur = *c;
        }
        g.currency = std::string(money::to_string(cur));
        const auto& t = b["target"];
        std::optional<money::Money> target;
        if (t.isString()) {
            target = money::Money::from_string(t.asString(), cur);
        } else {
            target = money::Money(t.asInt64(), cur);
        }
        if (!target.has_value() || target->is_negative() || target->is_zero()) {
            err = "'target' must be a strictly positive amount";
            return std::nullopt;
        }
        g.target_minor = target->minor_units();
        if (b.isMember("target_date") && !b["target_date"].isNull()) {
            const auto d = b["target_date"].asString();
            if (!parse_iso_date(d).has_value()) {
                err = "'target_date' must be YYYY-MM-DD";
                return std::nullopt;
            }
            g.target_date = d;
        }
        return g;
    }

    // Mirrors apps/cli compute_report + rendering, emitting JSON instead of text.
    Json::Value build_report_json(const ledger::Journal& journal,
                                  std::string_view account,
                                  std::optional<std::chrono::year_month> requested_month,
                                  const cli::BudgetConfig& cfg) {
        const auto registry = budget::default_registry();
        const auto currency = cfg.currency;

        std::chrono::year_month month{};
        if (requested_month.has_value()) {
            month = *requested_month;
        } else {
            for (const auto& tx : journal.transactions()) {
                const std::chrono::year_month ym{tx.date().year(), tx.date().month()};
                if (ym > month) {
                    month = ym;
                }
            }
        }

        auto inflow = money::Money::zero(currency);
        auto outflow = money::Money::zero(currency);
        auto net = money::Money::zero(currency);
        std::map<std::string, money::Money> by_cat;
        std::map<budget::Bucket, money::Money> by_bucket;

        for (const auto& tx : journal.transactions()) {
            if (tx.date().year() != month.year() || tx.date().month() != month.month()) {
                continue;
            }
            if (is_transfer(registry, tx.category_id())) {
                continue;
            }
            if (tx.is_inflow()) {
                inflow += tx.amount();
                net += tx.amount();
            } else {
                const auto positive = -tx.amount();
                outflow += positive;
                net += tx.amount();
                const auto key = tx.category_id().value_or("");
                auto [it, _] = by_cat.try_emplace(key, money::Money::zero(currency));
                it->second += positive;
                if (const auto* c = registry.find(key);
                    c != nullptr && c->bucket() != budget::Bucket::INCOME &&
                    c->bucket() != budget::Bucket::TRANSFERS) {
                    auto [bit, __] = by_bucket.try_emplace(c->bucket(),
                                                           money::Money::zero(currency));
                    bit->second += positive;
                }
            }
        }

        Json::Value out(Json::objectValue);
        out["account"] = std::string(account);
        out["month"] = api::format_year_month(month);
        out["inflow"] = api::money_json(inflow);
        out["outflow"] = api::money_json(outflow);
        out["net"] = api::money_json(net);

        Json::Value buckets(Json::arrayValue);
        for (const auto b : {budget::Bucket::NEEDS, budget::Bucket::WANTS,
                             budget::Bucket::SAVINGS_INVEST}) {
            const auto it = by_bucket.find(b);
            const auto amt = it != by_bucket.end() ? it->second
                                                   : money::Money::zero(currency);
            Json::Value row(Json::objectValue);
            row["bucket"] = std::string(budget::to_string(b));
            row["amount"] = api::money_json(amt);
            row["pct_of_outflow"] =
                format_pct_pair(amt.minor_units(), outflow.minor_units());
            buckets.append(row);
        }
        out["by_bucket"] = buckets;

        std::vector<std::pair<std::string, money::Money>> rows(by_cat.begin(), by_cat.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            return a.second.minor_units() > b.second.minor_units();
        });
        Json::Value cats(Json::arrayValue);
        for (const auto& [cat, amt] : rows) {
            Json::Value row(Json::objectValue);
            row["category"] = cat.empty() ? "(uncategorised)" : cat;
            row["amount"] = api::money_json(amt);
            row["pct_of_outflow"] =
                format_pct_pair(amt.minor_units(), outflow.minor_units());
            cats.append(row);
        }
        out["by_category"] = cats;

        Json::Value bvs(Json::arrayValue);
        if (!cfg.limits.empty()) {
            budget::Budget b{month, currency};
            for (const auto& [cat, limit] : cfg.limits) {
                b.set_limit(cat, limit);
            }
            for (const auto& line : budget::spending_vs_budget(b, journal)) {
                Json::Value row(Json::objectValue);
                row["category"] =
                    line.category_id.empty() ? "(uncategorised)" : line.category_id;
                row["spent"] = api::money_json(line.spent);
                row["budgeted"] = api::money_json(line.budgeted);
                row["delta"] = api::money_json(line.delta);
                row["over_budget"] = line.delta.is_negative();
                bvs.append(row);
            }
        }
        out["budget_vs_spending"] = bvs;
        return out;
    }

    // ---- multipart helpers ---------------------------------------------------

    struct Upload {
        std::string content;
        std::string account;
        std::map<std::string, std::string> params;
        bool ok = false;
        std::string error;
    };

    Upload read_upload(const HttpRequestPtr& req) {
        Upload up;
        drogon::MultiPartParser parser;
        if (parser.parse(req) != 0 || parser.getFiles().empty()) {
            up.error = "expected a multipart/form-data body with a 'file' field";
            return up;
        }
        up.content = std::string(parser.getFiles()[0].fileContent());
        for (const auto& [k, v] : parser.getParameters()) {
            up.params[k] = v;
        }
        if (const auto it = up.params.find("account"); it != up.params.end()) {
            up.account = it->second;
        } else if (auto q = req->getParameter("account"); !q.empty()) {
            up.account = q;
        }
        if (up.account.empty()) {
            up.error = "missing 'account' field";
            return up;
        }
        up.ok = true;
        return up;
    }

    // ---- handlers ------------------------------------------------------------

    void h_allocate(const HttpRequestPtr& req, Callback&& cb) {
        const auto income_s = req->getParameter("income");
        if (income_s.empty()) {
            return send_error(cb, "query parameter 'income' is required");
        }
        auto currency = money::Currency::EUR;
        if (auto c = req->getParameter("currency"); !c.empty()) {
            const auto parsed = money::currency_from_string(c);
            if (!parsed.has_value()) {
                return send_error(cb, "unknown currency '" + c + "'");
            }
            currency = *parsed;
        }
        const auto income = money::Money::from_string(income_s, currency);
        if (!income.has_value()) {
            return send_error(cb, "'" + income_s + "' is not a valid amount");
        }
        if (income->is_zero() || income->is_negative()) {
            return send_error(cb, "income must be strictly positive");
        }
        const auto alloc = budget::allocate_50_30_20(*income);
        Json::Value out(Json::objectValue);
        out["income"] = api::money_json(*income);
        out["needs"] = api::money_json(alloc.at(budget::Bucket::NEEDS));
        out["wants"] = api::money_json(alloc.at(budget::Bucket::WANTS));
        out["savings_invest"] =
            api::money_json(alloc.at(budget::Bucket::SAVINGS_INVEST));
        send_json(cb, out);
    }

    void h_categories(const HttpRequestPtr&, Callback&& cb) {
        const auto reg = budget::default_registry();
        std::map<budget::Bucket, Json::Value> grouped;
        for (const auto& c : reg.all()) {
            Json::Value entry(Json::objectValue);
            entry["id"] = c.id();
            entry["name"] = c.name();
            grouped[c.bucket()].append(entry);
        }
        Json::Value out(Json::objectValue);
        for (const auto b : {budget::Bucket::NEEDS, budget::Bucket::WANTS,
                             budget::Bucket::SAVINGS_INVEST, budget::Bucket::INCOME,
                             budget::Bucket::TRANSFERS}) {
            Json::Value arr = grouped.count(b) != 0U ? grouped[b] : Json::Value(Json::arrayValue);
            out[std::string(budget::to_string(b))] = arr;
        }
        send_json(cb, out);
    }

    void h_import(const HttpRequestPtr& req, Callback&& cb) {
        const auto up = read_upload(req);
        if (!up.ok) {
            return send_error(cb, up.error);
        }
        const auto overrides = db_make_categorizer(req, cb);
        if (!overrides.has_value()) {
            return;
        }
        std::istringstream in{up.content};
        import_::ImportOptions opts;
        opts.account_id = up.account;
        opts.overrides = &*overrides;
        try {
            const auto rep = import_::import_bp_csv(in, opts);
            Json::Value out(Json::objectValue);
            out["rows_seen"] = static_cast<Json::UInt64>(rep.rows_seen);
            out["rows_dropped"] = static_cast<Json::UInt64>(rep.rows_dropped);
            out["transactions"] = static_cast<Json::UInt64>(rep.transactions.size());
            out["categorised"] = static_cast<Json::UInt64>(rep.categorised);
            out["uncategorised"] = static_cast<Json::UInt64>(rep.uncategorised);
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what());
        }
    }

    void h_report_csv(const HttpRequestPtr& req, Callback&& cb) {
        const auto up = read_upload(req);
        if (!up.ok) {
            return send_error(cb, up.error);
        }
        std::optional<std::chrono::year_month> month;
        if (const auto it = up.params.find("month"); it != up.params.end()) {
            month = api::parse_year_month(it->second);
            if (!month.has_value()) {
                return send_error(cb, "'month' must be YYYY-MM");
            }
        }
        const auto cfg = db_load_budget(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        const auto overrides = db_make_categorizer(req, cb);
        if (!overrides.has_value()) {
            return;
        }
        try {
            std::istringstream in{up.content};
            const auto journal =
                journal_from_csv(in, up.account, cfg->currency, *overrides);
            if (journal.empty()) {
                return send_error(cb, "no transactions parsed from CSV");
            }
            send_json(cb, build_report_json(journal, up.account, month, *cfg));
        } catch (const std::exception& e) {
            send_error(cb, e.what());
        }
    }

    void h_report_db(const HttpRequestPtr& req, Callback&& cb) {
        const auto account = req->getParameter("account");
        if (account.empty()) {
            return send_error(cb, "query parameter 'account' is required");
        }
        std::optional<std::chrono::year_month> month;
        if (auto m = req->getParameter("month"); !m.empty()) {
            month = api::parse_year_month(m);
            if (!month.has_value()) {
                return send_error(cb, "'month' must be YYYY-MM");
            }
        }
        if (!require_account_owner(req, cb, account)) {
            return;
        }
        std::string err;
        auto journal = journal_from_db(account, err);
        if (!journal.has_value()) {
            return send_error(cb, err, drogon::k500InternalServerError);
        }
        if (journal->empty()) {
            return send_error(cb, "no transactions in DB for account " + account);
        }
        const auto cfg = db_load_budget(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        send_json(cb, build_report_json(*journal, account, month, *cfg));
    }

    void h_budget_get(const HttpRequestPtr& req, Callback&& cb) {
        const auto cfg = db_load_budget(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        Json::Value out(Json::objectValue);
        out["currency"] = std::string(money::to_string(cfg->currency));
        Json::Value limits(Json::arrayValue);
        auto total = money::Money::zero(cfg->currency);
        for (const auto& [cat, limit] : cfg->limits) {
            Json::Value row(Json::objectValue);
            row["category"] = cat;
            row["limit"] = api::money_json(limit);
            limits.append(row);
            total += limit;
        }
        out["limits"] = limits;
        out["total"] = api::money_json(total);
        send_json(cb, out);
    }

    void h_budget_post(const HttpRequestPtr& req, Callback&& cb) {
        const auto body = req->getJsonObject();
        if (!body || !body->isMember("category") || !body->isMember("amount")) {
            return send_error(cb, "JSON body with 'category' and 'amount' is required");
        }
        const auto category = (*body)["category"].asString();
        const auto amount_s = (*body)["amount"].asString();
        auto cfg = db_load_budget(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        if (body->isMember("currency")) {
            const auto parsed = money::currency_from_string((*body)["currency"].asString());
            if (!parsed.has_value()) {
                return send_error(cb, "unknown currency");
            }
            cfg->currency = *parsed;
        }
        const auto amount = money::Money::from_string(amount_s, cfg->currency);
        if (!amount.has_value()) {
            return send_error(cb, "'" + amount_s + "' is not a valid amount");
        }
        if (amount->is_negative()) {
            return send_error(cb, "limit must be non-negative");
        }
        cfg->limits[category] = *amount;
        if (!db_save_budget(req, cb, *cfg)) {
            return;
        }
        h_budget_get(req, std::move(cb));
    }

    void h_rules_get(const HttpRequestPtr& req, Callback&& cb) {
        const auto cfg = db_load_rules(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        Json::Value rules(Json::arrayValue);
        for (const auto& r : cfg->rules) {
            Json::Value row(Json::objectValue);
            row["pattern"] = r.pattern;
            row["category"] = r.category_id;
            if (r.bp_subcategory.has_value()) {
                row["bp_subcategory"] = *r.bp_subcategory;
            }
            rules.append(row);
        }
        Json::Value out(Json::objectValue);
        out["rules"] = rules;
        send_json(cb, out);
    }

    void h_rules_post(const HttpRequestPtr& req, Callback&& cb) {
        const auto body = req->getJsonObject();
        if (!body || !body->isMember("pattern") || !body->isMember("category")) {
            return send_error(cb, "JSON body with 'pattern' and 'category' is required");
        }
        const auto pattern = (*body)["pattern"].asString();
        const auto category = (*body)["category"].asString();
        std::optional<std::string> bp_sub;
        if (body->isMember("bp_subcategory") && !(*body)["bp_subcategory"].isNull()) {
            bp_sub = (*body)["bp_subcategory"].asString();
        }
        try {
            static_cast<void>(std::regex(pattern,
                                         std::regex::ECMAScript | std::regex::icase));
        } catch (const std::regex_error& e) {
            return send_error(cb, std::string("invalid regex: ") + e.what());
        }
        auto cfg = db_load_rules(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        bool updated = false;
        for (auto& r : cfg->rules) {
            if (r.pattern == pattern) {
                r.category_id = category;
                r.bp_subcategory = bp_sub;
                updated = true;
                break;
            }
        }
        if (!updated) {
            cfg->rules.push_back(cli::Rule{pattern, category, bp_sub});
        }
        if (!db_save_rules(req, cb, *cfg)) {
            return;
        }
        h_rules_get(req, std::move(cb));
    }

    void h_rules_delete(const HttpRequestPtr& req, Callback&& cb) {
        const auto pattern = req->getParameter("pattern");
        if (pattern.empty()) {
            return send_error(cb, "query parameter 'pattern' is required");
        }
        auto cfg = db_load_rules(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        const auto before = cfg->rules.size();
        cfg->rules.erase(std::remove_if(cfg->rules.begin(), cfg->rules.end(),
                                        [&](const cli::Rule& r) {
                                            return r.pattern == pattern;
                                        }),
                         cfg->rules.end());
        if (cfg->rules.size() == before) {
            return send_error(cb, "no rule matched '" + pattern + "'",
                              drogon::k404NotFound);
        }
        if (!db_save_rules(req, cb, *cfg)) {
            return;
        }
        h_rules_get(req, std::move(cb));
    }

    void h_sync(const HttpRequestPtr& req, Callback&& cb) {
        const auto up = read_upload(req);
        if (!up.ok) {
            return send_error(cb, up.error);
        }
        const auto url = storage::Connection::database_url_from_env();
        if (!url.has_value()) {
            return send_error(cb, "$DATABASE_URL is not set on the server",
                              drogon::k500InternalServerError);
        }
        const auto cfg = db_load_budget(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        const auto overrides = db_make_categorizer(req, cb);
        if (!overrides.has_value()) {
            return;
        }
        try {
            std::istringstream in{up.content};
            import_::ImportOptions opts;
            opts.account_id = up.account;
            opts.currency = cfg->currency;
            opts.overrides = &*overrides;
            const auto imp = import_::import_bp_csv(in, opts);

            storage::Connection conn{*url};
            storage::apply_default_migrations(conn);
            storage::AccountRepository accounts{conn};
            accounts.ensure(ledger::Account{up.account, up.account, cfg->currency,
                                            ledger::AccountType::CASH},
                            uid_of(req));
            storage::TransactionRepository txs{conn};
            const auto stats = txs.upsert(imp.transactions);

            Json::Value out(Json::objectValue);
            out["account"] = up.account;
            out["imported"] = static_cast<Json::UInt64>(imp.transactions.size());
            out["rows_dropped"] = static_cast<Json::UInt64>(imp.rows_dropped);
            out["inserted"] = static_cast<Json::UInt64>(stats.inserted);
            out["updated"] = static_cast<Json::UInt64>(stats.updated);
            out["unchanged"] = static_cast<Json::UInt64>(
                imp.transactions.size() - stats.inserted - stats.updated);
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    Json::Value suggestions_json(const ledger::Journal& journal,
                                 std::optional<std::chrono::year_month> ending_opt,
                                 unsigned months, money::Currency currency) {
        std::chrono::year_month ending{};
        if (ending_opt.has_value()) {
            ending = *ending_opt;
        } else {
            for (const auto& tx : journal.transactions()) {
                const std::chrono::year_month ym{tx.date().year(), tx.date().month()};
                if (ym > ending) {
                    ending = ym;
                }
            }
        }
        const auto sugg = analytics::suggest_budget(journal, ending, months, currency);
        Json::Value out(Json::objectValue);
        out["months"] = months;
        out["ending"] = api::format_year_month(ending);
        Json::Value arr(Json::arrayValue);
        auto total_avg = money::Money::zero(currency);
        auto total_sugg = money::Money::zero(currency);
        for (const auto& s : sugg) {
            Json::Value row(Json::objectValue);
            row["category"] =
                s.category_id.empty() ? "(uncategorised)" : s.category_id;
            row["rolling_average"] = api::money_json(s.rolling_average);
            row["suggested_limit"] = api::money_json(s.suggested_limit);
            arr.append(row);
            total_avg += s.rolling_average;
            total_sugg += s.suggested_limit;
        }
        out["suggestions"] = arr;
        out["total_rolling_average"] = api::money_json(total_avg);
        out["total_suggested"] = api::money_json(total_sugg);
        return out;
    }

    void h_suggest_csv(const HttpRequestPtr& req, Callback&& cb) {
        const auto up = read_upload(req);
        if (!up.ok) {
            return send_error(cb, up.error);
        }
        unsigned months = 3;
        if (const auto it = up.params.find("months"); it != up.params.end()) {
            try {
                const int v = std::stoi(it->second);
                if (v <= 0) {
                    return send_error(cb, "'months' must be a positive integer");
                }
                months = static_cast<unsigned>(v);
            } catch (...) {
                return send_error(cb, "'months' must be a positive integer");
            }
        }
        std::optional<std::chrono::year_month> ending;
        if (const auto it = up.params.find("ending"); it != up.params.end()) {
            ending = api::parse_year_month(it->second);
            if (!ending.has_value()) {
                return send_error(cb, "'ending' must be YYYY-MM");
            }
        }
        const auto cfg = db_load_budget(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        const auto overrides = db_make_categorizer(req, cb);
        if (!overrides.has_value()) {
            return;
        }
        try {
            std::istringstream in{up.content};
            const auto journal =
                journal_from_csv(in, up.account, cfg->currency, *overrides);
            if (journal.empty()) {
                return send_error(cb, "no transactions parsed from CSV");
            }
            send_json(cb, suggestions_json(journal, ending, months, cfg->currency));
        } catch (const std::exception& e) {
            send_error(cb, e.what());
        }
    }

    void h_suggest_db(const HttpRequestPtr& req, Callback&& cb) {
        const auto account = req->getParameter("account");
        if (account.empty()) {
            return send_error(cb, "query parameter 'account' is required");
        }
        unsigned months = 3;
        if (auto m = req->getParameter("months"); !m.empty()) {
            try {
                const int v = std::stoi(m);
                if (v <= 0) {
                    return send_error(cb, "'months' must be a positive integer");
                }
                months = static_cast<unsigned>(v);
            } catch (...) {
                return send_error(cb, "'months' must be a positive integer");
            }
        }
        std::optional<std::chrono::year_month> ending;
        if (auto e = req->getParameter("ending"); !e.empty()) {
            ending = api::parse_year_month(e);
            if (!ending.has_value()) {
                return send_error(cb, "'ending' must be YYYY-MM");
            }
        }
        if (!require_account_owner(req, cb, account)) {
            return;
        }
        std::string err;
        auto journal = journal_from_db(account, err);
        if (!journal.has_value()) {
            return send_error(cb, err, drogon::k500InternalServerError);
        }
        if (journal->empty()) {
            return send_error(cb, "no transactions in DB for account " + account);
        }
        const auto cfg = db_load_budget(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        send_json(cb, suggestions_json(*journal, ending, months, cfg->currency));
    }

    // 11-column CSV matching SORTED_DATA.xlsx, identical to `wt export`.
    std::string export_csv(const ledger::Journal& journal) {
        auto fmt_dmy = [](std::chrono::year_month_day d) {
            std::ostringstream o;
            o << std::setw(2) << std::setfill('0') << static_cast<unsigned>(d.day()) << '/'
              << std::setw(2) << std::setfill('0') << static_cast<unsigned>(d.month()) << '/'
              << static_cast<int>(d.year());
            return o.str();
        };
        auto fmt_amount = [](std::int64_t minor) {
            const bool neg = minor < 0;
            const std::int64_t a = neg ? -minor : minor;
            std::ostringstream o;
            if (neg) {
                o << '-';
            }
            o << a / 100 << ',' << std::setw(2) << std::setfill('0') << a % 100;
            return o.str();
        };
        std::ostringstream csv;
        csv << "Date de comptabilisation;Libelle simplifie;Libelle operation;"
               "Type operation;Categorie;Sous categorie;Debit;Credit;"
               "Date operation;Date de valeur;Colonne 1\r\n";
        for (const auto& t : journal.transactions()) {
            std::string simple = t.description();
            std::string op = t.description();
            if (const auto sep = simple.find(" — "); sep != std::string::npos) {
                op = simple.substr(sep + 5);
                simple = simple.substr(0, sep);
            }
            const std::string debit =
                t.is_outflow() ? fmt_amount(t.amount().minor_units()) : "";
            const std::string credit =
                t.is_inflow() ? "+" + fmt_amount(t.amount().minor_units()) : "";
            csv << fmt_dmy(t.date()) << ';' << simple << ';' << op << ';'
                << t.type_operation() << ';' << t.bp_category().value_or("") << ';'
                << t.bp_subcategory().value_or("") << ';' << debit << ';' << credit
                << ';' << fmt_dmy(t.date()) << ';' << fmt_dmy(t.date()) << ';'
                << (t.is_reconciled() ? "True" : "False") << "\r\n";
        }
        return csv.str();
    }

    void send_csv(const Callback& cb, const std::string& body) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k200OK);
        resp->setContentTypeString("text/csv; charset=utf-8");
        resp->addHeader("Content-Disposition",
                        "attachment; filename=\"export.csv\"");
        resp->setBody(body);
        cb(resp);
    }

    void h_export_csv(const HttpRequestPtr& req, Callback&& cb) {
        const auto up = read_upload(req);
        if (!up.ok) {
            return send_error(cb, up.error);
        }
        const auto cfg = db_load_budget(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        const auto overrides = db_make_categorizer(req, cb);
        if (!overrides.has_value()) {
            return;
        }
        try {
            std::istringstream in{up.content};
            const auto journal =
                journal_from_csv(in, up.account, cfg->currency, *overrides);
            send_csv(cb, export_csv(journal));
        } catch (const std::exception& e) {
            send_error(cb, e.what());
        }
    }

    void h_export_db(const HttpRequestPtr& req, Callback&& cb) {
        const auto account = req->getParameter("account");
        if (account.empty()) {
            return send_error(cb, "query parameter 'account' is required");
        }
        std::string err;
        auto journal = journal_from_db(account, err);
        if (!journal.has_value()) {
            return send_error(cb, err, drogon::k500InternalServerError);
        }
        send_csv(cb, export_csv(*journal));
    }

    // ---- CRUD: accounts & transactions (Postgres) ---------------------------

    // Opens a DB connection or, on failure, sends a 500 and returns nullopt.
    std::optional<storage::Connection> open_db(const Callback& cb) {
        const auto url = storage::Connection::database_url_from_env();
        if (!url.has_value()) {
            send_error(cb, "$DATABASE_URL is not set on the server",
                       drogon::k500InternalServerError);
            return std::nullopt;
        }
        try {
            return std::optional<storage::Connection>{std::in_place, *url};
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
            return std::nullopt;
        }
    }

    // ---- auth gating ---------------------------------------------------------

    bool is_public_path(const std::string& p) {
        return p == "/" || p == "/swagger" || p == "/openapi.json" ||
               p == "/api/auth/register" || p == "/api/auth/login";
    }

    // Premium (paid) surface. Everything else under /api/ is free but still
    // requires a valid token. Prefix match covers /{id} path params.
    bool is_premium_path(const std::string& p) {
        static const std::array<std::string_view, 11> prefixes{
            "/api/report",   "/api/suggest",      "/api/export",
            "/api/sync",     "/api/accounts",     "/api/transactions",
            "/api/networth", "/api/trends",       "/api/goals",
            "/api/recurring", "/api/forecast"};
        for (const auto pre : prefixes) {
            if (p.size() >= pre.size() && p.compare(0, pre.size(), pre) == 0) {
                return true;
            }
        }
        return false;
    }

    // Authenticated user id, stamped on the request by the pre-handling advice.
    std::string uid_of(const HttpRequestPtr& req) {
        return req->getAttributes()->get<std::string>("uid");
    }

    // Ensures `account` exists and belongs to the caller; otherwise sends the
    // error response itself and returns false.
    bool require_account_owner(const HttpRequestPtr& req, const Callback& cb,
                               const std::string& account) {
        auto db = open_db(cb);
        if (!db) {
            return false;
        }
        try {
            storage::AccountRepository ar{*db};
            if (!ar.find(account, uid_of(req)).has_value()) {
                send_error(cb, "account '" + account + "' not found",
                           drogon::k404NotFound);
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
            return false;
        }
    }

    // ---- per-user budget & rules (Postgres-backed) --------------------------

    std::optional<cli::BudgetConfig> db_load_budget(const HttpRequestPtr& req,
                                                    const Callback& cb) {
        auto db = open_db(cb);
        if (!db) {
            return std::nullopt;
        }
        try {
            const storage::BudgetConfigRepository repo{*db};
            const auto sb = repo.get(uid_of(req));
            cli::BudgetConfig cfg;
            cfg.currency = money::currency_from_string(sb.currency)
                               .value_or(money::Currency::EUR);
            for (const auto& [category, minor] : sb.limits) {
                cfg.limits[category] = money::Money(minor, cfg.currency);
            }
            return cfg;
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
            return std::nullopt;
        }
    }

    bool db_save_budget(const HttpRequestPtr& req, const Callback& cb,
                        const cli::BudgetConfig& cfg) {
        auto db = open_db(cb);
        if (!db) {
            return false;
        }
        try {
            storage::StoredBudget sb;
            sb.currency = std::string(money::to_string(cfg.currency));
            for (const auto& [category, amount] : cfg.limits) {
                sb.limits.emplace_back(category, amount.minor_units());
            }
            storage::BudgetConfigRepository repo{*db};
            repo.replace(uid_of(req), sb);
            return true;
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
            return false;
        }
    }

    std::optional<cli::RulesConfig> db_load_rules(const HttpRequestPtr& req,
                                                  const Callback& cb) {
        auto db = open_db(cb);
        if (!db) {
            return std::nullopt;
        }
        try {
            const storage::RulesRepository repo{*db};
            cli::RulesConfig cfg;
            for (auto& r : repo.list(uid_of(req))) {
                cfg.rules.push_back(cli::Rule{std::move(r.pattern),
                                              std::move(r.category_id),
                                              std::move(r.bp_subcategory)});
            }
            return cfg;
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
            return std::nullopt;
        }
    }

    bool db_save_rules(const HttpRequestPtr& req, const Callback& cb,
                       const cli::RulesConfig& cfg) {
        auto db = open_db(cb);
        if (!db) {
            return false;
        }
        try {
            std::vector<storage::StoredRule> rows;
            rows.reserve(cfg.rules.size());
            for (const auto& r : cfg.rules) {
                rows.push_back(storage::StoredRule{r.pattern, r.category_id,
                                                   r.bp_subcategory});
            }
            storage::RulesRepository repo{*db};
            repo.replace(uid_of(req), rows);
            return true;
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
            return false;
        }
    }

    // User rules first (priority), then the built-in default overrides.
    std::optional<import_::Categorizer> db_make_categorizer(
        const HttpRequestPtr& req, const Callback& cb) {
        const auto rules = db_load_rules(req, cb);
        if (!rules.has_value()) {
            return std::nullopt;
        }
        return cli::build_categorizer(*rules, import_::default_overrides());
    }

    // True if `account` exists and belongs to `uid`.
    bool owns_account(storage::AccountRepository& ar, const std::string& account,
                      const std::string& uid) {
        return ar.find(account, uid).has_value();
    }

    void h_accounts_get_all(const HttpRequestPtr& req, Callback&& cb) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::AccountRepository repo{*db};
            Json::Value arr(Json::arrayValue);
            for (const auto& b : repo.balances(uid_of(req))) {
                arr.append(balance_json(b));
            }
            Json::Value out(Json::objectValue);
            out["accounts"] = arr;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_accounts_get_one(const HttpRequestPtr& req, Callback&& cb,
                            std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::AccountRepository repo{*db};
            const auto b = repo.balance_of(uid_of(req), id);
            if (!b.has_value()) {
                return send_error(cb, "account '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            send_json(cb, balance_json(*b));
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_account_balance(const HttpRequestPtr& req, Callback&& cb,
                           std::string id) {
        h_accounts_get_one(req, std::move(cb), std::move(id));
    }

    void h_accounts_post(const HttpRequestPtr& req, Callback&& cb) {
        const auto body = req->getJsonObject();
        if (!body) {
            return send_error(cb, "JSON body is required");
        }
        std::string err;
        const std::string id =
            body->isMember("id") ? (*body)["id"].asString() : std::string{};
        const auto account = account_from_json(*body, id, err);
        if (!account.has_value()) {
            return send_error(cb, err);
        }
        const auto opening = opening_balance_from_json(*body, account->currency(), err);
        if (!opening.has_value()) {
            return send_error(cb, err);
        }
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::AccountRepository repo{*db};
            if (!repo.ensure(*account, uid_of(req), *opening)) {
                return send_error(cb, "account '" + id + "' already exists",
                                  drogon::k409Conflict);
            }
            const auto b = repo.balance_of(uid_of(req), id);
            send_json(cb, b.has_value() ? balance_json(*b) : account_json(*account),
                      drogon::k201Created);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_accounts_put(const HttpRequestPtr& req, Callback&& cb, std::string id) {
        const auto body = req->getJsonObject();
        if (!body) {
            return send_error(cb, "JSON body is required");
        }
        std::string err;
        const auto account = account_from_json(*body, id, err);
        if (!account.has_value()) {
            return send_error(cb, err);
        }
        const auto opening = opening_balance_from_json(*body, account->currency(), err);
        if (!opening.has_value()) {
            return send_error(cb, err);
        }
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::AccountRepository repo{*db};
            if (!repo.update(*account, uid_of(req), *opening)) {
                return send_error(cb, "account '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            const auto b = repo.balance_of(uid_of(req), id);
            send_json(cb, b.has_value() ? balance_json(*b) : account_json(*account));
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_accounts_delete(const HttpRequestPtr& req, Callback&& cb,
                           std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::AccountRepository repo{*db};
            if (!repo.remove(id, uid_of(req))) {
                return send_error(cb, "account '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            Json::Value out(Json::objectValue);
            out["deleted"] = id;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_tx_get_all(const HttpRequestPtr& req, Callback&& cb) {
        const auto account = req->getParameter("account");
        if (account.empty()) {
            return send_error(cb, "query parameter 'account' is required");
        }
        std::optional<std::chrono::year_month> month;
        if (auto m = req->getParameter("month"); !m.empty()) {
            month = api::parse_year_month(m);
            if (!month.has_value()) {
                return send_error(cb, "'month' must be YYYY-MM");
            }
        }
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::AccountRepository accounts{*db};
            if (!owns_account(accounts, account, uid_of(req))) {
                return send_error(cb, "account '" + account + "' not found",
                                  drogon::k404NotFound);
            }
            const storage::TransactionRepository repo{*db};
            const auto rows = month.has_value() ? repo.for_month(account, *month)
                                                : repo.for_account(account);
            Json::Value arr(Json::arrayValue);
            for (const auto& t : rows) {
                arr.append(transaction_json(t));
            }
            Json::Value out(Json::objectValue);
            out["account"] = account;
            out["count"] = static_cast<Json::UInt64>(rows.size());
            out["transactions"] = arr;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_tx_get_one(const HttpRequestPtr& req, Callback&& cb, std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::TransactionRepository repo{*db};
            const auto t = repo.find(id);
            const storage::AccountRepository accounts{*db};
            if (!t.has_value() ||
                accounts.owner_of(t->account_id()) != uid_of(req)) {
                return send_error(cb, "transaction '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            send_json(cb, transaction_json(*t));
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_tx_post(const HttpRequestPtr& req, Callback&& cb) {
        const auto body = req->getJsonObject();
        if (!body) {
            return send_error(cb, "JSON body is required");
        }
        std::string err;
        const std::string id =
            body->isMember("id") ? (*body)["id"].asString() : std::string{};
        const auto t = transaction_from_json(*body, id, err);
        if (!t.has_value()) {
            return send_error(cb, err);
        }
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::AccountRepository accounts{*db};
            if (!owns_account(accounts, t->account_id(), uid_of(req))) {
                return send_error(cb, "account '" + t->account_id() +
                                          "' not found for this user",
                                  drogon::k404NotFound);
            }
            storage::TransactionRepository repo{*db};
            if (repo.find(id).has_value()) {
                return send_error(cb, "transaction '" + id + "' already exists",
                                  drogon::k409Conflict);
            }
            const ledger::Transaction one[] = {*t};
            repo.upsert(one);
            send_json(cb, transaction_json(*t), drogon::k201Created);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_tx_put(const HttpRequestPtr& req, Callback&& cb, std::string id) {
        const auto body = req->getJsonObject();
        if (!body) {
            return send_error(cb, "JSON body is required");
        }
        std::string err;
        const auto t = transaction_from_json(*body, id, err);
        if (!t.has_value()) {
            return send_error(cb, err);
        }
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::TransactionRepository repo{*db};
            const auto existing = repo.find(id);
            storage::AccountRepository accounts{*db};
            if (!existing.has_value() ||
                accounts.owner_of(existing->account_id()) != uid_of(req)) {
                return send_error(cb, "transaction '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            if (!owns_account(accounts, t->account_id(), uid_of(req))) {
                return send_error(cb, "account '" + t->account_id() +
                                          "' not found for this user",
                                  drogon::k404NotFound);
            }
            const ledger::Transaction one[] = {*t};
            storage::TransactionRepository{*db}.upsert(one);
            send_json(cb, transaction_json(*t));
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_tx_delete(const HttpRequestPtr& req, Callback&& cb, std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::TransactionRepository repo{*db};
            const auto existing = repo.find(id);
            const storage::AccountRepository accounts{*db};
            if (!existing.has_value() ||
                accounts.owner_of(existing->account_id()) != uid_of(req)) {
                return send_error(cb, "transaction '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            repo.remove(id);
            Json::Value out(Json::objectValue);
            out["deleted"] = id;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    // ---- budget: getById + delete (per-user, Postgres) ----------------------

    void h_budget_get_one(const HttpRequestPtr& req, Callback&& cb,
                          std::string category) {
        const auto cfg = db_load_budget(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        const auto it = cfg->limits.find(category);
        if (it == cfg->limits.end()) {
            return send_error(cb, "no limit set for category '" + category + "'",
                              drogon::k404NotFound);
        }
        Json::Value out(Json::objectValue);
        out["category"] = category;
        out["limit"] = api::money_json(it->second);
        send_json(cb, out);
    }

    void h_budget_delete(const HttpRequestPtr& req, Callback&& cb,
                         std::string category) {
        auto cfg = db_load_budget(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        if (cfg->limits.erase(category) == 0) {
            return send_error(cb, "no limit set for category '" + category + "'",
                              drogon::k404NotFound);
        }
        if (!db_save_budget(req, cb, *cfg)) {
            return;
        }
        h_budget_get(req, std::move(cb));
    }

    // ---- rules: getByString + update ----------------------------------------

    void h_rules_get_dispatch(const HttpRequestPtr& req, Callback&& cb) {
        const auto pattern = req->getParameter("pattern");
        if (pattern.empty()) {
            return h_rules_get(req, std::move(cb));
        }
        const auto cfg = db_load_rules(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        for (const auto& r : cfg->rules) {
            if (r.pattern == pattern) {
                Json::Value row(Json::objectValue);
                row["pattern"] = r.pattern;
                row["category"] = r.category_id;
                if (r.bp_subcategory.has_value()) {
                    row["bp_subcategory"] = *r.bp_subcategory;
                }
                return send_json(cb, row);
            }
        }
        send_error(cb, "no rule with pattern '" + pattern + "'", drogon::k404NotFound);
    }

    void h_rules_put(const HttpRequestPtr& req, Callback&& cb) {
        const auto body = req->getJsonObject();
        if (!body || !body->isMember("pattern") || !body->isMember("category")) {
            return send_error(cb, "JSON body with 'pattern' and 'category' is required");
        }
        const auto pattern = (*body)["pattern"].asString();
        const auto category = (*body)["category"].asString();
        std::optional<std::string> bp_sub;
        if (body->isMember("bp_subcategory") && !(*body)["bp_subcategory"].isNull()) {
            bp_sub = (*body)["bp_subcategory"].asString();
        }
        auto cfg = db_load_rules(req, cb);
        if (!cfg.has_value()) {
            return;
        }
        bool found = false;
        for (auto& r : cfg->rules) {
            if (r.pattern == pattern) {
                r.category_id = category;
                r.bp_subcategory = bp_sub;
                found = true;
                break;
            }
        }
        if (!found) {
            return send_error(cb, "no rule with pattern '" + pattern +
                                      "' (use POST to create)",
                              drogon::k404NotFound);
        }
        if (!db_save_rules(req, cb, *cfg)) {
            return;
        }
        h_rules_get(req, std::move(cb));
    }

    // ---- auth: register / login / me ----------------------------------------

    Json::Value user_json(const storage::User& u) {
        Json::Value v(Json::objectValue);
        v["id"] = u.id;
        v["email"] = u.email;
        v["plan"] = u.plan;
        return v;
    }

    Json::Value session_json(const storage::User& u) {
        Json::Value v(Json::objectValue);
        v["token"] = api::auth::make_token(u);
        v["token_type"] = "Bearer";
        v["user"] = user_json(u);
        return v;
    }

    void h_register(const HttpRequestPtr& req, Callback&& cb) {
        const auto body = req->getJsonObject();
        if (!body || !body->isMember("email") || !body->isMember("password")) {
            return send_error(cb, "JSON body with 'email' and 'password' is required");
        }
        const auto email = (*body)["email"].asString();
        const auto password = (*body)["password"].asString();
        if (email.find('@') == std::string::npos) {
            return send_error(cb, "invalid email");
        }
        if (password.size() < 8) {
            return send_error(cb, "password must be at least 8 characters");
        }
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::User u;
            u.id = api::auth::new_id();
            u.email = email;
            u.password_hash = api::auth::hash_password(password);
            u.plan = "free";
            storage::UserRepository repo{*db};
            if (!repo.create(u)) {
                return send_error(cb, "email already registered",
                                  drogon::k409Conflict);
            }
            send_json(cb, session_json(u), drogon::k201Created);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_login(const HttpRequestPtr& req, Callback&& cb) {
        const auto body = req->getJsonObject();
        if (!body || !body->isMember("email") || !body->isMember("password")) {
            return send_error(cb, "JSON body with 'email' and 'password' is required");
        }
        const auto email = (*body)["email"].asString();
        const auto password = (*body)["password"].asString();
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::UserRepository repo{*db};
            const auto u = repo.find_by_email(email);
            if (!u.has_value() ||
                !api::auth::verify_password(u->password_hash, password)) {
                return send_error(cb, "invalid email or password",
                                  drogon::k401Unauthorized);
            }
            send_json(cb, session_json(*u));
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_me(const HttpRequestPtr& req, Callback&& cb) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::UserRepository repo{*db};
            const auto u = repo.find_by_id(uid_of(req));
            if (!u.has_value()) {
                return send_error(cb, "user not found", drogon::k404NotFound);
            }
            send_json(cb, user_json(*u));
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    // ---- net worth & trends -------------------------------------------------

    void h_networth(const HttpRequestPtr& req, Callback&& cb) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::AccountRepository repo{*db};
            const auto rows = repo.balances(uid_of(req));
            // Net worth doesn't FX-convert: total per currency.
            std::map<std::string, std::pair<std::int64_t, std::int64_t>> by_ccy;
            Json::Value accounts(Json::arrayValue);
            for (const auto& b : rows) {
                accounts.append(balance_json(b));
                auto& [open, bal] = by_ccy[b.currency];
                open += b.opening_balance;
                bal += b.current_balance;
            }
            Json::Value totals(Json::arrayValue);
            for (const auto& [ccy, sums] : by_ccy) {
                const auto cur = currency_or_eur(ccy);
                Json::Value t(Json::objectValue);
                t["currency"] = ccy;
                t["opening_balance"] =
                    api::money_json(money::Money(sums.first, cur));
                t["net_worth"] = api::money_json(money::Money(sums.second, cur));
                totals.append(t);
            }
            Json::Value out(Json::objectValue);
            out["accounts"] = accounts;
            out["totals"] = totals;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_trends(const HttpRequestPtr& req, Callback&& cb) {
        const auto account = req->getParameter("account");
        if (account.empty()) {
            return send_error(cb, "query parameter 'account' is required");
        }
        std::optional<int> last_n;
        if (auto m = req->getParameter("months"); !m.empty()) {
            try {
                const int v = std::stoi(m);
                if (v <= 0) {
                    return send_error(cb, "'months' must be a positive integer");
                }
                last_n = v;
            } catch (...) {
                return send_error(cb, "'months' must be a positive integer");
            }
        }
        if (!require_account_owner(req, cb, account)) {
            return;
        }
        std::string err;
        auto journal = journal_from_db(account, err);
        if (!journal.has_value()) {
            return send_error(cb, err, drogon::k500InternalServerError);
        }
        try {
            auto db = open_db(cb);
            if (!db) {
                return;
            }
            const storage::AccountRepository repo{*db};
            const auto bal = repo.balance_of(uid_of(req), account);
            const auto currency =
                bal.has_value() ? currency_or_eur(bal->currency)
                                : money::Currency::EUR;
            auto series = analytics::monthly_totals(*journal, currency);
            if (last_n.has_value() &&
                series.size() > static_cast<std::size_t>(*last_n)) {
                series.erase(series.begin(),
                             series.end() - *last_n);
            }
            Json::Value months(Json::arrayValue);
            for (const auto& mt : series) {
                Json::Value row(Json::objectValue);
                row["month"] = api::format_year_month(mt.month);
                row["inflow"] = api::money_json(mt.inflow);
                row["outflow"] = api::money_json(mt.outflow);
                row["net"] = api::money_json(mt.net);
                const auto in = mt.inflow.minor_units();
                row["savings_rate_pct"] =
                    in > 0 ? 100.0 * static_cast<double>(mt.net.minor_units()) /
                                 static_cast<double>(in)
                           : 0.0;
                months.append(row);
            }
            Json::Value out(Json::objectValue);
            out["account"] = account;
            out["months"] = months;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    // ---- savings goals ------------------------------------------------------

    void h_goals_get_all(const HttpRequestPtr& req, Callback&& cb) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::SavingsGoalRepository repo{*db};
            Json::Value arr(Json::arrayValue);
            for (const auto& gp : repo.list(uid_of(req))) {
                arr.append(goal_json(gp));
            }
            Json::Value out(Json::objectValue);
            out["goals"] = arr;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_goals_get_one(const HttpRequestPtr& req, Callback&& cb,
                         std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::SavingsGoalRepository repo{*db};
            const auto gp = repo.find(uid_of(req), id);
            if (!gp.has_value()) {
                return send_error(cb, "goal '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            send_json(cb, goal_json(*gp));
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_goals_post(const HttpRequestPtr& req, Callback&& cb) {
        const auto body = req->getJsonObject();
        if (!body) {
            return send_error(cb, "JSON body is required");
        }
        std::string err;
        const auto goal = goal_from_json(*body, api::auth::new_id(), err);
        if (!goal.has_value()) {
            return send_error(cb, err);
        }
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::SavingsGoalRepository repo{*db};
            if (!repo.create(uid_of(req), *goal)) {
                return send_error(cb, "could not create goal",
                                  drogon::k500InternalServerError);
            }
            const auto gp = repo.find(uid_of(req), goal->id);
            send_json(cb, gp.has_value() ? goal_json(*gp) : Json::Value(),
                      drogon::k201Created);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_goals_put(const HttpRequestPtr& req, Callback&& cb, std::string id) {
        const auto body = req->getJsonObject();
        if (!body) {
            return send_error(cb, "JSON body is required");
        }
        std::string err;
        const auto goal = goal_from_json(*body, id, err);
        if (!goal.has_value()) {
            return send_error(cb, err);
        }
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::SavingsGoalRepository repo{*db};
            if (!repo.update(uid_of(req), *goal)) {
                return send_error(cb, "goal '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            const auto gp = repo.find(uid_of(req), id);
            send_json(cb, gp.has_value() ? goal_json(*gp) : Json::Value());
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_goals_delete(const HttpRequestPtr& req, Callback&& cb,
                        std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::SavingsGoalRepository repo{*db};
            if (!repo.remove(uid_of(req), id)) {
                return send_error(cb, "goal '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            Json::Value out(Json::objectValue);
            out["deleted"] = id;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_goal_contrib_post(const HttpRequestPtr& req, Callback&& cb,
                             std::string id) {
        const auto body = req->getJsonObject();
        if (!body || !body->isMember("amount")) {
            return send_error(cb, "JSON body with 'amount' is required");
        }
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::SavingsGoalRepository repo{*db};
            const auto gp = repo.find(uid_of(req), id);
            if (!gp.has_value()) {
                return send_error(cb, "goal '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            const auto cur = currency_or_eur(gp->goal.currency);
            const auto& a = (*body)["amount"];
            std::optional<money::Money> amount;
            if (a.isString()) {
                amount = money::Money::from_string(a.asString(), cur);
            } else {
                amount = money::Money(a.asInt64(), cur);
            }
            if (!amount.has_value() || amount->is_zero()) {
                return send_error(cb, "'amount' must be a non-zero amount "
                                      "(negative = withdrawal)");
            }
            storage::Contribution c;
            c.id = api::auth::new_id();
            if (body->isMember("date") && !(*body)["date"].isNull()) {
                const auto d = (*body)["date"].asString();
                if (!parse_iso_date(d).has_value()) {
                    return send_error(cb, "'date' must be YYYY-MM-DD");
                }
                c.occurred_on = d;
            } else {
                c.occurred_on = format_iso_date(today_ymd());
            }
            c.minor_units = amount->minor_units();
            c.note = body->isMember("note") ? (*body)["note"].asString() : "";
            if (!repo.add_contribution(uid_of(req), id, c)) {
                return send_error(cb, "goal '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            const auto updated = repo.find(uid_of(req), id);
            send_json(cb, updated.has_value() ? goal_json(*updated) : Json::Value(),
                      drogon::k201Created);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_goal_contrib_get(const HttpRequestPtr& req, Callback&& cb,
                            std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::SavingsGoalRepository repo{*db};
            const auto gp = repo.find(uid_of(req), id);
            if (!gp.has_value()) {
                return send_error(cb, "goal '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            const auto cur = currency_or_eur(gp->goal.currency);
            Json::Value arr(Json::arrayValue);
            for (const auto& c : repo.list_contributions(id)) {
                arr.append(contribution_json(c, cur));
            }
            Json::Value out(Json::objectValue);
            out["goal"] = id;
            out["contributions"] = arr;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    // ---- recurring & forecast -----------------------------------------------

    std::chrono::year_month_day add_month_ymd(std::chrono::year_month_day d) {
        const auto ym = std::chrono::year_month{d.year(), d.month()} +
                        std::chrono::months{1};
        std::chrono::year_month_day cand{ym.year(), ym.month(), d.day()};
        if (!cand.ok()) {
            return std::chrono::year_month_day{ym.year() / ym.month() /
                                               std::chrono::last};
        }
        return cand;
    }

    std::chrono::year_month_day end_of_month(std::chrono::year_month_day d) {
        return std::chrono::year_month_day{d.year() / d.month() /
                                           std::chrono::last};
    }

    Json::Value recurring_json(const analytics::RecurringItem& r) {
        Json::Value v(Json::objectValue);
        v["label"] = r.label;
        v["category_id"] = r.category_id.has_value()
                               ? Json::Value(*r.category_id)
                               : Json::Value(Json::nullValue);
        v["average_amount"] = api::money_json(r.average_amount);
        v["occurrences"] = r.occurrences;
        v["last_date"] = format_iso_date(r.last_date);
        v["next_date"] = format_iso_date(r.next_date);
        return v;
    }

    money::Currency account_currency(const HttpRequestPtr& req,
                                     const std::string& account) {
        auto db = storage::Connection::database_url_from_env();
        if (!db.has_value()) {
            return money::Currency::EUR;
        }
        try {
            storage::Connection c{*db};
            storage::AccountRepository repo{c};
            if (const auto b = repo.balance_of(uid_of(req), account);
                b.has_value()) {
                return currency_or_eur(b->currency);
            }
        } catch (...) {
        }
        return money::Currency::EUR;
    }

    void h_recurring(const HttpRequestPtr& req, Callback&& cb) {
        const auto account = req->getParameter("account");
        if (account.empty()) {
            return send_error(cb, "query parameter 'account' is required");
        }
        if (!require_account_owner(req, cb, account)) {
            return;
        }
        std::string err;
        const auto journal = journal_from_db(account, err);
        if (!journal.has_value()) {
            return send_error(cb, err, drogon::k500InternalServerError);
        }
        try {
            const auto cur = account_currency(req, account);
            Json::Value arr(Json::arrayValue);
            for (const auto& r : analytics::detect_recurring(*journal, cur)) {
                arr.append(recurring_json(r));
            }
            Json::Value out(Json::objectValue);
            out["account"] = account;
            out["recurring"] = arr;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_forecast(const HttpRequestPtr& req, Callback&& cb) {
        const auto account = req->getParameter("account");
        if (account.empty()) {
            return send_error(cb, "query parameter 'account' is required");
        }
        const auto today = today_ymd();
        std::chrono::year_month_day horizon = end_of_month(today);
        if (auto u = req->getParameter("until"); !u.empty()) {
            const auto d = parse_iso_date(u);
            if (!d.has_value()) {
                return send_error(cb, "'until' must be YYYY-MM-DD");
            }
            horizon = *d;
        } else if (auto m = req->getParameter("months"); !m.empty()) {
            int n = 0;
            try {
                n = std::stoi(m);
            } catch (...) {
                n = 0;
            }
            if (n <= 0) {
                return send_error(cb, "'months' must be a positive integer");
            }
            auto ym = std::chrono::year_month{today.year(), today.month()} +
                      std::chrono::months{n};
            horizon = std::chrono::year_month_day{ym.year() / ym.month() /
                                                  std::chrono::last};
        }
        if (std::chrono::sys_days{horizon} < std::chrono::sys_days{today}) {
            return send_error(cb, "horizon is in the past");
        }
        if (!require_account_owner(req, cb, account)) {
            return;
        }
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::AccountRepository accounts{*db};
            const auto bal = accounts.balance_of(uid_of(req), account);
            if (!bal.has_value()) {
                return send_error(cb, "account '" + account + "' not found",
                                  drogon::k404NotFound);
            }
            const auto cur = currency_or_eur(bal->currency);
            std::string err;
            const auto journal = journal_from_db(account, err);
            if (!journal.has_value()) {
                return send_error(cb, err, drogon::k500InternalServerError);
            }
            const auto recurring =
                analytics::detect_recurring(*journal, cur);

            std::int64_t delta = 0;
            std::vector<std::pair<std::string, Json::Value>> rows;
            for (const auto& r : recurring) {
                auto when = r.next_date;
                // Roll forward to the first occurrence not in the past.
                while (std::chrono::sys_days{when} <
                       std::chrono::sys_days{today}) {
                    when = add_month_ymd(when);
                }
                while (std::chrono::sys_days{when} <=
                       std::chrono::sys_days{horizon}) {
                    delta += r.average_amount.minor_units();
                    Json::Value e(Json::objectValue);
                    const auto iso = format_iso_date(when);
                    e["date"] = iso;
                    e["label"] = r.label;
                    e["amount"] = api::money_json(r.average_amount);
                    rows.emplace_back(iso, std::move(e));
                    when = add_month_ymd(when);
                }
            }
            std::sort(rows.begin(), rows.end(),
                      [](const auto& a, const auto& b) {
                          return a.first < b.first;
                      });
            Json::Value events(Json::arrayValue);
            for (auto& [_, e] : rows) {
                events.append(std::move(e));
            }

            Json::Value out(Json::objectValue);
            out["account"] = account;
            out["as_of"] = format_iso_date(today);
            out["horizon"] = format_iso_date(horizon);
            out["current_balance"] =
                api::money_json(money::Money(bal->current_balance, cur));
            out["projected_balance"] = api::money_json(
                money::Money(bal->current_balance + delta, cur));
            out["expected"] = events;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

} // namespace

int main() {
    using namespace drogon;

    auto& app = drogon::app();

    // ---- documentation -------------------------------------------------------
    app.registerHandler(
        "/",
        [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
            cb(HttpResponse::newRedirectionResponse("/swagger"));
        },
        {Get});
    app.registerHandler(
        "/swagger",
        [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_TEXT_HTML);
            resp->setBody(wealthtorii::api::kSwaggerHtml);
            cb(resp);
        },
        {Get});
    app.registerHandler(
        "/openapi.json",
        [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeString("application/json");
            resp->setBody(wealthtorii::api::kOpenApiJson);
            cb(resp);
        },
        {Get});

    // ---- API -----------------------------------------------------------------
    app.registerHandler("/api/allocate", &h_allocate, {Get});
    app.registerHandler("/api/categories", &h_categories, {Get});
    app.registerHandler("/api/import", &h_import, {Post});
    app.registerHandler("/api/report", &h_report_csv, {Post});
    app.registerHandler("/api/report", &h_report_db, {Get});
    app.registerHandler("/api/budget", &h_budget_get, {Get});
    app.registerHandler("/api/budget", &h_budget_post, {Post});
    app.registerHandler("/api/budget/{category}", &h_budget_get_one, {Get});
    app.registerHandler("/api/budget/{category}", &h_budget_delete, {Delete});
    app.registerHandler("/api/rules", &h_rules_get_dispatch, {Get});
    app.registerHandler("/api/rules", &h_rules_post, {Post});
    app.registerHandler("/api/rules", &h_rules_put, {Put});
    app.registerHandler("/api/rules", &h_rules_delete, {Delete});
    app.registerHandler("/api/sync", &h_sync, {Post});
    app.registerHandler("/api/suggest", &h_suggest_csv, {Post});
    app.registerHandler("/api/suggest", &h_suggest_db, {Get});
    app.registerHandler("/api/export", &h_export_csv, {Post});
    app.registerHandler("/api/export", &h_export_db, {Get});

    // ---- CRUD: accounts ------------------------------------------------------
    app.registerHandler("/api/accounts", &h_accounts_get_all, {Get});
    app.registerHandler("/api/accounts", &h_accounts_post, {Post});
    app.registerHandler("/api/accounts/{id}", &h_accounts_get_one, {Get});
    app.registerHandler("/api/accounts/{id}", &h_accounts_put, {Put});
    app.registerHandler("/api/accounts/{id}", &h_accounts_delete, {Delete});
    app.registerHandler("/api/accounts/{id}/balance", &h_account_balance, {Get});

    // ---- net worth & trends --------------------------------------------------
    app.registerHandler("/api/networth", &h_networth, {Get});
    app.registerHandler("/api/trends", &h_trends, {Get});

    // ---- savings goals -------------------------------------------------------
    app.registerHandler("/api/goals", &h_goals_get_all, {Get});
    app.registerHandler("/api/goals", &h_goals_post, {Post});
    app.registerHandler("/api/goals/{id}", &h_goals_get_one, {Get});
    app.registerHandler("/api/goals/{id}", &h_goals_put, {Put});
    app.registerHandler("/api/goals/{id}", &h_goals_delete, {Delete});
    app.registerHandler("/api/goals/{id}/contributions", &h_goal_contrib_post,
                        {Post});
    app.registerHandler("/api/goals/{id}/contributions", &h_goal_contrib_get,
                        {Get});

    // ---- recurring & forecast ------------------------------------------------
    app.registerHandler("/api/recurring", &h_recurring, {Get});
    app.registerHandler("/api/forecast", &h_forecast, {Get});

    // ---- CRUD: transactions --------------------------------------------------
    app.registerHandler("/api/transactions", &h_tx_get_all, {Get});
    app.registerHandler("/api/transactions", &h_tx_post, {Post});
    app.registerHandler("/api/transactions/{id}", &h_tx_get_one, {Get});
    app.registerHandler("/api/transactions/{id}", &h_tx_put, {Put});
    app.registerHandler("/api/transactions/{id}", &h_tx_delete, {Delete});

    // ---- auth ----------------------------------------------------------------
    app.registerHandler("/api/auth/register", &h_register, {Post});
    app.registerHandler("/api/auth/login", &h_login, {Post});
    app.registerHandler("/api/auth/me", &h_me, {Get});

    // Gate every /api/* route (except register/login): require a valid Bearer
    // token, enforce the premium plan on paid endpoints, and stamp the user id
    // on the request for the handlers' tenant scoping.
    app.registerPreHandlingAdvice(
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& stop,
           std::function<void()>&& next) {
            const std::string path = req->path();
            if (is_public_path(path) || path.rfind("/api/", 0) != 0) {
                next();
                return;
            }
            auto deny = [&](const char* msg, HttpStatusCode code) {
                auto resp = HttpResponse::newHttpJsonResponse(
                    wealthtorii::api::error_json(msg));
                resp->setStatusCode(code);
                stop(resp);
            };
            const auto tok = wealthtorii::api::auth::bearer(
                req->getHeader("Authorization"));
            if (!tok.has_value()) {
                return deny("missing or malformed Authorization: Bearer <token>",
                            k401Unauthorized);
            }
            const auto claims = wealthtorii::api::auth::verify_token(*tok);
            if (!claims.has_value()) {
                return deny("invalid or expired token", k401Unauthorized);
            }
            if (is_premium_path(path) && claims->plan != "premium") {
                return deny("this feature requires a premium plan",
                            k402PaymentRequired);
            }
            req->getAttributes()->insert("uid", claims->user_id);
            req->getAttributes()->insert("plan", claims->plan);
            req->getAttributes()->insert("email", claims->email);
            next();
        });

    if (!wealthtorii::api::auth::ensure_sodium()) {
        LOG_ERROR << "libsodium initialisation failed";
        return 1;
    }
    if (const auto url =
            wealthtorii::storage::Connection::database_url_from_env();
        url.has_value()) {
        try {
            wealthtorii::storage::Connection conn{*url};
            wealthtorii::storage::apply_default_migrations(conn);
            LOG_INFO << "database migrations applied";
        } catch (const std::exception& e) {
            LOG_WARN << "could not apply migrations at startup: " << e.what();
        }
    } else {
        LOG_WARN << "$DATABASE_URL not set — auth and persistence endpoints "
                    "will return 500";
    }

    const std::uint16_t port = 8080;
    LOG_INFO << "WealthTorii API on http://127.0.0.1:" << port
             << "  (Swagger: http://127.0.0.1:" << port << "/swagger)";
    app.addListener("0.0.0.0", port).setThreadNum(1).run();
    return 0;
}
