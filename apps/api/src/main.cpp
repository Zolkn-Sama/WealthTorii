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

    // Builds the layered categorizer used by the CLI: user rules first, defaults appended.
    import_::Categorizer make_categorizer() {
        const auto user_rules = cli::load_rules_config(cli::default_rules_config_path());
        const auto base = import_::default_overrides();
        return cli::build_categorizer(user_rules, base);
    }

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
        std::istringstream in{up.content};
        import_::ImportOptions opts;
        opts.account_id = up.account;
        const auto overrides = make_categorizer();
        opts.overrides = &overrides;
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
        const auto cfg = cli::load_budget_config(cli::default_budget_config_path());
        const auto overrides = make_categorizer();
        try {
            std::istringstream in{up.content};
            const auto journal =
                journal_from_csv(in, up.account, cfg.currency, overrides);
            if (journal.empty()) {
                return send_error(cb, "no transactions parsed from CSV");
            }
            send_json(cb, build_report_json(journal, up.account, month, cfg));
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
        std::string err;
        auto journal = journal_from_db(account, err);
        if (!journal.has_value()) {
            return send_error(cb, err, drogon::k500InternalServerError);
        }
        if (journal->empty()) {
            return send_error(cb, "no transactions in DB for account " + account);
        }
        const auto cfg = cli::load_budget_config(cli::default_budget_config_path());
        send_json(cb, build_report_json(*journal, account, month, cfg));
    }

    void h_budget_get(const HttpRequestPtr&, Callback&& cb) {
        const auto cfg = cli::load_budget_config(cli::default_budget_config_path());
        Json::Value out(Json::objectValue);
        out["currency"] = std::string(money::to_string(cfg.currency));
        Json::Value limits(Json::arrayValue);
        auto total = money::Money::zero(cfg.currency);
        for (const auto& [cat, limit] : cfg.limits) {
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
        auto cfg = cli::load_budget_config(cli::default_budget_config_path());
        if (body->isMember("currency")) {
            const auto parsed = money::currency_from_string((*body)["currency"].asString());
            if (!parsed.has_value()) {
                return send_error(cb, "unknown currency");
            }
            cfg.currency = *parsed;
        }
        const auto amount = money::Money::from_string(amount_s, cfg.currency);
        if (!amount.has_value()) {
            return send_error(cb, "'" + amount_s + "' is not a valid amount");
        }
        if (amount->is_negative()) {
            return send_error(cb, "limit must be non-negative");
        }
        cfg.limits[category] = *amount;
        try {
            cli::save_budget_config(cli::default_budget_config_path(), cfg);
        } catch (const std::exception& e) {
            return send_error(cb, e.what(), drogon::k500InternalServerError);
        }
        h_budget_get(req, std::move(cb));
    }

    void h_rules_get(const HttpRequestPtr&, Callback&& cb) {
        const auto cfg = cli::load_rules_config(cli::default_rules_config_path());
        Json::Value rules(Json::arrayValue);
        for (const auto& r : cfg.rules) {
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
        auto cfg = cli::load_rules_config(cli::default_rules_config_path());
        bool updated = false;
        for (auto& r : cfg.rules) {
            if (r.pattern == pattern) {
                r.category_id = category;
                r.bp_subcategory = bp_sub;
                updated = true;
                break;
            }
        }
        if (!updated) {
            cfg.rules.push_back(cli::Rule{pattern, category, bp_sub});
        }
        try {
            cli::save_rules_config(cli::default_rules_config_path(), cfg);
        } catch (const std::exception& e) {
            return send_error(cb, e.what(), drogon::k500InternalServerError);
        }
        h_rules_get(req, std::move(cb));
    }

    void h_rules_delete(const HttpRequestPtr& req, Callback&& cb) {
        const auto pattern = req->getParameter("pattern");
        if (pattern.empty()) {
            return send_error(cb, "query parameter 'pattern' is required");
        }
        auto cfg = cli::load_rules_config(cli::default_rules_config_path());
        const auto before = cfg.rules.size();
        cfg.rules.erase(std::remove_if(cfg.rules.begin(), cfg.rules.end(),
                                       [&](const cli::Rule& r) {
                                           return r.pattern == pattern;
                                       }),
                        cfg.rules.end());
        if (cfg.rules.size() == before) {
            return send_error(cb, "no rule matched '" + pattern + "'",
                              drogon::k404NotFound);
        }
        try {
            cli::save_rules_config(cli::default_rules_config_path(), cfg);
        } catch (const std::exception& e) {
            return send_error(cb, e.what(), drogon::k500InternalServerError);
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
        const auto cfg = cli::load_budget_config(cli::default_budget_config_path());
        const auto overrides = make_categorizer();
        try {
            std::istringstream in{up.content};
            import_::ImportOptions opts;
            opts.account_id = up.account;
            opts.currency = cfg.currency;
            opts.overrides = &overrides;
            const auto imp = import_::import_bp_csv(in, opts);

            storage::Connection conn{*url};
            storage::apply_default_migrations(conn);
            storage::AccountRepository accounts{conn};
            accounts.ensure(ledger::Account{up.account, up.account, cfg.currency,
                                            ledger::AccountType::CASH});
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
        const auto cfg = cli::load_budget_config(cli::default_budget_config_path());
        const auto overrides = make_categorizer();
        try {
            std::istringstream in{up.content};
            const auto journal =
                journal_from_csv(in, up.account, cfg.currency, overrides);
            if (journal.empty()) {
                return send_error(cb, "no transactions parsed from CSV");
            }
            send_json(cb, suggestions_json(journal, ending, months, cfg.currency));
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
        std::string err;
        auto journal = journal_from_db(account, err);
        if (!journal.has_value()) {
            return send_error(cb, err, drogon::k500InternalServerError);
        }
        if (journal->empty()) {
            return send_error(cb, "no transactions in DB for account " + account);
        }
        const auto cfg = cli::load_budget_config(cli::default_budget_config_path());
        send_json(cb, suggestions_json(*journal, ending, months, cfg.currency));
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
        const auto cfg = cli::load_budget_config(cli::default_budget_config_path());
        const auto overrides = make_categorizer();
        try {
            std::istringstream in{up.content};
            const auto journal =
                journal_from_csv(in, up.account, cfg.currency, overrides);
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

    void h_accounts_get_all(const HttpRequestPtr&, Callback&& cb) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::AccountRepository repo{*db};
            Json::Value arr(Json::arrayValue);
            for (const auto& a : repo.all()) {
                arr.append(account_json(a));
            }
            Json::Value out(Json::objectValue);
            out["accounts"] = arr;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_accounts_get_one(const HttpRequestPtr&, Callback&& cb, std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::AccountRepository repo{*db};
            const auto a = repo.find(id);
            if (!a.has_value()) {
                return send_error(cb, "account '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            send_json(cb, account_json(*a));
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
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
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::AccountRepository repo{*db};
            if (!repo.ensure(*account)) {
                return send_error(cb, "account '" + id + "' already exists",
                                  drogon::k409Conflict);
            }
            send_json(cb, account_json(*account), drogon::k201Created);
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
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::AccountRepository repo{*db};
            if (!repo.update(*account)) {
                return send_error(cb, "account '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            send_json(cb, account_json(*account));
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_accounts_delete(const HttpRequestPtr&, Callback&& cb, std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::AccountRepository repo{*db};
            if (!repo.remove(id)) {
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

    void h_tx_get_one(const HttpRequestPtr&, Callback&& cb, std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            const storage::TransactionRepository repo{*db};
            const auto t = repo.find(id);
            if (!t.has_value()) {
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
            storage::TransactionRepository repo{*db};
            if (!repo.find(id).has_value()) {
                return send_error(cb, "transaction '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            const ledger::Transaction one[] = {*t};
            repo.upsert(one);
            send_json(cb, transaction_json(*t));
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    void h_tx_delete(const HttpRequestPtr&, Callback&& cb, std::string id) {
        auto db = open_db(cb);
        if (!db) {
            return;
        }
        try {
            storage::TransactionRepository repo{*db};
            if (!repo.remove(id)) {
                return send_error(cb, "transaction '" + id + "' not found",
                                  drogon::k404NotFound);
            }
            Json::Value out(Json::objectValue);
            out["deleted"] = id;
            send_json(cb, out);
        } catch (const std::exception& e) {
            send_error(cb, e.what(), drogon::k500InternalServerError);
        }
    }

    // ---- budget: getById + delete (file-backed) -----------------------------

    void h_budget_get_one(const HttpRequestPtr&, Callback&& cb, std::string category) {
        const auto cfg = cli::load_budget_config(cli::default_budget_config_path());
        const auto it = cfg.limits.find(category);
        if (it == cfg.limits.end()) {
            return send_error(cb, "no limit set for category '" + category + "'",
                              drogon::k404NotFound);
        }
        Json::Value out(Json::objectValue);
        out["category"] = category;
        out["limit"] = api::money_json(it->second);
        send_json(cb, out);
    }

    void h_budget_delete(const HttpRequestPtr& req, Callback&& cb, std::string category) {
        auto cfg = cli::load_budget_config(cli::default_budget_config_path());
        if (cfg.limits.erase(category) == 0) {
            return send_error(cb, "no limit set for category '" + category + "'",
                              drogon::k404NotFound);
        }
        try {
            cli::save_budget_config(cli::default_budget_config_path(), cfg);
        } catch (const std::exception& e) {
            return send_error(cb, e.what(), drogon::k500InternalServerError);
        }
        h_budget_get(req, std::move(cb));
    }

    // ---- rules: getByString + update ----------------------------------------

    void h_rules_get_dispatch(const HttpRequestPtr& req, Callback&& cb) {
        const auto pattern = req->getParameter("pattern");
        if (pattern.empty()) {
            return h_rules_get(req, std::move(cb));
        }
        const auto cfg = cli::load_rules_config(cli::default_rules_config_path());
        for (const auto& r : cfg.rules) {
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
        auto cfg = cli::load_rules_config(cli::default_rules_config_path());
        bool found = false;
        for (auto& r : cfg.rules) {
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
        try {
            cli::save_rules_config(cli::default_rules_config_path(), cfg);
        } catch (const std::exception& e) {
            return send_error(cb, e.what(), drogon::k500InternalServerError);
        }
        h_rules_get(req, std::move(cb));
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

    // ---- CRUD: transactions --------------------------------------------------
    app.registerHandler("/api/transactions", &h_tx_get_all, {Get});
    app.registerHandler("/api/transactions", &h_tx_post, {Post});
    app.registerHandler("/api/transactions/{id}", &h_tx_get_one, {Get});
    app.registerHandler("/api/transactions/{id}", &h_tx_put, {Put});
    app.registerHandler("/api/transactions/{id}", &h_tx_delete, {Delete});

    const std::uint16_t port = 8080;
    LOG_INFO << "WealthTorii API on http://127.0.0.1:" << port
             << "  (Swagger: http://127.0.0.1:" << port << "/swagger)";
    app.addListener("0.0.0.0", port).setThreadNum(1).run();
    return 0;
}
