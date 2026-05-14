#include <gtest/gtest.h>

#include "budget_config.hpp"

#include <sstream>

using namespace wealthtorii::cli;
using wealthtorii::money::Currency;
using wealthtorii::money::Money;

TEST(BudgetConfig, RoundTripsThroughStreams) {
    BudgetConfig cfg;
    cfg.currency = Currency::EUR;
    cfg.limits["housing"] = Money(60000, Currency::EUR);
    cfg.limits["groceries"] = Money(30050, Currency::EUR);

    std::ostringstream out;
    write_budget_config(out, cfg);

    std::istringstream in(out.str());
    const auto reloaded = parse_budget_config(in);

    EXPECT_EQ(reloaded.currency, Currency::EUR);
    EXPECT_EQ(reloaded.limits.at("housing"), Money(60000, Currency::EUR));
    EXPECT_EQ(reloaded.limits.at("groceries"), Money(30050, Currency::EUR));
}

TEST(BudgetConfig, IgnoresCommentsAndBlankLines) {
    std::istringstream in(
        "# my config\n"
        "\n"
        "currency=EUR\n"
        "# trailing comment\n"
        "housing=600,00\n");
    const auto cfg = parse_budget_config(in);
    EXPECT_EQ(cfg.currency, Currency::EUR);
    EXPECT_EQ(cfg.limits.at("housing"), Money(60000, Currency::EUR));
}

TEST(BudgetConfig, RejectsLineWithoutEquals) {
    std::istringstream in("housing 600\n");
    EXPECT_THROW(static_cast<void>(parse_budget_config(in)), std::runtime_error);
}

TEST(BudgetConfig, RejectsUnknownCurrency) {
    std::istringstream in("currency=JPY\n");
    EXPECT_THROW(static_cast<void>(parse_budget_config(in)), std::runtime_error);
}

TEST(BudgetConfig, AcceptsBothFrenchAndEnglishAmountFormats) {
    std::istringstream in(
        "currency=EUR\n"
        "housing=600,00\n"
        "groceries=300.50\n");
    const auto cfg = parse_budget_config(in);
    EXPECT_EQ(cfg.limits.at("housing"), Money(60000, Currency::EUR));
    EXPECT_EQ(cfg.limits.at("groceries"), Money(30050, Currency::EUR));
}
