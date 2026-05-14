#include <gtest/gtest.h>

#include "wealthtorii/money/money.hpp"

#include <limits>
#include <vector>

using namespace wealthtorii::money;

TEST(MoneyTest, ConstructsWithMinorUnitsAndCurrency) {
    constexpr Money value{12345, Currency::EUR};

    EXPECT_EQ(value.minor_units(), 12345);
    EXPECT_EQ(value.currency(), Currency::EUR);
}

TEST(MoneyTest, ZeroFactoryCreatesZeroAmount) {
    const Money value = Money::zero(Currency::CHF);

    EXPECT_TRUE(value.is_zero());
    EXPECT_EQ(value.minor_units(), 0);
    EXPECT_EQ(value.currency(), Currency::CHF);
}

TEST(MoneyTest, AdditionWithSameCurrencySucceeds) {
    const Money lhs{1000, Currency::EUR};
    const Money rhs{250, Currency::EUR};

    EXPECT_EQ(lhs + rhs, Money(1250, Currency::EUR));
}

TEST(MoneyTest, SubtractionWithSameCurrencySucceeds) {
    const Money lhs{1000, Currency::EUR};
    const Money rhs{250, Currency::EUR};

    EXPECT_EQ(lhs - rhs, Money(750, Currency::EUR));
}

TEST(MoneyTest, AdditionWithDifferentCurrenciesThrowsCurrencyMismatch) {
    const Money lhs{1000, Currency::EUR};
    const Money rhs{250, Currency::USD};

    EXPECT_THROW(static_cast<void>(lhs + rhs), CurrencyMismatch);
}

TEST(MoneyTest, CurrencyMismatchCarriesBothCurrencies) {
    const Money lhs{1000, Currency::EUR};
    const Money rhs{250, Currency::USD};

    try {
        static_cast<void>(lhs + rhs);
        FAIL() << "expected CurrencyMismatch";
    } catch (const CurrencyMismatch& e) {
        EXPECT_EQ(e.lhs(), Currency::EUR);
        EXPECT_EQ(e.rhs(), Currency::USD);
    }
}

TEST(MoneyTest, UnaryMinusKeepsCurrency) {
    const Money value{1234, Currency::CHF};
    EXPECT_EQ(-value, Money(-1234, Currency::CHF));
}

TEST(MoneyTest, MultiplicationByScalarBothSides) {
    const Money value{1234, Currency::EUR};

    EXPECT_EQ(value * 3, Money(3702, Currency::EUR));
    EXPECT_EQ(3 * value, Money(3702, Currency::EUR));
    EXPECT_EQ(value * -2, Money(-2468, Currency::EUR));
}

TEST(MoneyTest, IsNegativeReportsSign) {
    EXPECT_TRUE(Money(-1, Currency::EUR).is_negative());
    EXPECT_FALSE(Money(0, Currency::EUR).is_negative());
    EXPECT_FALSE(Money(1, Currency::EUR).is_negative());
}

TEST(MoneyTest, ToStringFormatsAmountWithTwoDecimals) {
    EXPECT_EQ(to_string(Money(12345, Currency::EUR)), "123.45 EUR");
    EXPECT_EQ(to_string(Money(-987, Currency::USD)), "-9.87 USD");
    EXPECT_EQ(to_string(Money(-50, Currency::EUR)), "-0.50 EUR");
}

// --- from_string ---

TEST(MoneyFromString, ParsesDotDecimalWithSuffix) {
    const auto m = Money::from_string("12.34 EUR");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(*m, Money(1234, Currency::EUR));
}

TEST(MoneyFromString, ParsesCommaDecimalWithSuffix) {
    const auto m = Money::from_string("12,34 EUR");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(*m, Money(1234, Currency::EUR));
}

TEST(MoneyFromString, ParsesNegativeAmount) {
    const auto m = Money::from_string("-1,50 USD");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(*m, Money(-150, Currency::USD));
}

