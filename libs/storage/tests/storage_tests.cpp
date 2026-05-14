#include <gtest/gtest.h>

#include "wealthtorii/storage/connection.hpp"
#include "wealthtorii/storage/migrations.hpp"
#include "wealthtorii/storage/repository.hpp"

#include <chrono>
#include <cstdlib>
#include <string>

using namespace wealthtorii::storage;
using wealthtorii::budget::Budget;
using wealthtorii::ledger::Account;
using wealthtorii::ledger::AccountType;
using wealthtorii::ledger::Transaction;
using wealthtorii::money::Currency;
using wealthtorii::money::Money;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;
using std::chrono::year_month;
using std::chrono::year_month_day;

namespace {
    std::optional<std::string> test_database_url() {
        if (const char* url = std::getenv("DATABASE_URL"); url != nullptr && url[0] != '\0') {
            return std::string{url};
        }
        return std::nullopt;
    }
}

// --- Pure unit tests (no DB) ---

TEST(StorageEnv, ConnectionFromEnvReturnsNulloptWhenUnset) {
    // Save and clear DATABASE_URL; restore after the test.
    const char* prev = std::getenv("DATABASE_URL");
    const std::string saved = prev != nullptr ? prev : "";
    ::unsetenv("DATABASE_URL");

    EXPECT_FALSE(Connection::database_url_from_env().has_value());

    if (!saved.empty()) {
        ::setenv("DATABASE_URL", saved.c_str(), 1);
    }
}

TEST(StorageEnv, CanConnectReturnsFalseForBogusUrl) {
    EXPECT_FALSE(Connection::can_connect("postgresql://nobody:nopw@127.0.0.1:1/none"));
}

// --- Integration tests (skipped without DATABASE_URL) ---

class StorageIntegration : public ::testing::Test {
protected:
    void SetUp() override {
        const auto url = test_database_url();
        if (!url.has_value() || !Connection::can_connect(*url)) {
            GTEST_SKIP() << "DATABASE_URL not set or unreachable — skipping integration tests";
        }
        url_ = *url;
        conn_ = std::make_unique<Connection>(url_);
        apply_default_migrations(*conn_);

        // Clean slate per test.
        pqxx::work tx(conn_->raw());
        tx.exec("TRUNCATE budgets, transactions, accounts CASCADE");
        tx.commit();
    }

    std::string url_;
    std::unique_ptr<Connection> conn_;
};

TEST_F(StorageIntegration, AccountEnsureIsIdempotent) {
    AccountRepository repo(*conn_);
    const Account a{"BP", "Banque Populaire", Currency::EUR, AccountType::CASH};

    EXPECT_TRUE(repo.ensure(a));
    EXPECT_FALSE(repo.ensure(a));

    const auto fetched = repo.find("BP");
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(fetched->id(), "BP");
    EXPECT_EQ(fetched->currency(), Currency::EUR);
    EXPECT_TRUE(fetched->is_active());
}

TEST_F(StorageIntegration, TransactionUpsertInsertsThenUpdates) {
    AccountRepository accounts(*conn_);
    TransactionRepository txs(*conn_);
    accounts.ensure(Account{"BP", "BP", Currency::EUR, AccountType::CASH});

    const year_month_day d{year{2026}, month{4}, day{15}};
    const Transaction t{"T1", d, "BP", Money(-1234, Currency::EUR), "TEST", std::string{"groceries"}};

    auto stats = txs.upsert(std::vector{t});
    EXPECT_EQ(stats.inserted, 1u);
    EXPECT_EQ(stats.updated, 0u);

    // Re-upsert with a different amount → update path.
    const Transaction t2{"T1", d, "BP", Money(-1500, Currency::EUR), "TEST", std::string{"groceries"}};
    stats = txs.upsert(std::vector{t2});
    EXPECT_EQ(stats.inserted, 0u);
    EXPECT_EQ(stats.updated, 1u);

    const auto loaded = txs.for_account("BP");
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].amount(), Money(-1500, Currency::EUR));
}

TEST_F(StorageIntegration, TransactionForMonthFilters) {
    AccountRepository accounts(*conn_);
    TransactionRepository txs(*conn_);
    accounts.ensure(Account{"BP", "BP", Currency::EUR, AccountType::CASH});

    txs.upsert(std::vector{
        Transaction{"A", year_month_day{year{2026}, month{3}, day{28}}, "BP",
                     Money(-100, Currency::EUR), "march"},
        Transaction{"B", year_month_day{year{2026}, month{4}, day{1}}, "BP",
                     Money(-200, Currency::EUR), "april1"},
        Transaction{"C", year_month_day{year{2026}, month{4}, day{30}}, "BP",
                     Money(-300, Currency::EUR), "april30"},
        Transaction{"D", year_month_day{year{2026}, month{5}, day{1}}, "BP",
                     Money(-400, Currency::EUR), "may"},
    });

    const auto april = txs.for_month("BP", year_month{year{2026}, month{4}});
    ASSERT_EQ(april.size(), 2u);
    EXPECT_EQ(april[0].id(), "B");
    EXPECT_EQ(april[1].id(), "C");
}

TEST_F(StorageIntegration, NullCategoryRoundTrips) {
    AccountRepository accounts(*conn_);
    TransactionRepository txs(*conn_);
    accounts.ensure(Account{"BP", "BP", Currency::EUR, AccountType::CASH});

    txs.upsert(std::vector{Transaction{"X", year_month_day{year{2026}, month{4}, day{1}}, "BP",
                                         Money(-100, Currency::EUR), "no cat"}});
    const auto loaded = txs.for_account("BP");
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_FALSE(loaded[0].category_id().has_value());
}

TEST_F(StorageIntegration, BudgetUpsertReplacesMonth) {
    BudgetRepository repo(*conn_);

    Budget b{year_month{year{2026}, month{4}}, Currency::EUR};
    b.set_limit("housing", Money(60000, Currency::EUR));
    b.set_limit("groceries", Money(30000, Currency::EUR));
    repo.upsert(b);

    auto loaded = repo.find(year_month{year{2026}, month{4}}, Currency::EUR);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->total_limit(), Money(90000, Currency::EUR));

    // Re-upserting with only housing should leave only housing in DB.
    Budget b2{year_month{year{2026}, month{4}}, Currency::EUR};
    b2.set_limit("housing", Money(70000, Currency::EUR));
    repo.upsert(b2);

    loaded = repo.find(year_month{year{2026}, month{4}}, Currency::EUR);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->total_limit(), Money(70000, Currency::EUR));
    EXPECT_FALSE(loaded->limit_for("groceries").has_value());
}
