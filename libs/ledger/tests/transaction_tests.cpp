#include <gtest/gtest.h>

#include "wealthtorii/ledger/transaction.hpp"

#include <chrono>
#include <stdexcept>

using namespace wealthtorii::ledger;
using wealthtorii::money::Currency;
using wealthtorii::money::Money;

namespace {
    constexpr std::chrono::year_month_day kDay{
        std::chrono::year{2026}, std::chrono::month{5}, std::chrono::day{14}};
}

TEST(TransactionTest, ConstructsValidTransaction) {
    const Transaction tx{"T1", kDay, "BP_CHECKING", Money(-4250, Currency::EUR),
                         "TOTAL ACCESS"};

    EXPECT_EQ(tx.id(), "T1");
    EXPECT_EQ(tx.account_id(), "BP_CHECKING");
    EXPECT_EQ(tx.amount(), Money(-4250, Currency::EUR));
    EXPECT_TRUE(tx.is_outflow());
    EXPECT_FALSE(tx.category_id().has_value());
}

TEST(TransactionTest, InflowDetectedForPositiveAmount) {
    const Transaction tx{"T2", kDay, "BP_CHECKING", Money(180000, Currency::EUR),
                         "SALAIRE"};

    EXPECT_TRUE(tx.is_inflow());
    EXPECT_FALSE(tx.is_outflow());
}

TEST(TransactionTest, RejectsZeroAmount) {
    EXPECT_THROW(Transaction("T", kDay, "A", Money(0, Currency::EUR), "noop"),
                 std::invalid_argument);
}

TEST(TransactionTest, RejectsEmptyId) {
    EXPECT_THROW(Transaction("", kDay, "A", Money(-100, Currency::EUR), "x"),
                 std::invalid_argument);
}

TEST(TransactionTest, RejectsEmptyAccountId) {
    EXPECT_THROW(Transaction("T", kDay, "", Money(-100, Currency::EUR), "x"),
                 std::invalid_argument);
}

TEST(TransactionTest, RejectsInvalidDate) {
    const std::chrono::year_month_day bad{
        std::chrono::year{2026}, std::chrono::month{2}, std::chrono::day{31}};
    EXPECT_THROW(Transaction("T", bad, "A", Money(-100, Currency::EUR), "x"),
                 std::invalid_argument);
}

TEST(TransactionTest, AssignAndClearCategory) {
    Transaction tx{"T", kDay, "A", Money(-100, Currency::EUR), "x"};

    tx.assign_category("groceries");
    ASSERT_TRUE(tx.category_id().has_value());
    EXPECT_EQ(*tx.category_id(), "groceries");

    tx.clear_category();
    EXPECT_FALSE(tx.category_id().has_value());
}

TEST(TransactionTest, AssignCategoryRejectsEmpty) {
    Transaction tx{"T", kDay, "A", Money(-100, Currency::EUR), "x"};
    EXPECT_THROW(tx.assign_category(""), std::invalid_argument);
}

TEST(TransactionTest, EqualityDistinguishesAmountsAndCategories) {
    const Transaction a{"T", kDay, "A", Money(-100, Currency::EUR), "x", "g"};
    const Transaction b{"T", kDay, "A", Money(-100, Currency::EUR), "x", "g"};
    const Transaction c{"T", kDay, "A", Money(-100, Currency::EUR), "x", "h"};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
