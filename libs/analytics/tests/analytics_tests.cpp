#include <gtest/gtest.h>

#include "wealthtorii/analytics/analytics.hpp"

#include <chrono>
#include <stdexcept>

using namespace wealthtorii::analytics;
using wealthtorii::ledger::Journal;
using wealthtorii::ledger::Transaction;
using wealthtorii::money::Currency;
using wealthtorii::money::Money;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;
using std::chrono::year_month;
using std::chrono::year_month_day;

namespace {
    Transaction tx(std::string id, year_month_day d, std::int64_t minor,
                   std::string category = "") {
        std::optional<std::string> cat;
        if (!category.empty()) cat = category;
        return Transaction(std::move(id), d, "BP", Money(minor, Currency::EUR), "x",
                            std::move(cat));
    }
}

TEST(MonthlyTotals, AggregatesInflowsAndOutflows) {
    Journal j;
    j.add(tx("A", year_month_day{year{2026}, month{4}, day{1}}, 200000));      // inflow
    j.add(tx("B", year_month_day{year{2026}, month{4}, day{10}}, -85000));     // outflow
    j.add(tx("C", year_month_day{year{2026}, month{4}, day{15}}, -12000));     // outflow
    j.add(tx("D", year_month_day{year{2026}, month{5}, day{1}}, 200000));      // next month

    const auto totals = monthly_totals(j, Currency::EUR);
    ASSERT_EQ(totals.size(), 2u);
    EXPECT_EQ(totals[0].month, (year_month{year{2026}, month{4}}));
    EXPECT_EQ(totals[0].inflow, Money(200000, Currency::EUR));
    EXPECT_EQ(totals[0].outflow, Money(97000, Currency::EUR));
    EXPECT_EQ(totals[0].net, Money(200000 - 97000, Currency::EUR));
}

TEST(OutflowByCategoryByMonth, GroupsCorrectly) {
    Journal j;
    j.add(tx("A", year_month_day{year{2026}, month{3}, day{1}}, -10000, "housing"));
    j.add(tx("B", year_month_day{year{2026}, month{4}, day{1}}, -10000, "housing"));
    j.add(tx("C", year_month_day{year{2026}, month{4}, day{2}}, -5000, "groceries"));
    j.add(tx("D", year_month_day{year{2026}, month{4}, day{3}}, -1500));  // uncategorised

    const auto out = outflow_by_category_by_month(j, Currency::EUR);
    EXPECT_EQ(out.at("housing").at(year_month{year{2026}, month{3}}), Money(10000, Currency::EUR));
    EXPECT_EQ(out.at("housing").at(year_month{year{2026}, month{4}}), Money(10000, Currency::EUR));
    EXPECT_EQ(out.at("groceries").at(year_month{year{2026}, month{4}}), Money(5000, Currency::EUR));
    EXPECT_EQ(out.at("").at(year_month{year{2026}, month{4}}), Money(1500, Currency::EUR));
}

TEST(RollingOutflowAverage, ComputesThreeMonthAverage) {
    Journal j;
    // housing: 300 in Feb, 100 in Mar, 200 in Apr → 3-month average ending Apr = 200
    j.add(tx("F", year_month_day{year{2026}, month{2}, day{1}}, -30000, "housing"));
    j.add(tx("M", year_month_day{year{2026}, month{3}, day{1}}, -10000, "housing"));
    j.add(tx("A", year_month_day{year{2026}, month{4}, day{1}}, -20000, "housing"));

    const auto avg = rolling_outflow_average(j, year_month{year{2026}, month{4}}, 3, Currency::EUR);
    EXPECT_EQ(avg.at("housing"), Money(20000, Currency::EUR));
}

TEST(RollingOutflowAverage, MissingMonthsCountAsZero) {
    Journal j;
    // Only one month of data in a 3-month window → average = 600 / 3 = 200
    j.add(tx("A", year_month_day{year{2026}, month{4}, day{1}}, -60000, "housing"));

    const auto avg = rolling_outflow_average(j, year_month{year{2026}, month{4}}, 3, Currency::EUR);
    EXPECT_EQ(avg.at("housing"), Money(20000, Currency::EUR));
}

TEST(RollingOutflowAverage, RejectsZeroWindow) {
    Journal j;
    EXPECT_THROW(static_cast<void>(rolling_outflow_average(j, year_month{year{2026}, month{4}}, 0,
                                                            Currency::EUR)),
                 std::invalid_argument);
}

TEST(SuggestBudget, RoundsUpWithSafetyMargin) {
    Journal j;
    // 3-month average housing outflow = 200 €
    j.add(tx("F", year_month_day{year{2026}, month{2}, day{1}}, -20000, "housing"));
    j.add(tx("M", year_month_day{year{2026}, month{3}, day{1}}, -20000, "housing"));
    j.add(tx("A", year_month_day{year{2026}, month{4}, day{1}}, -20000, "housing"));

    const auto suggestions =
        suggest_budget(j, year_month{year{2026}, month{4}}, 3, Currency::EUR, 10, 500);
    ASSERT_EQ(suggestions.size(), 1u);
    EXPECT_EQ(suggestions[0].category_id, "housing");
    EXPECT_EQ(suggestions[0].rolling_average, Money(20000, Currency::EUR));
    // 200 € + 10% = 220 € → round up to next 5 € step = still 220 €
    EXPECT_EQ(suggestions[0].suggested_limit, Money(22000, Currency::EUR));
}