TEST(MoneyFromString, ParsesAmountWithoutDecimals) {
    const auto m = Money::from_string("42 EUR");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(*m, Money(4200, Currency::EUR));
}

TEST(MoneyFromString, UsesFallbackCurrencyWhenNoneProvided) {
    const auto m = Money::from_string("12,34", Currency::CHF);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(*m, Money(1234, Currency::CHF));
}

TEST(MoneyFromString, AcceptsSpaceThousandsSeparator) {
    const auto m = Money::from_string("1 234,56 EUR");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(*m, Money(123456, Currency::EUR));
}

TEST(MoneyFromString, AcceptsNonBreakingSpaceThousandsSeparator) {
    const auto m = Money::from_string("1\xC2\xA0""234,56 EUR");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(*m, Money(123456, Currency::EUR));
}

TEST(MoneyFromString, RejectsSubCentPrecision) {
    EXPECT_FALSE(Money::from_string("12,345 EUR").has_value());
}

TEST(MoneyFromString, RejectsMultipleDecimalSeparators) {
    EXPECT_FALSE(Money::from_string("1,2,3 EUR").has_value());
}

TEST(MoneyFromString, RejectsGarbage) {
    EXPECT_FALSE(Money::from_string("abc").has_value());
    EXPECT_FALSE(Money::from_string("").has_value());
    EXPECT_FALSE(Money::from_string("   ").has_value());
}

TEST(MoneyFromString, IgnoresUnknownCurrencySuffix) {
    // "JPY" not supported — currency suffix is not consumed, falls back to default.
    // But the remaining text "12,34 JPY" is then parsed and the trailing "JPY" is not a digit
    // so parsing fails. This documents the conservative behavior.
    EXPECT_FALSE(Money::from_string("12,34 JPY").has_value());
}

// --- split_proportional ---

TEST(MoneySplit, ProportionalExactDivision) {
    const auto parts = split_proportional(Money(10000, Currency::EUR), {50, 30, 20});
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], Money(5000, Currency::EUR));
    EXPECT_EQ(parts[1], Money(3000, Currency::EUR));
    EXPECT_EQ(parts[2], Money(2000, Currency::EUR));
}

TEST(MoneySplit, ProportionalResidualDistributedToLargestRemainders) {
    // 100 minor units split 1/1/1 → 33 + 33 + 33 = 99, residual = 1 goes to first part.
    const auto parts = split_proportional(Money(100, Currency::EUR), {1, 1, 1});
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0].minor_units() + parts[1].minor_units() + parts[2].minor_units(), 100);
}

TEST(MoneySplit, ProportionalPreservesTotalAcrossManyCases) {
    const std::vector<std::int64_t> weights{50, 30, 20};
    for (std::int64_t total : {1, 7, 99, 100, 12345, 999999}) {
        const auto parts = split_proportional(Money(total, Currency::EUR), weights);
        std::int64_t sum = 0;
        for (const auto& p : parts) sum += p.minor_units();
        EXPECT_EQ(sum, total) << "failed conservation for total=" << total;
    }
}

TEST(MoneySplit, ProportionalNegativeAmountPreservesSign) {
    const auto parts = split_proportional(Money(-100, Currency::EUR), {1, 1, 1});
    std::int64_t sum = 0;
    for (const auto& p : parts) {
        EXPECT_LE(p.minor_units(), 0);
        sum += p.minor_units();
    }
    EXPECT_EQ(sum, -100);
}

TEST(MoneySplit, ProportionalRejectsEmptyWeights) {
    EXPECT_THROW(static_cast<void>(split_proportional(Money(100, Currency::EUR), {})),
                 std::invalid_argument);
}

TEST(MoneySplit, ProportionalRejectsNonPositiveWeights) {
    EXPECT_THROW(static_cast<void>(split_proportional(Money(100, Currency::EUR), {1, 0, 2})),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(split_proportional(Money(100, Currency::EUR), {1, -1, 2})),
                 std::invalid_argument);
}
