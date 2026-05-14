#include <gtest/gtest.h>

#include "wealthtorii/budget/allocation.hpp"
#include "wealthtorii/budget/category.hpp"
#include "wealthtorii/import/bp_csv.hpp"
#include "wealthtorii/ledger/journal.hpp"

#include <chrono>
#include <fstream>
#include <iostream>

#ifndef WEALTHTORII_DATA_CSV
#define WEALTHTORII_DATA_CSV ""
#endif

using namespace wealthtorii::import_;
using wealthtorii::budget::allocate_50_30_20;
using wealthtorii::budget::Bucket;
using wealthtorii::budget::default_registry;
using wealthtorii::ledger::Journal;
using wealthtorii::money::Currency;
using wealthtorii::money::Money;

namespace {
    bool data_file_available() {
        std::ifstream f(WEALTHTORII_DATA_CSV);
        return f.good();
    }
}

class BpRealData : public ::testing::Test {
protected:
    void SetUp() override {
        if (!data_file_available()) {
            GTEST_SKIP() << "DATA.csv not available at " << WEALTHTORII_DATA_CSV;
        }
    }
};

TEST_F(BpRealData, ParsesEntireFile) {
    std::ifstream f(WEALTHTORII_DATA_CSV);
    ASSERT_TRUE(f.good());

    ImportOptions opts;
    opts.account_id = "BP_CHECKING";
    const auto overrides = default_overrides();
    opts.overrides = &overrides;

    const auto report = import_bp_csv(f, opts);

    EXPECT_GT(report.rows_seen, 200u);
    EXPECT_EQ(report.rows_seen, report.transactions.size() + report.rows_dropped);

    // The mapping (BP categories + default overrides) should leave very few rows uncategorised.
    // We accept up to 15% uncategorised in the worst case (mostly "A categoriser" transfers).
    const double uncategorised_ratio =
        static_cast<double>(report.uncategorised) /
        static_cast<double>(std::max<std::size_t>(1, report.transactions.size()));
    EXPECT_LT(uncategorised_ratio, 0.15) << "too many uncategorised: "
                                          << report.uncategorised << "/"
                                          << report.transactions.size();
}

TEST_F(BpRealData, MonthlySummaryAprilHasInflowsAndOutflows) {
    std::ifstream f(WEALTHTORII_DATA_CSV);
    ImportOptions opts;
    opts.account_id = "BP_CHECKING";
    const auto overrides = default_overrides();
    opts.overrides = &overrides;
    const auto report = import_bp_csv(f, opts);

    Journal j;
    for (const auto& tx : report.transactions) {
        j.add(tx);
    }

    const std::chrono::year_month april2026{std::chrono::year{2026}, std::chrono::month{4}};

    const auto inflow = j.inflow_for_month(april2026, Currency::EUR);
    EXPECT_GT(inflow.minor_units(), 0);

    const auto net = j.total_for_month(april2026, Currency::EUR);
    // net is (inflows - outflows). Either sign is fine for a real month — we just want it computed.
    EXPECT_NE(net.minor_units(), inflow.minor_units())
        << "outflows appear to be zero, which is implausible for a real account";

    const auto by_cat = j.outflow_by_category(april2026, Currency::EUR);
    EXPECT_FALSE(by_cat.empty());
}

// Diagnostic: print a short summary so the developer can eyeball the import quality.
TEST_F(BpRealData, PrintsHumanReadableSummary) {
    std::ifstream f(WEALTHTORII_DATA_CSV);
    ImportOptions opts;
    opts.account_id = "BP_CHECKING";
    const auto overrides = default_overrides();
    opts.overrides = &overrides;
    const auto report = import_bp_csv(f, opts);

    Journal j;
    for (const auto& tx : report.transactions) {
        j.add(tx);
    }

    std::cout << "\n=== Import summary ===\n"
              << "rows seen      : " << report.rows_seen << "\n"
              << "rows dropped   : " << report.rows_dropped << "\n"
              << "transactions   : " << report.transactions.size() << "\n"
              << "categorised    : " << report.categorised << "\n"
              << "uncategorised  : " << report.uncategorised << "\n";

    // Per-month aggregates for every month present.
    std::map<std::chrono::year_month, Money> net_by_month;
    std::map<std::chrono::year_month, Money> in_by_month;
    std::map<std::chrono::year_month, Money> out_by_month;
    for (const auto& tx : report.transactions) {
        const std::chrono::year_month ym{tx.date().year(), tx.date().month()};
        auto& net = net_by_month.try_emplace(ym, Money::zero(Currency::EUR)).first->second;
        net += tx.amount();
        if (tx.is_inflow()) {
            auto& in = in_by_month.try_emplace(ym, Money::zero(Currency::EUR)).first->second;
            in += tx.amount();
        } else {
            auto& out = out_by_month.try_emplace(ym, Money::zero(Currency::EUR)).first->second;
            out += -tx.amount();
        }
    }

    std::cout << "\n=== Monthly summary ===\n";
    std::cout << "month     in           out          net\n";
    for (const auto& [ym, net] : net_by_month) {
        std::cout << static_cast<int>(ym.year()) << '-'
                  << static_cast<unsigned>(ym.month()) << "   "
                  << in_by_month[ym] << "   "
                  << out_by_month[ym] << "   "
                  << net << "\n";
    }
    std::cout << std::endl;
    SUCCEED();
}
