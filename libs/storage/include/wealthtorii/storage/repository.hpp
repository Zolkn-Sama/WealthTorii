#pragma once

#include "connection.hpp"

#include <wealthtorii/budget/budget.hpp>
#include <wealthtorii/ledger/account.hpp>
#include <wealthtorii/ledger/journal.hpp>
#include <wealthtorii/ledger/transaction.hpp>
#include <wealthtorii/money/money.hpp>

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wealthtorii::storage {

    // Application user (login + freemium plan). Distinct from ledger::Account.
    struct User {
        std::string id;
        std::string email;
        std::string password_hash; // self-contained Argon2id string
        std::string plan = "free"; // "free" | "premium"
    };

    class UserRepository {
    public:
        explicit UserRepository(Connection& conn) noexcept : conn_(&conn) {}

        // Inserts the user. Returns false if the email is already taken.
        bool create(const User& user);

        [[nodiscard]] std::optional<User> find_by_email(std::string_view email) const;
        [[nodiscard]] std::optional<User> find_by_id(std::string_view id) const;

        // Updates the plan ("free"|"premium"). Returns false if the id is unknown.
        bool set_plan(std::string_view id, std::string_view plan);

    private:
        Connection* conn_;
    };

    struct UpsertStats {
        std::size_t inserted = 0;
        std::size_t updated = 0;
    };

    class AccountRepository {
    public:
        explicit AccountRepository(Connection& conn) noexcept : conn_(&conn) {}

        // Inserts the account if absent, no-op otherwise. Returns true on insert.
        bool ensure(const ledger::Account& account);

        // Updates name/currency/type/is_active of an existing account (matched by id).
        // Returns false if no account with that id exists.
        bool update(const ledger::Account& account);

        // Deletes the account and (via ON DELETE CASCADE) its transactions.
        // Returns false if no account with that id existed.
        bool remove(std::string_view id);

        [[nodiscard]] std::optional<ledger::Account> find(std::string_view id) const;
        [[nodiscard]] std::vector<ledger::Account> all() const;

        // --- user-scoped (multi-tenant) variants used by the API ---
        // Each binds the row to user_id; queries filter on it so one user never
        // sees another's accounts. The CLI keeps using the user-less overloads.
        bool ensure(const ledger::Account& account, std::string_view user_id);
        bool update(const ledger::Account& account, std::string_view user_id);
        bool remove(std::string_view id, std::string_view user_id);
        [[nodiscard]] std::optional<ledger::Account> find(std::string_view id,
                                                          std::string_view user_id) const;
        [[nodiscard]] std::vector<ledger::Account> all(std::string_view user_id) const;

        // Returns the owning user_id of an account (NULL/absent -> nullopt).
        // Used to enforce transaction tenancy via the parent account.
        [[nodiscard]] std::optional<std::string> owner_of(
            std::string_view account_id) const;

    private:
        Connection* conn_;
    };

    class TransactionRepository {
    public:
        explicit TransactionRepository(Connection& conn) noexcept : conn_(&conn) {}

        // Upserts every transaction (PK = id). Re-importing the same CSV is idempotent.
        UpsertStats upsert(std::span<const ledger::Transaction> txs);

        [[nodiscard]] std::optional<ledger::Transaction> find(std::string_view id) const;
        [[nodiscard]] std::vector<ledger::Transaction> for_account(std::string_view account_id) const;
        [[nodiscard]] std::vector<ledger::Transaction> for_month(
            std::string_view account_id, std::chrono::year_month month) const;

        // Deletes a single transaction by id. Returns false if it did not exist.
        bool remove(std::string_view id);

        // Convenience: loads transactions for an account into a Journal.
        [[nodiscard]] ledger::Journal load_journal(std::string_view account_id) const;

    private:
        Connection* conn_;
    };

    class BudgetRepository {
    public:
        explicit BudgetRepository(Connection& conn) noexcept : conn_(&conn) {}

        // Replaces (month, category) limits with the supplied budget.
        void upsert(const budget::Budget& b);

        [[nodiscard]] std::optional<budget::Budget> find(std::chrono::year_month month,
                                                          money::Currency currency) const;

    private:
        Connection* conn_;
    };

} // namespace wealthtorii::storage
