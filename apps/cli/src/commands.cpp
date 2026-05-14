#include "commands.hpp"

#include <wealthtorii/budget/allocation.hpp>
#include <wealthtorii/budget/budget.hpp>
#include <wealthtorii/budget/category.hpp>
#include <wealthtorii/import/bp_csv.hpp>
#include <wealthtorii/ledger/account.hpp>
#include <wealthtorii/ledger/journal.hpp>
#include <wealthtorii/money/money.hpp>
#include <wealthtorii/analytics/analytics.hpp>
#include <wealthtorii/storage/connection.hpp>
#include <wealthtorii/storage/migrations.hpp>
#include <wealthtorii/storage/repository.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace wealthtorii::cli {

    namespace {

        std::string format_pct(std::int64_t numerator, std::int64_t denominator) {
            if (denominator == 0) return "—";
            const double pct = 100.0 * static_cast<double>(numerator) / static_cast<double>(denominator);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << pct << '%';
            return oss.str();
        }

        std::string pad_right(std::string s, std::size_t width) {
            if (s.size() < width) s.append(width - s.size(), ' ');
            return s;
        }

        std::string pad_left(std::string s, std::size_t width) {
            if (s.size() < width) s.insert(0, width - s.size(), ' ');
            return s;
        }

        std::optional<std::string_view> get_option(Args args, std::string_view name) {
            for (std::size_t i = 0; i + 1 < args.size(); ++i) {
                if (args[i] == name) {
                    return args[i + 1];
                }
            }
            return std::nullopt;
        }

        std::vector<std::string_view> positional(Args args) {
            std::vector<std::string_view> out;
            for (std::size_t i = 0; i < args.size(); ++i) {
                const auto& a = args[i];
                if (!a.empty() && a.front() == '-') {
                    ++i; // skip value
                    continue;
                }
                out.push_back(a);
            }
            return out;
        }

        // Parses "YYYY-MM".
        std::optional<std::chrono::year_month> parse_year_month(std::string_view s) noexcept {
            if (s.size() != 7 || s[4] != '-') return std::nullopt;
            int y = 0;
            int m = 0;
            if (std::from_chars(s.data(), s.data() + 4, y).ec != std::errc{}) return std::nullopt;
            if (std::from_chars(s.data() + 5, s.data() + 7, m).ec != std::errc{}) return std::nullopt;
            const std::chrono::year_month ym{std::chrono::year{y},
                                              std::chrono::month{static_cast<unsigned>(m)}};
            if (!ym.ok()) return std::nullopt;
            return ym;
        }

        std::string format_year_month(std::chrono::year_month ym) {
            std::ostringstream oss;
            oss << static_cast<int>(ym.year()) << '-' << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(ym.month());
            return oss.str();
        }

        std::string format_money(const money::Money& m) {
            return money::to_string(m);
        }

    } // namespace

    int cmd_help(Args /*args*/, std::ostream& out, std::ostream& /*err*/) {
        out << "wt — WealthTorii personal finance CLI\n\n"
            << "usage: wt <command> [options]\n\n"
            << "commands:\n"
            << "  allocate <income>                 Show 50/30/20 split for the given income.\n"
            << "  categories                        List the default category registry.\n"
            << "  import <csv> --account <id>       Parse a Banque Populaire CSV and summarise.\n"
            << "  report <csv> --account <id>       Show monthly inflow/outflow/breakdown,\n"
            << "         [--month YYYY-MM]          compared against the saved budget config.\n"
            << "  budget show                       Print the saved budget config.\n"
            << "  budget set <category> <amount>    Set a monthly limit for a category.\n"
            << "  rules show                        List user categorisation rules.\n"
            << "  rules add <pattern> <wt_category> [<bp_subcategory>]\n"
            << "                                    Add a regex rule (case-insensitive).\n"
            << "                                    bp_subcategory overrides the value emitted on export.\n"
            << "  rules remove <pattern>            Remove a rule by exact pattern.\n"
            << "  sync <csv> --account <id>         Import a CSV and persist to Postgres.\n"
            << "  report --from-db --account <id>   Read transactions from Postgres instead of CSV.\n"
            << "         [--month YYYY-MM]\n"
            << "  suggest <csv> --account <id>      Suggest per-category monthly budgets based on\n"
            << "         [--months N]               the trailing N months (default 3).\n"
            << "         [--ending YYYY-MM]         Also supports --from-db.\n"
            << "  export <out.csv> --account <id>   Emit an 11-column CSV in the SORTED_DATA.xlsx\n"
            << "         [--from-db | <in.csv>]     layout (Date comptabilisation … Pointage op).\n"
            << "  help                              Show this message.\n\n"
            << "Postgres is read from $DATABASE_URL.\n"
            << "amounts: use either '12,34' (FR) or '12.34' (EN). Default currency is EUR.\n";
        return 0;
    }

    int cmd_allocate(Args args, std::ostream& out, std::ostream& err) {
        const auto pos = positional(args);
        if (pos.empty()) {
            err << "allocate: expected an income amount, e.g. `wt allocate 1800`\n";
            return 2;
        }
        const auto income = money::Money::from_string(pos[0]);
        if (!income.has_value()) {
            err << "allocate: '" << pos[0] << "' is not a valid amount\n";
            return 2;
        }
        if (income->is_zero() || income->is_negative()) {
            err << "allocate: income must be strictly positive\n";
            return 2;
        }
        const auto alloc = budget::allocate_50_30_20(*income);

        out << "50/30/20 split of " << format_money(*income) << "\n";
        out << "  NEEDS          (50%) : " << format_money(alloc.at(budget::Bucket::NEEDS)) << "\n";
        out << "  WANTS          (30%) : " << format_money(alloc.at(budget::Bucket::WANTS)) << "\n";
        out << "  SAVINGS/INVEST (20%) : " << format_money(alloc.at(budget::Bucket::SAVINGS_INVEST))
            << "\n";
        return 0;
    }

    int cmd_categories(Args /*args*/, std::ostream& out, std::ostream& /*err*/) {
        const auto reg = budget::default_registry();
        std::map<budget::Bucket, std::vector<const budget::Category*>> by_bucket;
        for (const auto& c : reg.all()) {
            by_bucket[c.bucket()].push_back(&c);
        }
        out << "WealthTorii default categories (id — name):\n";
        for (const auto bucket : {budget::Bucket::NEEDS, budget::Bucket::WANTS,
                                   budget::Bucket::SAVINGS_INVEST, budget::Bucket::INCOME}) {
            out << "\n[" << budget::to_string(bucket) << "]\n";
            for (const auto* c : by_bucket[bucket]) {
                out << "  " << pad_right(c->id(), 26) << " " << c->name() << '\n';
            }
        }
        return 0;
    }

    int cmd_import(Args args, std::ostream& out, std::ostream& err, const Environment& env) {
        const auto pos = positional(args);
        if (pos.empty()) {
            err << "import: expected a CSV path\n";
            return 2;
        }
        const auto account = get_option(args, "--account");
        if (!account.has_value()) {
            err << "import: --account <id> is required\n";
            return 2;
        }
        std::ifstream in{std::string(pos[0])};
        if (!in) {
            err << "import: cannot open " << pos[0] << '\n';
            return 1;
        }

        import_::ImportOptions opts;
        opts.account_id = std::string(*account);
        const auto user_rules = load_rules_config(env.rules_config_path);
        const auto base_overrides = import_::default_overrides();
        const auto overrides = build_categorizer(user_rules, base_overrides);
        opts.overrides = &overrides;

        const auto report = import_::import_bp_csv(in, opts);
        out << "rows seen     : " << report.rows_seen << '\n'
            << "rows dropped  : " << report.rows_dropped << '\n'
            << "transactions  : " << report.transactions.size() << '\n'
            << "categorised   : " << report.categorised << " ("
            << format_pct(static_cast<std::int64_t>(report.categorised),
                          static_cast<std::int64_t>(report.transactions.size())) << ")\n"
            << "uncategorised : " << report.uncategorised << '\n';
        return 0;
    }

    namespace {
        struct ReportData {
            std::chrono::year_month month;
            money::Money inflow;
            money::Money outflow;
            money::Money net;
            std::map<std::string, money::Money> outflow_by_category;
            std::map<budget::Bucket, money::Money> outflow_by_bucket;
        };

        bool is_transfer(const budget::CategoryRegistry& registry,
                         const std::optional<std::string>& category_id) noexcept {
            if (!category_id.has_value()) return false;
            const auto* c = registry.find(*category_id);
            return c != nullptr && c->bucket() == budget::Bucket::TRANSFERS;
        }

        ReportData compute_report(const ledger::Journal& journal,
                                  std::chrono::year_month month,
                                  money::Currency currency,
                                  const budget::CategoryRegistry& registry) {
            ReportData r{month,
                         money::Money::zero(currency),
                         money::Money::zero(currency),
                         money::Money::zero(currency),
                         {},
                         {}};
            for (const auto& tx : journal.transactions()) {
                if (tx.date().year() != month.year() || tx.date().month() != month.month()) {
                    continue;
                }
                if (is_transfer(registry, tx.category_id())) {
                    continue; // neutral movement, doesn't count as inflow/outflow
                }
                if (tx.is_inflow()) {
                    r.inflow += tx.amount();
                    r.net += tx.amount();
                } else {
                    const auto positive = -tx.amount();
                    r.outflow += positive;
                    r.net += tx.amount();
                    const auto key = tx.category_id().value_or("");
                    auto [it, _] = r.outflow_by_category.try_emplace(key,
                                                                      money::Money::zero(currency));
                    it->second += positive;
                    if (const auto* c = registry.find(key); c != nullptr &&
                        c->bucket() != budget::Bucket::INCOME &&
                        c->bucket() != budget::Bucket::TRANSFERS) {
                        auto [bit, __] = r.outflow_by_bucket.try_emplace(c->bucket(),
                                                                          money::Money::zero(currency));
                        bit->second += positive;
                    }
                }
            }
            return r;
        }
    } // namespace

    int cmd_report(Args args, std::ostream& out, std::ostream& err, const Environment& env) {
        const auto account = get_option(args, "--account");
        if (!account.has_value()) {
            err << "report: --account <id> is required\n";
            return 2;
        }
        const auto cfg = load_budget_config(env.budget_config_path);

        const bool from_db = std::find(args.begin(), args.end(), std::string_view{"--from-db"}) !=
                              args.end();

        ledger::Journal journal;
        if (from_db) {
            const auto url = storage::Connection::database_url_from_env();
            if (!url.has_value()) {
                err << "report: --from-db requires $DATABASE_URL\n";
                return 1;
            }
            storage::Connection conn{*url};
            const storage::TransactionRepository repo{conn};
            journal = repo.load_journal(*account);
            if (journal.empty()) {
                err << "report: no transactions in DB for account " << *account << '\n';
                return 1;
            }
        } else {
            const auto pos = positional(args);
            if (pos.empty()) {
                err << "report: expected a CSV path (or pass --from-db)\n";
                return 2;
            }
            std::ifstream in{std::string(pos[0])};
            if (!in) {
                err << "report: cannot open " << pos[0] << '\n';
                return 1;
            }
            import_::ImportOptions opts;
            opts.account_id = std::string(*account);
            opts.currency = cfg.currency;
            const auto user_rules = load_rules_config(env.rules_config_path);
            const auto base_overrides = import_::default_overrides();
            const auto overrides = build_categorizer(user_rules, base_overrides);
            opts.overrides = &overrides;
            const auto imp = import_::import_bp_csv(in, opts);
            for (const auto& tx : imp.transactions) journal.add(tx);
        }

        std::chrono::year_month month{};
        if (const auto m_opt = get_option(args, "--month"); m_opt.has_value()) {
            const auto parsed = parse_year_month(*m_opt);
            if (!parsed.has_value()) {
                err << "report: --month must be YYYY-MM, got '" << *m_opt << "'\n";
                return 2;
            }
            month = *parsed;
        } else {
            // Default to the latest month seen in the data.
            for (const auto& tx : journal.transactions()) {
                const std::chrono::year_month ym{tx.date().year(), tx.date().month()};
                if (ym > month) month = ym;
            }
            if (!month.ok()) {
                err << "report: no transactions in input\n";
                return 1;
            }
        }

        const auto registry = budget::default_registry();
        const auto data = compute_report(journal, month, cfg.currency, registry);

        out << "Report for " << format_year_month(data.month) << " — account "
            << *account << '\n';
        out << "  inflow  : " << format_money(data.inflow) << '\n';
        out << "  outflow : " << format_money(data.outflow) << '\n';
        out << "  net     : " << format_money(data.net) << '\n';

        out << "\nBy bucket (outflow):\n";
        for (const auto bucket : {budget::Bucket::NEEDS, budget::Bucket::WANTS,
                                   budget::Bucket::SAVINGS_INVEST}) {
            const auto it = data.outflow_by_bucket.find(bucket);
            const money::Money amt =
                it != data.outflow_by_bucket.end() ? it->second : money::Money::zero(cfg.currency);
            out << "  " << pad_right(std::string(budget::to_string(bucket)), 16) << " "
                << pad_left(format_money(amt), 14) << "  "
                << format_pct(amt.minor_units(), data.outflow.minor_units()) << '\n';
        }

        out << "\nBy category (outflow):\n";
        std::vector<std::pair<std::string, money::Money>> rows(data.outflow_by_category.begin(),
                                                                data.outflow_by_category.end());
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.second.minor_units() > b.second.minor_units(); });
        for (const auto& [cat, amt] : rows) {
            const auto* c = registry.find(cat);
            const std::string label = cat.empty() ? "(uncategorised)"
                                                  : (c != nullptr ? c->id() : cat);
            out << "  " << pad_right(label, 26) << " "
                << pad_left(format_money(amt), 14) << "  "
                << format_pct(amt.minor_units(), data.outflow.minor_units()) << '\n';
        }

        if (!cfg.limits.empty()) {
            budget::Budget b{data.month, cfg.currency};
            for (const auto& [cat, limit] : cfg.limits) {
                b.set_limit(cat, limit);
            }
            const auto lines = budget::spending_vs_budget(b, journal);
            out << "\nBudget vs spending:\n";
            for (const auto& line : lines) {
                const std::string label = line.category_id.empty() ? "(uncategorised)" : line.category_id;
                const char tag = line.delta.is_negative() ? '!' : ' ';
                out << "  " << tag << ' ' << pad_right(label, 24)
                    << " spent " << pad_left(format_money(line.spent), 12)
                    << " / budget " << pad_left(format_money(line.budgeted), 12)
                    << " → " << format_money(line.delta) << '\n';
            }
        } else {
            out << "\n(no budget configured — run `wt budget set <category> <amount>`)\n";
        }
        return 0;
    }

    int cmd_budget(Args args, std::ostream& out, std::ostream& err, const Environment& env) {
        const auto pos = positional(args);
        if (pos.empty()) {
            err << "budget: expected `show` or `set`\n";
            return 2;
        }
        const auto sub = pos[0];

        if (sub == "show") {
            const auto cfg = load_budget_config(env.budget_config_path);
            out << "config file : " << env.budget_config_path << '\n';
            out << "currency    : " << money::to_string(cfg.currency) << '\n';
            if (cfg.limits.empty()) {
                out << "(no limits set)\n";
                return 0;
            }
            money::Money total = money::Money::zero(cfg.currency);
            for (const auto& [cat, limit] : cfg.limits) {
                out << "  " << pad_right(cat, 26) << " " << format_money(limit) << '\n';
                total += limit;
            }
            out << "  " << pad_right("TOTAL", 26) << " " << format_money(total) << '\n';
            return 0;
        }

        if (sub == "set") {
            if (pos.size() < 3) {
                err << "budget set: expected <category> <amount>\n";
                return 2;
            }
            const auto cat = std::string(pos[1]);
            auto cfg = load_budget_config(env.budget_config_path);
            const auto parsed = money::Money::from_string(pos[2], cfg.currency);
            if (!parsed.has_value()) {
                err << "budget set: '" << pos[2] << "' is not a valid amount\n";
                return 2;
            }
            if (parsed->is_negative()) {
                err << "budget set: limit must be non-negative\n";
                return 2;
            }
            cfg.limits[cat] = *parsed;
            save_budget_config(env.budget_config_path, cfg);
            out << "set " << cat << " = " << format_money(*parsed) << '\n';
            return 0;
        }

        err << "budget: unknown subcommand '" << sub << "'\n";
        return 2;
    }

    int cmd_rules(Args args, std::ostream& out, std::ostream& err, const Environment& env) {
        const auto pos = positional(args);
        if (pos.empty()) {
            err << "rules: expected `show`, `add` or `remove`\n";
            return 2;
        }
        const auto sub = pos[0];

        if (sub == "show") {
            const auto cfg = load_rules_config(env.rules_config_path);
            out << "config file : " << env.rules_config_path << '\n';
            if (cfg.rules.empty()) {
                out << "(no user rules — only built-in overrides are active)\n";
                return 0;
            }
            for (std::size_t i = 0; i < cfg.rules.size(); ++i) {
                out << "  [" << pad_left(std::to_string(i + 1), 2) << "] "
                    << pad_right(cfg.rules[i].pattern, 40) << " => "
                    << cfg.rules[i].category_id;
                if (cfg.rules[i].bp_subcategory.has_value()) {
                    out << " / " << *cfg.rules[i].bp_subcategory;
                }
                out << '\n';
            }
            return 0;
        }

        if (sub == "add") {
            if (pos.size() < 3) {
                err << "rules add: expected <pattern> <wt_category> [<bp_subcategory>]\n";
                return 2;
            }
            const auto pattern = std::string(pos[1]);
            const auto category = std::string(pos[2]);
            std::optional<std::string> bp_sub;
            if (pos.size() >= 4) {
                bp_sub = std::string(pos[3]);
            }
            try {
                static_cast<void>(std::regex(pattern, std::regex::ECMAScript | std::regex::icase));
            } catch (const std::regex_error& e) {
                err << "rules add: invalid regex '" << pattern << "': " << e.what() << '\n';
                return 2;
            }
            auto cfg = load_rules_config(env.rules_config_path);
            for (auto& r : cfg.rules) {
                if (r.pattern == pattern) {
                    r.category_id = category;
                    r.bp_subcategory = bp_sub;
                    save_rules_config(env.rules_config_path, cfg);
                    out << "updated rule: " << pattern << " => " << category;
                    if (bp_sub.has_value()) out << " / " << *bp_sub;
                    out << '\n';
                    return 0;
                }
            }
            cfg.rules.push_back({pattern, category, bp_sub});
            save_rules_config(env.rules_config_path, cfg);
            out << "added rule: " << pattern << " => " << category;
            if (bp_sub.has_value()) out << " / " << *bp_sub;
            out << '\n';
            return 0;
        }

        if (sub == "remove") {
            if (pos.size() < 2) {
                err << "rules remove: expected <pattern>\n";
                return 2;
            }
            const auto pattern = std::string(pos[1]);
            auto cfg = load_rules_config(env.rules_config_path);
            const auto before = cfg.rules.size();
            cfg.rules.erase(
                std::remove_if(cfg.rules.begin(), cfg.rules.end(),
                               [&](const Rule& r) { return r.pattern == pattern; }),
                cfg.rules.end());
            if (cfg.rules.size() == before) {
                err << "rules remove: no rule matched '" << pattern << "'\n";
                return 1;
            }
            save_rules_config(env.rules_config_path, cfg);
            out << "removed rule: " << pattern << '\n';
            return 0;
        }

        err << "rules: unknown subcommand '" << sub << "'\n";
        return 2;
    }

    int cmd_sync(Args args, std::ostream& out, std::ostream& err, const Environment& env) {
        const auto pos = positional(args);
        if (pos.empty()) {
            err << "sync: expected a CSV path\n";
            return 2;
        }
        const auto account = get_option(args, "--account");
        if (!account.has_value()) {
            err << "sync: --account <id> is required\n";
            return 2;
        }
        const auto url = storage::Connection::database_url_from_env();
        if (!url.has_value()) {
            err << "sync: $DATABASE_URL is required (see infra/docker-compose.yml)\n";
            return 1;
        }
        std::ifstream in{std::string(pos[0])};
        if (!in) {
            err << "sync: cannot open " << pos[0] << '\n';
            return 1;
        }

        const auto cfg = load_budget_config(env.budget_config_path);
        import_::ImportOptions opts;
        opts.account_id = std::string(*account);
        opts.currency = cfg.currency;
        const auto user_rules = load_rules_config(env.rules_config_path);
        const auto base_overrides = import_::default_overrides();
        const auto overrides = build_categorizer(user_rules, base_overrides);
        opts.overrides = &overrides;
        const auto imp = import_::import_bp_csv(in, opts);

        storage::Connection conn{*url};
        storage::apply_default_migrations(conn);

        storage::AccountRepository accounts{conn};
        accounts.ensure(ledger::Account{std::string(*account),
                                         std::string(*account),
                                         cfg.currency,
                                         ledger::AccountType::CASH});

        storage::TransactionRepository txs{conn};
        const auto stats = txs.upsert(imp.transactions);

        out << "sync " << *account << ": imported " << imp.transactions.size()
            << " transactions (" << imp.rows_dropped << " dropped from CSV)\n"
            << "  inserted : " << stats.inserted << '\n'
            << "  updated  : " << stats.updated << '\n'
            << "  unchanged: " << (imp.transactions.size() - stats.inserted - stats.updated) << '\n';
        return 0;
    }

    namespace {
        // Loads a journal either from --from-db or from a positional CSV path. Returns nullopt
        // and writes to err on failure.
        std::optional<ledger::Journal> load_journal_from_args(Args args, std::string_view account,
                                                                const Environment& env,
                                                                std::ostream& err) {
            const bool from_db = std::find(args.begin(), args.end(),
                                            std::string_view{"--from-db"}) != args.end();
            ledger::Journal journal;
            if (from_db) {
                const auto url = storage::Connection::database_url_from_env();
                if (!url.has_value()) {
                    err << "$DATABASE_URL is required for --from-db\n";
                    return std::nullopt;
                }
                storage::Connection conn{*url};
                const storage::TransactionRepository repo{conn};
                journal = repo.load_journal(account);
                return journal;
            }
            const auto pos = positional(args);
            if (pos.empty()) {
                err << "expected a CSV path (or pass --from-db)\n";
                return std::nullopt;
            }
            std::ifstream in{std::string(pos[0])};
            if (!in) {
                err << "cannot open " << pos[0] << '\n';
                return std::nullopt;
            }
            import_::ImportOptions opts;
            opts.account_id = std::string(account);
            const auto user_rules = load_rules_config(env.rules_config_path);
            const auto base_overrides = import_::default_overrides();
            const auto overrides = build_categorizer(user_rules, base_overrides);
            opts.overrides = &overrides;
            const auto imp = import_::import_bp_csv(in, opts);
            for (const auto& tx : imp.transactions) journal.add(tx);
            return journal;
        }
    } // namespace

    int cmd_suggest(Args args, std::ostream& out, std::ostream& err, const Environment& env) {
        const auto account = get_option(args, "--account");
        if (!account.has_value()) {
            err << "suggest: --account <id> is required\n";
            return 2;
        }

        unsigned months = 3;
        if (const auto m_opt = get_option(args, "--months"); m_opt.has_value()) {
            int parsed = 0;
            if (std::from_chars(m_opt->data(), m_opt->data() + m_opt->size(), parsed).ec !=
                    std::errc{} ||
                parsed <= 0) {
                err << "suggest: --months must be a positive integer\n";
                return 2;
            }
            months = static_cast<unsigned>(parsed);
        }

        const auto journal_opt = load_journal_from_args(args, *account, env, err);
        if (!journal_opt.has_value()) {
            return 1;
        }
        const auto& journal = *journal_opt;
        if (journal.empty()) {
            err << "suggest: no transactions available\n";
            return 1;
        }

        std::chrono::year_month ending{};
        if (const auto e_opt = get_option(args, "--ending"); e_opt.has_value()) {
            const auto parsed = parse_year_month(*e_opt);
            if (!parsed.has_value()) {
                err << "suggest: --ending must be YYYY-MM\n";
                return 2;
            }
            ending = *parsed;
        } else {
            for (const auto& tx : journal.transactions()) {
                const std::chrono::year_month ym{tx.date().year(), tx.date().month()};
                if (ym > ending) ending = ym;
            }
        }

        const auto cfg = load_budget_config(env.budget_config_path);
        const auto suggestions =
            analytics::suggest_budget(journal, ending, months, cfg.currency);

        out << "Suggested monthly budgets — basis: trailing " << months
            << " months ending " << format_year_month(ending) << '\n';
        out << "  category                   avg outflow  suggested\n";
        money::Money total_avg = money::Money::zero(cfg.currency);
        money::Money total_suggest = money::Money::zero(cfg.currency);
        for (const auto& s : suggestions) {
            const std::string label = s.category_id.empty() ? "(uncategorised)" : s.category_id;
            out << "  " << pad_right(label, 24)
                << pad_left(format_money(s.rolling_average), 14) << "  "
                << pad_left(format_money(s.suggested_limit), 14) << '\n';
            total_avg += s.rolling_average;
            total_suggest += s.suggested_limit;
        }
        out << "  " << pad_right("TOTAL", 24)
            << pad_left(format_money(total_avg), 14) << "  "
            << pad_left(format_money(total_suggest), 14) << '\n';
        out << "\nTo adopt these, run `wt budget set <category> <amount>` for each line.\n";
        return 0;
    }

    int cmd_export(Args args, std::ostream& out, std::ostream& err, const Environment& env) {
        const auto pos = positional(args);
        if (pos.empty()) {
            err << "export: expected an output CSV path\n";
            return 2;
        }
        const auto account = get_option(args, "--account");
        if (!account.has_value()) {
            err << "export: --account <id> is required\n";
            return 2;
        }

        // Source: either --from-db, or the second positional (input CSV path).
        std::optional<ledger::Journal> journal_opt;
        const bool from_db = std::find(args.begin(), args.end(),
                                        std::string_view{"--from-db"}) != args.end();
        if (from_db) {
            const auto url = storage::Connection::database_url_from_env();
            if (!url.has_value()) {
                err << "export: $DATABASE_URL is required for --from-db\n";
                return 1;
            }
            storage::Connection conn{*url};
            const storage::TransactionRepository repo{conn};
            journal_opt = repo.load_journal(*account);
        } else {
            if (pos.size() < 2) {
                err << "export: expected `wt export <out.csv> <in.csv> --account ID`"
                       " (or use --from-db)\n";
                return 2;
            }
            std::ifstream in{std::string(pos[1])};
            if (!in) {
                err << "export: cannot open input " << pos[1] << '\n';
                return 1;
            }
            import_::ImportOptions opts;
            opts.account_id = std::string(*account);
            const auto user_rules = load_rules_config(env.rules_config_path);
            const auto base_overrides = import_::default_overrides();
            const auto overrides = build_categorizer(user_rules, base_overrides);
            opts.overrides = &overrides;
            const auto imp = import_::import_bp_csv(in, opts);
            ledger::Journal j;
            for (const auto& tx : imp.transactions) j.add(tx);
            journal_opt = std::move(j);
        }

        const std::string out_path{pos[0]};
        std::ofstream csv(out_path);
        if (!csv) {
            err << "export: cannot write " << out_path << '\n';
            return 1;
        }

        auto format_dmy = [](std::chrono::year_month_day d) {
            std::ostringstream oss;
            const unsigned dd = static_cast<unsigned>(d.day());
            if (dd < 10) oss << '0';
            oss << dd << '/';
            const unsigned mm = static_cast<unsigned>(d.month());
            if (mm < 10) oss << '0';
            oss << mm << '/' << static_cast<int>(d.year());
            return oss.str();
        };
        auto format_amount = [](std::int64_t minor) {
            std::ostringstream oss;
            const bool neg = minor < 0;
            const std::int64_t abs_minor = neg ? -minor : minor;
            const auto integral = abs_minor / 100;
            const auto fraction = abs_minor % 100;
            if (neg) oss << '-';
            oss << integral << ',' << std::setw(2) << std::setfill('0') << fraction;
            return oss.str();
        };

        // 11-column header matching SORTED_DATA.xlsx exactly.
        csv << "Date de comptabilisation;Libelle simplifie;Libelle operation;"
               "Type operation;Categorie;Sous categorie;Debit;Credit;"
               "Date operation;Date de valeur;Colonne 1\r\n";

        std::size_t rows_written = 0;
        for (const auto& t : journal_opt->transactions()) {
            // Split description back into libelle_simplifie / libelle_operation if it contains the
            // "—" separator we injected during import. Otherwise emit the full string in both.
            std::string libelle_simplifie = t.description();
            std::string libelle_operation = t.description();
            if (const auto sep = libelle_simplifie.find(" — "); sep != std::string::npos) {
                libelle_operation = libelle_simplifie.substr(sep + 5);
                libelle_simplifie = libelle_simplifie.substr(0, sep);
            }
            const std::string debit = t.is_outflow() ? format_amount(t.amount().minor_units()) : "";
            const std::string credit = t.is_inflow() ? "+" + format_amount(t.amount().minor_units()) : "";

            csv << format_dmy(t.date()) << ';' << libelle_simplifie << ';' << libelle_operation
                << ';' << t.type_operation()
                << ';' << t.bp_category().value_or("")
                << ';' << t.bp_subcategory().value_or("")
                << ';' << debit << ';' << credit
                << ';' << format_dmy(t.date()) << ';' << format_dmy(t.date())
                << ';' << (t.is_reconciled() ? "True" : "False")
                << "\r\n";
            ++rows_written;
        }
        out << "wrote " << rows_written << " rows to " << out_path << '\n';
        return 0;
    }

    int run(Args args, std::ostream& out, std::ostream& err, const Environment& env) {
        if (args.empty()) {
            return cmd_help({}, out, err);
        }
        const auto cmd = args.front();
        const Args rest = args.subspan(1);

        try {
            if (cmd == "help" || cmd == "--help" || cmd == "-h") return cmd_help(rest, out, err);
            if (cmd == "allocate") return cmd_allocate(rest, out, err);
            if (cmd == "categories") return cmd_categories(rest, out, err);
            if (cmd == "import") return cmd_import(rest, out, err, env);
            if (cmd == "report") return cmd_report(rest, out, err, env);
            if (cmd == "budget") return cmd_budget(rest, out, err, env);
            if (cmd == "rules") return cmd_rules(rest, out, err, env);
            if (cmd == "sync") return cmd_sync(rest, out, err, env);
            if (cmd == "suggest") return cmd_suggest(rest, out, err, env);
            if (cmd == "export") return cmd_export(rest, out, err, env);
            err << "wt: unknown command '" << cmd << "' (try `wt help`)\n";
            return 2;
        } catch (const std::exception& e) {
            err << "wt: error: " << e.what() << '\n';
            return 1;
        }
    }

} // namespace wealthtorii::cli
