#include <gtest/gtest.h>

#include "wealthtorii/ledger/journal.hpp"

#include <chrono>

using namespace wealthtorii::ledger;
using wealthtorii::money::Currency;
using wealthtorii::money::Money;
using std::chrono::day;
using std::chrono::month;
using std::chrono::year;
using std::chrono::year_month;
using std::chrono::year_month_day;

namespace {
    Transaction make(std::string id, year_month_day date, std::string account,
                     std::int64_t minor, std::string desc,
                     std::optional<std::string> category = std::nullopt) {
        return Transaction(std::move(id), date, std::move(account),
                           Money(minor, Currency::EUR), std::move(desc),
                           std::move(category));
    }

    constexpr year_month kMay2026{year{2026}, month{5}};
    constexpr year_month_day kMay01{year{2026}, month{5}, day{1}};
    constexpr year_month_day kMay20{year{2026}, month{5}, day{20}};
    constexpr year_month_day kJun01{year{2026}, month{6}, day{1}};
}

TEST(JournalTest, EmptyByDefault) {
    const Journal j;
    EXPECT_TRUE(j.empty());
    EXPECT_EQ(j.size(), 0u);
}

TEST(JournalTest, AddIncreasesSize) {
    Journal j;
    j.add(make("T1", kMay01, "A", -4250, "TOTAL", "transport"));
    j.add(make("T2", kMay20, "A", 180000, "SALAIRE"));
    EXPECT_EQ(j.size(), 2u);
}

TEST(JournalTest, FiltersByMonth) {
    Journal j;
    j.add(make("T1", kMay01, "A", -1000, "x"));
    j.add(make("T2", kMay20, "A", -2000, "x"));
    j.add(make("T3", kJun01, "A", -3000, "x"));

    EXPECT_EQ(j.for_month(kMay2026).size(), 2u);
    EXPECT_EQ(j.for_month(year_month{year{2026}, month{6}}).size(), 1u);
}

TEST(JournalTest, FiltersByAccountAndCategory) {
    Journal j;
    j.add(make("T1", kMay01, "BP", -1000, "x", "groceries"));
    j.add(make("T2", kMay20, "BP", -2000, "x", "transport"));
    j.add(make("T3", kMay20, "BROKER", -3000, "x", "transport"));

    EXPECT_EQ(j.for_account("BP").size(), 2u);
    EXPECT_EQ(j.for_category("transport").size(), 2u);
    EXPECT_EQ(j.for_category("groceries").size(), 1u);
    EXPECT_EQ(j.for_category("unknown").size(), 0u);
}

TEST(JournalTest, TotalForMonthAddsInflowsAndOutflows) {
    Journal j;
    j.add(make("S", kMay01, "BP", 200000, "SALAIRE"));
    j.add(make("R", kMay20, "BP", -85000, "LOYER", "housing"));
    j.add(make("C", kMay20, "BP", -12000, "COURSES", "groceries"));

    EXPECT_EQ(j.total_for_month(kMay2026, Currency::EUR),
              Money(200000 - 85000 - 12000, Currency::EUR));
}

TEST(JournalTest, OutflowByCategoryStoresPositiveMagnitudes) {
    Journal j;
    j.add(make("S", kMay01, "BP", 200000, "SALAIRE"));
    j.add(make("R", kMay20, "BP", -85000, "LOYER", "housing"));
    j.add(make("C1", kMay20, "BP", -12000, "COURSES", "groceries"));
    j.add(make("C2", kMay20, "BP", -3000, "COURSES", "groceries"));
    j.add(make("U", kMay20, "BP", -1500, "INCONNU"));

    const auto breakdown = j.outflow_by_category(kMay2026, Currency::EUR);
    EXPECT_EQ(breakdown.at("housing"), Money(85000, Currency::EUR));
    EXPECT_EQ(breakdown.at("groceries"), Money(15000, Currency::EUR));
    EXPECT_EQ(breakdown.at(""), Money(1500, Currency::EUR));
    EXPECT_EQ(breakdown.count("nonexistent"), 0u);
}

TEST(JournalTest, InflowForMonthIgnoresOutflows) {
    Journal j;
    j.add(make("S", kMay01, "BP", 200000, "SALAIRE"));
    j.add(make("B", kMay20, "BP", 5000, "BONUS"));
    j.add(make("R", kMay20, "BP", -85000, "LOYER", "housing"));

    EXPECT_EQ(j.inflow_for_month(kMay2026, Currency::EUR),
              Money(205000, Currency::EUR));
}

TEST(JournalTest, IgnoresOtherMonthsInAggregates) {
    Journal j;
    j.add(make("J", kJun01, "BP", -99999, "next month", "housing"));

    EXPECT_EQ(j.total_for_month(kMay2026, Currency::EUR),
              Money::zero(Currency::EUR));
    EXPECT_TRUE(j.outflow_by_category(kMay2026, Currency::EUR).empty());
}
