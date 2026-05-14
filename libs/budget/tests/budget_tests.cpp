#include <gtest/gtest.h>

#include "wealthtorii/budget/budget.hpp"

#include <chrono>

using namespace wealthtorii::budget;
using wealthtorii::ledger::Journal;
using wealthtorii::ledger::Transaction;
using wealthtorii::money::Currency;
using wealthtorii::money::CurrencyMismatch;
using wealthtorii::money::Money;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;
using std::chrono::year_month;
using std::chrono::year_month_day;

namespace {
    constexpr year_month kMay{year{2026}, month{5}};
    constexpr year_month_day kDay{year{2026}, month{5}, day{15}};

    Transaction tx(std::string id, std::int64_t minor, std::string category) {
        return Transaction(std::move(id), kDay, "BP",
                           Money(minor, Currency::EUR), "x", std::move(category));
    }
}

TEST(BudgetTest, SetLimitAndQuery) {
    Budget b{kMay, Currency::EUR};
    b.set_limit("housing", Money(85000, Currency::EUR));
    b.set_limit("groceries", Money(30000, Currency::EUR));

    ASSERT_TRUE(b.limit_for("housing").has_value());
    EXPECT_EQ(*b.limit_for("housing"), Money(85000, Currency::EUR));
    EXPECT_FALSE(b.limit_for("unknown").has_value());
    EXPECT_EQ(b.total_limit(), Money(115000, Currency::EUR));
}

TEST(BudgetTest, RejectsEmptyCategoryId) {
    Budget b{kMay, Currency::EUR};
    EXPECT_THROW(b.set_limit("", Money(1, Currency::EUR)), std::invalid_argument);
}

TEST(BudgetTest, RejectsNegativeLimit) {
    Budget b{kMay, Currency::EUR};
    EXPECT_THROW(b.set_limit("housing", Money(-1, Currency::EUR)), std::invalid_argument);
}

TEST(BudgetTest, RejectsMismatchedCurrency) {
    Budget b{kMay, Currency::EUR};
    EXPECT_THROW(b.set_limit("housing", Money(100, Currency::USD)), CurrencyMismatch);
}

TEST(BudgetTest, SpendingVsBudgetReportsDeltas) {
    Budget b{kMay, Currency::EUR};
    b.set_limit("housing", Money(85000, Currency::EUR));
    b.set_limit("groceries", Money(30000, Currency::EUR));
    b.set_limit("dining", Money(10000, Currency::EUR));

    Journal j;
    j.add(tx("T1", -85000, "housing"));    // pile sur le budget
    j.add(tx("T2", -25000, "groceries"));  // sous le budget
    j.add(tx("T3", -12000, "dining"));     // dépassement

    const auto lines = spending_vs_budget(b, j);
    ASSERT_EQ(lines.size(), 3u);

    const auto find = [&](std::string_view cat) {
        for (const auto& l : lines) {
            if (l.category_id == cat) return &l;
        }
        return static_cast<const BudgetLine*>(nullptr);
    };

    EXPECT_EQ(find("housing")->delta, Money(0, Currency::EUR));
    EXPECT_EQ(find("groceries")->delta, Money(5000, Currency::EUR));
    EXPECT_EQ(find("dining")->delta, Money(-2000, Currency::EUR));
}

TEST(BudgetTest, SpendingVsBudgetIncludesOnlyTrackedMonths) {
    Budget b{kMay, Currency::EUR};
    b.set_limit("housing", Money(85000, Currency::EUR));

    Journal j;
    j.add(Transaction("T1", year_month_day{year{2026}, month{6}, day{1}}, "BP",
                      Money(-85000, Currency::EUR), "next month", "housing"));

    const auto lines = spending_vs_budget(b, j);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].category_id, "housing");
    EXPECT_EQ(lines[0].spent, Money(0, Currency::EUR));
    EXPECT_EQ(lines[0].budgeted, Money(85000, Currency::EUR));
    EXPECT_EQ(lines[0].delta, Money(85000, Currency::EUR));
}

TEST(BudgetTest, SpendingVsBudgetCombinesBudgetedAndUnbudgetedSpending) {
    Budget b{kMay, Currency::EUR};
    b.set_limit("housing", Money(85000, Currency::EUR));

    Journal j;
    j.add(tx("T1", -90000, "housing"));      // dépassement de 5 000
    j.add(tx("T2", -12000, "transport"));    // pas de budget pour transport

    const auto lines = spending_vs_budget(b, j);
    ASSERT_EQ(lines.size(), 2u);

    const auto find = [&](std::string_view cat) {
        for (const auto& l : lines) {
            if (l.category_id == cat) return &l;
        }
        return static_cast<const BudgetLine*>(nullptr);
    };

    const auto* housing = find("housing");
    ASSERT_NE(housing, nullptr);
    EXPECT_EQ(housing->spent, Money(90000, Currency::EUR));
    EXPECT_EQ(housing->budgeted, Money(85000, Currency::EUR));
    EXPECT_EQ(housing->delta, Money(-5000, Currency::EUR));

    const auto* transport = find("transport");
    ASSERT_NE(transport, nullptr);
    EXPECT_EQ(transport->spent, Money(12000, Currency::EUR));
    EXPECT_EQ(transport->budgeted, Money(0, Currency::EUR));
    EXPECT_EQ(transport->delta, Money(-12000, Currency::EUR));
}
