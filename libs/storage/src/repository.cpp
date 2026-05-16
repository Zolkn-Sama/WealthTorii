#include "wealthtorii/storage/repository.hpp"

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace wealthtorii::storage {

    namespace {
        std::string format_iso_date(std::chrono::year_month_day ymd) {
            std::ostringstream oss;
            oss << static_cast<int>(ymd.year()) << '-';
            const unsigned m = static_cast<unsigned>(ymd.month());
            if (m < 10) oss << '0';
            oss << m << '-';
            const unsigned d = static_cast<unsigned>(ymd.day());
            if (d < 10) oss << '0';
            oss << d;
            return oss.str();
        }

        std::chrono::year_month_day parse_iso_date(std::string_view s) {
            if (s.size() < 10 || s[4] != '-' || s[7] != '-') {
                throw std::runtime_error("invalid ISO date from DB: " + std::string(s));
            }
            const int y = std::stoi(std::string(s.substr(0, 4)));
            const unsigned m = static_cast<unsigned>(std::stoi(std::string(s.substr(5, 2))));
            const unsigned d = static_cast<unsigned>(std::stoi(std::string(s.substr(8, 2))));
            return std::chrono::year_month_day{std::chrono::year{y}, std::chrono::month{m},
                                                std::chrono::day{d}};
        }

        std::string currency_code(money::Currency c) {
            return std::string(money::to_string(c));
        }

        money::Currency parse_currency(std::string_view code) {
            const auto parsed = money::currency_from_string(code);
            if (!parsed.has_value()) {
                throw std::runtime_error("unknown currency code from DB: " + std::string(code));
            }
            return *parsed;
        }

        std::string account_type_code(ledger::AccountType t) {
            return std::string(ledger::to_string(t));
        }
    } // namespace

    bool AccountRepository::ensure(const ledger::Account& account) {
        pqxx::work tx(conn_->raw());
        const auto r = tx.exec(
            "INSERT INTO accounts (id, name, currency, type, is_active) "
            "VALUES ($1, $2, $3, $4, $5) "
            "ON CONFLICT (id) DO NOTHING",
            pqxx::params{account.id(), account.name(), currency_code(account.currency()),
                         account_type_code(account.type()), account.is_active()});
        tx.commit();
        return r.affected_rows() > 0;
    }

    std::optional<ledger::Account> AccountRepository::find(std::string_view id) const {
        pqxx::work tx(conn_->raw());
        const auto r = tx.exec(
            "SELECT id, name, currency, type, is_active FROM accounts WHERE id = $1",
            pqxx::params{std::string(id)});
        if (r.empty()) return std::nullopt;
        const auto& row = r.front();
        const auto type = ledger::account_type_from_string(row[3].as<std::string>());
        if (!type.has_value()) {
            throw std::runtime_error("unknown account type from DB: " + row[3].as<std::string>());
        }
        return ledger::Account(row[0].as<std::string>(), row[1].as<std::string>(),
                                parse_currency(row[2].as<std::string>()), *type,
                                row[4].as<bool>());
    }

    std::vector<ledger::Account> AccountRepository::all() const {
        pqxx::work tx(conn_->raw());
        const auto r = tx.exec(
            "SELECT id, name, currency, type, is_active FROM accounts ORDER BY id");
        std::vector<ledger::Account> out;
        out.reserve(static_cast<std::size_t>(r.size()));
        for (const auto& row : r) {
            const auto type = ledger::account_type_from_string(row[3].as<std::string>());
            if (!type.has_value()) continue;
            out.emplace_back(row[0].as<std::string>(), row[1].as<std::string>(),
                              parse_currency(row[2].as<std::string>()), *type,
                              row[4].as<bool>());
        }
        return out;
    }

    bool AccountRepository::update(const ledger::Account& account) {
        pqxx::work tx(conn_->raw());
        const auto r = tx.exec(
            "UPDATE accounts SET name = $2, currency = $3, type = $4, is_active = $5 "
            "WHERE id = $1",
            pqxx::params{account.id(), account.name(), currency_code(account.currency()),
                         account_type_code(account.type()), account.is_active()});
        tx.commit();
        return r.affected_rows() > 0;
    }

    bool AccountRepository::remove(std::string_view id) {
        pqxx::work tx(conn_->raw());
        const auto r = tx.exec("DELETE FROM accounts WHERE id = $1",
                               pqxx::params{std::string(id)});
        tx.commit();
        return r.affected_rows() > 0;
    }

    std::optional<ledger::Transaction> TransactionRepository::find(
        std::string_view id) const {
        pqxx::work tx(conn_->raw());
        const auto r = tx.exec(
            "SELECT id, account_id, occurred_on::text, minor_units, currency, description, "
            "       category_id, bp_category, bp_subcategory, type_operation, is_reconciled "
            "FROM transactions WHERE id = $1",
            pqxx::params{std::string(id)});
        if (r.empty()) return std::nullopt;
        const auto& row = r.front();
        const auto date = parse_iso_date(row[2].as<std::string>());
        const money::Money amount(row[3].as<std::int64_t>(),
                                   parse_currency(row[4].as<std::string>()));
        std::optional<std::string> category;
        if (!row[6].is_null()) category = row[6].as<std::string>();
        std::optional<std::string> bp_cat;
        if (!row[7].is_null()) bp_cat = row[7].as<std::string>();
        std::optional<std::string> bp_sub;
        if (!row[8].is_null()) bp_sub = row[8].as<std::string>();
        return ledger::Transaction(row[0].as<std::string>(), date, row[1].as<std::string>(),
                                    amount, row[5].as<std::string>(), std::move(category),
                                    std::move(bp_cat), std::move(bp_sub),
                                    row[9].as<std::string>(), row[10].as<bool>());
    }

    bool TransactionRepository::remove(std::string_view id) {
        pqxx::work tx(conn_->raw());
        const auto r = tx.exec("DELETE FROM transactions WHERE id = $1",
                               pqxx::params{std::string(id)});
        tx.commit();
        return r.affected_rows() > 0;
    }

    UpsertStats TransactionRepository::upsert(std::span<const ledger::Transaction> txs) {
        UpsertStats stats;
        pqxx::work tx(conn_->raw());
        for (const auto& t : txs) {
            const auto r = tx.exec(
                "INSERT INTO transactions "
                "  (id, account_id, occurred_on, minor_units, currency, description, "
                "   category_id, bp_category, bp_subcategory, type_operation, is_reconciled) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11) "
                "ON CONFLICT (id) DO UPDATE SET "
                "  account_id     = EXCLUDED.account_id, "
                "  occurred_on    = EXCLUDED.occurred_on, "
                "  minor_units    = EXCLUDED.minor_units, "
                "  currency       = EXCLUDED.currency, "
                "  description    = EXCLUDED.description, "
                "  category_id    = EXCLUDED.category_id, "
                "  bp_category    = EXCLUDED.bp_category, "
                "  bp_subcategory = EXCLUDED.bp_subcategory, "
                "  type_operation = EXCLUDED.type_operation, "
                "  is_reconciled  = transactions.is_reconciled "
                "WHERE  transactions.account_id     IS DISTINCT FROM EXCLUDED.account_id "
                "   OR  transactions.occurred_on    IS DISTINCT FROM EXCLUDED.occurred_on "
                "   OR  transactions.minor_units    IS DISTINCT FROM EXCLUDED.minor_units "
                "   OR  transactions.currency       IS DISTINCT FROM EXCLUDED.currency "
                "   OR  transactions.description    IS DISTINCT FROM EXCLUDED.description "
                "   OR  transactions.category_id    IS DISTINCT FROM EXCLUDED.category_id "
                "   OR  transactions.bp_category    IS DISTINCT FROM EXCLUDED.bp_category "
                "   OR  transactions.bp_subcategory IS DISTINCT FROM EXCLUDED.bp_subcategory "
                "   OR  transactions.type_operation IS DISTINCT FROM EXCLUDED.type_operation "
                "RETURNING (xmax = 0) AS inserted",
                pqxx::params{t.id(), t.account_id(), format_iso_date(t.date()),
                             t.amount().minor_units(), currency_code(t.amount().currency()),
                             t.description(), t.category_id(),
                             t.bp_category(), t.bp_subcategory(),
                             t.type_operation(), t.is_reconciled()});
            for (const auto& row : r) {
                if (row[0].as<bool>()) {
                    ++stats.inserted;
                } else {
                    ++stats.updated;
                }
            }
        }
        tx.commit();
        return stats;
    }

    std::vector<ledger::Transaction> TransactionRepository::for_account(
        std::string_view account_id) const {
        pqxx::work tx(conn_->raw());
        const auto r = tx.exec(
            "SELECT id, account_id, occurred_on::text, minor_units, currency, description, "
            "       category_id, bp_category, bp_subcategory, type_operation, is_reconciled "
            "FROM transactions WHERE account_id = $1 ORDER BY occurred_on, id",
            pqxx::params{std::string(account_id)});

        std::vector<ledger::Transaction> out;
        out.reserve(static_cast<std::size_t>(r.size()));
        for (const auto& row : r) {
            const auto date = parse_iso_date(row[2].as<std::string>());
            const money::Money amount(row[3].as<std::int64_t>(),
                                       parse_currency(row[4].as<std::string>()));
            std::optional<std::string> category;
            if (!row[6].is_null()) category = row[6].as<std::string>();
            std::optional<std::string> bp_cat;
            if (!row[7].is_null()) bp_cat = row[7].as<std::string>();
            std::optional<std::string> bp_sub;
            if (!row[8].is_null()) bp_sub = row[8].as<std::string>();
            out.emplace_back(row[0].as<std::string>(), date, row[1].as<std::string>(), amount,
                              row[5].as<std::string>(), std::move(category),
                              std::move(bp_cat), std::move(bp_sub),
                              row[9].as<std::string>(), row[10].as<bool>());
        }
        return out;
    }

    std::vector<ledger::Transaction> TransactionRepository::for_month(
        std::string_view account_id, std::chrono::year_month month) const {
        pqxx::work tx(conn_->raw());
        const auto first = std::chrono::year_month_day{
            month.year(), month.month(), std::chrono::day{1}};
        // Next-month first-of-month — using a +1 month and clamping. C++20 chrono supports it.
        auto next = month + std::chrono::months{1};
        const auto next_first = std::chrono::year_month_day{
            next.year(), next.month(), std::chrono::day{1}};

        const auto r = tx.exec(
            "SELECT id, account_id, occurred_on::text, minor_units, currency, description, "
            "       category_id, bp_category, bp_subcategory, type_operation, is_reconciled "
            "FROM transactions WHERE account_id = $1 "
            "  AND occurred_on >= $2 AND occurred_on < $3 ORDER BY occurred_on, id",
            pqxx::params{std::string(account_id), format_iso_date(first),
                         format_iso_date(next_first)});
        std::vector<ledger::Transaction> out;
        out.reserve(static_cast<std::size_t>(r.size()));
        for (const auto& row : r) {
            const auto date = parse_iso_date(row[2].as<std::string>());
            const money::Money amount(row[3].as<std::int64_t>(),
                                       parse_currency(row[4].as<std::string>()));
            std::optional<std::string> category;
            if (!row[6].is_null()) category = row[6].as<std::string>();
            std::optional<std::string> bp_cat;
            if (!row[7].is_null()) bp_cat = row[7].as<std::string>();
            std::optional<std::string> bp_sub;
            if (!row[8].is_null()) bp_sub = row[8].as<std::string>();
            out.emplace_back(row[0].as<std::string>(), date, row[1].as<std::string>(), amount,
                              row[5].as<std::string>(), std::move(category),
                              std::move(bp_cat), std::move(bp_sub),
                              row[9].as<std::string>(), row[10].as<bool>());
        }
        return out;
    }

    ledger::Journal TransactionRepository::load_journal(std::string_view account_id) const {
        ledger::Journal j;
        for (auto& tx : for_account(account_id)) {
            j.add(std::move(tx));
        }
        return j;
    }

    void BudgetRepository::upsert(const budget::Budget& b) {
        pqxx::work tx(conn_->raw());
        const auto first = std::chrono::year_month_day{b.month().year(), b.month().month(),
                                                        std::chrono::day{1}};
        const std::string month_iso = format_iso_date(first);
        // Clear existing for this month then insert each row — simpler than per-row upsert and
        // matches "Budget is the source of truth for that month" semantics.
        tx.exec("DELETE FROM budgets WHERE month = $1", pqxx::params{month_iso});
        for (const auto& [cat, limit] : b.limits()) {
            tx.exec(
                "INSERT INTO budgets (month, currency, category_id, minor_units) "
                "VALUES ($1, $2, $3, $4)",
                pqxx::params{month_iso, currency_code(b.currency()), cat, limit.minor_units()});
        }
        tx.commit();
    }

    std::optional<budget::Budget> BudgetRepository::find(std::chrono::year_month month,
                                                          money::Currency currency) const {
        pqxx::work tx(conn_->raw());
        const auto first = std::chrono::year_month_day{month.year(), month.month(),
                                                        std::chrono::day{1}};
        const auto r = tx.exec(
            "SELECT category_id, minor_units, currency FROM budgets WHERE month = $1",
            pqxx::params{format_iso_date(first)});
        if (r.empty()) return std::nullopt;

        budget::Budget out(month, currency);
        for (const auto& row : r) {
            const auto row_currency = parse_currency(row[2].as<std::string>());
            if (row_currency != currency) continue;
            out.set_limit(row[0].as<std::string>(),
                           money::Money(row[1].as<std::int64_t>(), row_currency));
        }
        return out;
    }

} // namespace wealthtorii::storage
