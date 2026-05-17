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
#include <utility>
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

    // Per-user budget limits. currency is uniform across the set (enforced in
    // the API); empty when the user has no limits yet.
    struct StoredBudget {
        std::string currency = "EUR";
        std::vector<std::pair<std::string, std::int64_t>> limits; // (category, minor)
    };

    class BudgetConfigRepository {
    public:
        explicit BudgetConfigRepository(Connection& conn) noexcept : conn_(&conn) {}

        [[nodiscard]] StoredBudget get(std::string_view user_id) const;

        // Replaces the user's whole budget (delete-all then insert).
        void replace(std::string_view user_id, const StoredBudget& budget);

    private:
        Connection* conn_;
    };

    // One ordered categorisation rule (insertion order = match priority).
    struct StoredRule {
        std::string pattern;
        std::string category_id;
        std::optional<std::string> bp_subcategory;
    };

    class RulesRepository {
    public:
        explicit RulesRepository(Connection& conn) noexcept : conn_(&conn) {}

        [[nodiscard]] std::vector<StoredRule> list(std::string_view user_id) const;

        // Replaces the user's whole rule list, preserving the given order.
        void replace(std::string_view user_id, const std::vector<StoredRule>& rules);

    private:
        Connection* conn_;
    };

    struct UpsertStats {
        std::size_t inserted = 0;
        std::size_t updated = 0;
    };

    // An account with its opening balance and the derived current balance
    // (opening_balance + sum of the account's transaction amounts), in minor
    // units of `currency`.
    struct AccountBalance {
        std::string id;
        std::string name;
        std::string currency;
        std::string type;
        std::int64_t opening_balance = 0;
        std::int64_t current_balance = 0;
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
        bool ensure(const ledger::Account& account, std::string_view user_id,
                    std::int64_t opening_balance = 0);
        bool update(const ledger::Account& account, std::string_view user_id,
                    std::int64_t opening_balance = 0);
        bool remove(std::string_view id, std::string_view user_id);
        [[nodiscard]] std::optional<ledger::Account> find(std::string_view id,
                                                          std::string_view user_id) const;
        [[nodiscard]] std::vector<ledger::Account> all(std::string_view user_id) const;

        // Returns the owning user_id of an account (NULL/absent -> nullopt).
        // Used to enforce transaction tenancy via the parent account.
        [[nodiscard]] std::optional<std::string> owner_of(
            std::string_view account_id) const;

        // Per-account current balance for a user (opening_balance + sum of
        // transaction amounts), ordered by account id.
        [[nodiscard]] std::vector<AccountBalance> balances(
            std::string_view user_id) const;

        // Balance of one owned account; nullopt if absent / not the user's.
        [[nodiscard]] std::optional<AccountBalance> balance_of(
            std::string_view user_id, std::string_view account_id) const;

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

    // A savings goal ("projet saving"). target_date is optional (ISO yyyy-mm-dd
    // string when set). currency is an ISO code.
    struct SavingsGoal {
        std::string id;
        std::string name;
        std::string currency = "EUR";
        std::int64_t target_minor = 0;
        std::optional<std::string> target_date;
    };

    // A goal plus the derived saved amount (sum of its contributions).
    struct GoalProgress {
        SavingsGoal goal;
        std::int64_t saved_minor = 0;
    };

    struct Contribution {
        std::string id;
        std::string occurred_on; // ISO yyyy-mm-dd
        std::int64_t minor_units = 0; // + deposit, - withdrawal
        std::string note;
    };

    class SavingsGoalRepository {
    public:
        explicit SavingsGoalRepository(Connection& conn) noexcept : conn_(&conn) {}

        bool create(std::string_view user_id, const SavingsGoal& goal);
        bool update(std::string_view user_id, const SavingsGoal& goal);
        bool remove(std::string_view user_id, std::string_view goal_id);

        [[nodiscard]] std::vector<GoalProgress> list(
            std::string_view user_id) const;
        [[nodiscard]] std::optional<GoalProgress> find(
            std::string_view user_id, std::string_view goal_id) const;

        // Records a contribution on a goal the caller owns. Returns false if
        // the goal doesn't exist for that user.
        bool add_contribution(std::string_view user_id, std::string_view goal_id,
                              const Contribution& c);
        [[nodiscard]] std::vector<Contribution> list_contributions(
            std::string_view goal_id) const;

    private:
        Connection* conn_;
    };

    // ---- investments --------------------------------------------------------

    struct StoredPrice {
        std::string symbol;
        std::string currency;
        std::int64_t price_minor = 0;
        std::string as_of; // ISO yyyy-mm-dd
    };

    class InstrumentPriceRepository {
    public:
        explicit InstrumentPriceRepository(Connection& conn) noexcept
            : conn_(&conn) {}

        // Upserts the latest price for (user, symbol).
        void upsert(std::string_view user_id, const StoredPrice& p);
        [[nodiscard]] std::vector<StoredPrice> list(
            std::string_view user_id) const;
        bool remove(std::string_view user_id, std::string_view symbol);

    private:
        Connection* conn_;
    };

    struct StoredPosition {
        std::string id;
        std::string account_id; // optional label, "" -> stored NULL
        std::string symbol;
        std::int64_t quantity_micro = 0;
        std::int64_t cost_minor = 0;
        std::string currency;
    };

    class PositionRepository {
    public:
        explicit PositionRepository(Connection& conn) noexcept
            : conn_(&conn) {}

        bool create(std::string_view user_id, const StoredPosition& p);
        bool update(std::string_view user_id, const StoredPosition& p);
        bool remove(std::string_view user_id, std::string_view id);
        [[nodiscard]] std::vector<StoredPosition> list(
            std::string_view user_id) const;

    private:
        Connection* conn_;
    };

} // namespace wealthtorii::storage
