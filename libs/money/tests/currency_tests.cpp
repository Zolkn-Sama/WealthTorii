//
// Created by Enzo Landrecy on 20/03/2026.
//

#include <gtest/gtest.h>
#include "wealthtorii/money/currency.hpp"

using namespace wealthtorii::money;

TEST(CurrencyTest, ToStringReturnsIsoCode) {
    EXPECT_EQ(to_string(Currency::EUR), "EUR");
    EXPECT_EQ(to_string(Currency::USD), "USD");
    EXPECT_EQ(to_string(Currency::CHF), "CHF");
}

TEST(CurrencyTest, CurrencyFromStringParsesKnownCurrency) {
    const auto eur = currency_from_string("EUR");
    const auto usd = currency_from_string("USD");

    ASSERT_TRUE(eur.has_value());
    ASSERT_TRUE(usd.has_value());

    EXPECT_EQ(*eur, Currency::EUR);
    EXPECT_EQ(*usd, Currency::USD);
}

TEST(CurrencyTest, CurrencyFromStringReturnsNulloptForUnknownValue) {
    const auto result = currency_from_string("JPY");

    EXPECT_FALSE(result.has_value());
}