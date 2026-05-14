#include <gtest/gtest.h>

#include "wealthtorii/budget/allocation.hpp"

#include <stdexcept>

using namespace wealthtorii::budget;
using wealthtorii::money::Currency;
using wealthtorii::money::Money;

TEST(Allocation50_30_20, SimpleRoundCase) {
    const auto alloc = allocate_50_30_20(Money(200000, Currency::EUR)); // 2 000,00 €
    EXPECT_EQ(alloc.at(Bucket::NEEDS), Money(100000, Currency::EUR));
    EXPECT_EQ(alloc.at(Bucket::WANTS), Money(60000, Currency::EUR));
    EXPECT_EQ(alloc.at(Bucket::SAVINGS_INVEST), Money(40000, Currency::EUR));
}

TEST(Allocation50_30_20, PreservesTotalDownToTheCent) {
    for (auto minor : {1, 7, 99, 100, 123, 100001, 250000, 7654321}) {
        const auto alloc = allocate_50_30_20(Money(minor, Currency::EUR));
        std::int64_t sum = 0;
        for (const auto& [_, m] : alloc) sum += m.minor_units();
        EXPECT_EQ(sum, minor) << "failed conservation for income=" << minor;
    }
}

TEST(Allocation50_30_20, RejectsZeroOrNegativeIncome) {
    EXPECT_THROW(static_cast<void>(allocate_50_30_20(Money(0, Currency::EUR))),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(allocate_50_30_20(Money(-100, Currency::EUR))),
                 std::invalid_argument);
}

TEST(DistributeEvenly, SpreadsEnvelopeAcrossBucketCategories) {
    const auto reg = default_registry();
    const auto needs = distribute_evenly(Money(60000, Currency::EUR), reg, Bucket::NEEDS);

    std::int64_t sum = 0;
    for (const auto& [_, m] : needs) sum += m.minor_units();
    EXPECT_EQ(sum, 60000);
    EXPECT_FALSE(needs.empty());
    EXPECT_NE(needs.find("housing"), needs.end());
}

TEST(DistributeEvenly, EmptyResultForBucketWithoutCategories) {
    CategoryRegistry empty;
    const auto out = distribute_evenly(Money(1000, Currency::EUR), empty, Bucket::WANTS);
    EXPECT_TRUE(out.empty());
}