TEST(SuggestBudget, SortsByDescendingSuggestedLimit) {
    Journal j;
    // housing 200/month, groceries 50/month — 3 months each
    for (auto m : {month{2}, month{3}, month{4}}) {
        j.add(tx("h" + std::to_string(static_cast<unsigned>(m)),
                  year_month_day{year{2026}, m, day{1}}, -20000, "housing"));
        j.add(tx("g" + std::to_string(static_cast<unsigned>(m)),
                  year_month_day{year{2026}, m, day{1}}, -5000, "groceries"));
    }

    const auto suggestions =
        suggest_budget(j, year_month{year{2026}, month{4}}, 3, Currency::EUR);
    ASSERT_EQ(suggestions.size(), 2u);
    EXPECT_EQ(suggestions[0].category_id, "housing");
    EXPECT_EQ(suggestions[1].category_id, "groceries");
}

TEST(SuggestBudget, SkipsCategoriesWithZeroAverage) {
    Journal j;
    j.add(tx("A", year_month_day{year{2026}, month{4}, day{1}}, -1000, "leisure"));

    // 3-month window ending Jan 2026 → no transactions → average = 0 for leisure → skipped
    const auto suggestions =
        suggest_budget(j, year_month{year{2026}, month{1}}, 3, Currency::EUR);
    EXPECT_TRUE(suggestions.empty());
}

TEST(SuggestBudget, RejectsInvalidSafetyOrRound) {
    Journal j;
    EXPECT_THROW(static_cast<void>(suggest_budget(j, year_month{year{2026}, month{4}}, 3,
                                                    Currency::EUR, -1, 100)),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(suggest_budget(j, year_month{year{2026}, month{4}}, 3,
                                                    Currency::EUR, 0, 0)),
                 std::invalid_argument);
}

namespace {
    Transaction named_tx(std::string id, year_month_day d, std::int64_t minor,
                         std::string desc, std::string category = "") {
        std::optional<std::string> cat;
        if (!category.empty()) cat = category;
        return Transaction(std::move(id), d, "BP", Money(minor, Currency::EUR),
                           std::move(desc), std::move(cat));
    }
}

TEST(DetectRecurring, FindsMonthlySubscription) {
    Journal j;
    // ~monthly, stable amount, same description -> recurring.
    j.add(named_tx("n1", year_month_day{year{2026}, month{1}, day{15}}, -1299,
                   "NETFLIX.COM 4321", "subscriptions-leisure"));
    j.add(named_tx("n2", year_month_day{year{2026}, month{2}, day{15}}, -1299,
                   "NETFLIX.COM 9987", "subscriptions-leisure"));
    j.add(named_tx("n3", year_month_day{year{2026}, month{3}, day{14}}, -1399,
                   "NETFLIX COM 1122", "subscriptions-leisure"));
    // Noise: one-off, different label.
    j.add(named_tx("x1", year_month_day{year{2026}, month{2}, day{3}}, -8000,
                   "FNAC PARIS"));

    const auto rec = detect_recurring(j, Currency::EUR);
    ASSERT_EQ(rec.size(), 1u);
    EXPECT_EQ(rec[0].occurrences, 3u);
    EXPECT_TRUE(rec[0].average_amount.is_negative());
    EXPECT_EQ(rec[0].last_date, (year_month_day{year{2026}, month{3}, day{14}}));
    EXPECT_EQ(rec[0].next_date, (year_month_day{year{2026}, month{4}, day{14}}));
    ASSERT_TRUE(rec[0].category_id.has_value());
    EXPECT_EQ(*rec[0].category_id, "subscriptions-leisure");
}

TEST(DetectRecurring, IgnoresTooFewAndIrregular) {
    Journal j;
    // Only twice -> below default min_occurrences (3).
    j.add(named_tx("a1", year_month_day{year{2026}, month{1}, day{5}}, -1000,
                   "SPOTIFY"));
    j.add(named_tx("a2", year_month_day{year{2026}, month{2}, day{5}}, -1000,
                   "SPOTIFY"));
    // Three times but wildly irregular gaps -> not monthly.
    j.add(named_tx("b1", year_month_day{year{2026}, month{1}, day{1}}, -500,
                   "RANDOM SHOP"));
    j.add(named_tx("b2", year_month_day{year{2026}, month{1}, day{9}}, -500,
                   "RANDOM SHOP"));
    j.add(named_tx("b3", year_month_day{year{2026}, month{6}, day{20}}, -500,
                   "RANDOM SHOP"));

    EXPECT_TRUE(detect_recurring(j, Currency::EUR).empty());
}
