#include <gtest/gtest.h>

#include "wealthtorii/portfolio/portfolio.hpp"

using namespace wealthtorii::portfolio;
using wealthtorii::money::Currency;
using wealthtorii::money::Money;

namespace {
    Position pos(std::string sym, std::int64_t qtyMicro, std::int64_t cost) {
        Position p;
        p.id = sym;
        p.symbol = sym;
        p.quantity_micro = qtyMicro;
        p.cost_minor = cost;
        p.currency = Currency::EUR;
        return p;
    }
}

TEST(ValuePositions, PricedGainAndLoss) {
    std::vector<Position> ps = {
        pos("AAPL", 10 * kQtyScale, 150000),  // 10 @ cost 1500.00
        pos("BTC", kQtyScale / 2, 1000000),   // 0.5 @ cost 10000.00
    };
    std::map<std::string, Price> prices = {
        {"AAPL", Price{"AAPL", 18000, Currency::EUR}},  // 180.00 -> 1800.00
        {"BTC", Price{"BTC", 1600000, Currency::EUR}},  // 16000.00 -> 8000.00
    };
    const auto v = value_positions(ps, prices);
    ASSERT_EQ(v.size(), 2u);

    EXPECT_TRUE(v[0].priced);
    EXPECT_EQ(v[0].market_value, Money(180000, Currency::EUR));
    EXPECT_EQ(v[0].unrealized, Money(30000, Currency::EUR));   // +300.00
    EXPECT_NEAR(v[0].return_pct, 20.0, 1e-9);

    EXPECT_EQ(v[1].market_value, Money(800000, Currency::EUR));
    EXPECT_EQ(v[1].unrealized, Money(-200000, Currency::EUR));  // -2000.00
    EXPECT_NEAR(v[1].return_pct, -20.0, 1e-9);
}

TEST(ValuePositions, UnpricedFallsBackToCost) {
    std::vector<Position> ps = {pos("XYZ", 3 * kQtyScale, 90000)};
    const auto v = value_positions(ps, {});
    ASSERT_EQ(v.size(), 1u);
    EXPECT_FALSE(v[0].priced);
    EXPECT_EQ(v[0].market_value, v[0].cost);
    EXPECT_EQ(v[0].unrealized, Money(0, Currency::EUR));
}

TEST(TotalsByCurrency, Aggregates) {
    std::vector<Position> ps = {
        pos("A", kQtyScale, 10000),
        pos("B", kQtyScale, 20000),
    };
    std::map<std::string, Price> prices = {
        {"A", Price{"A", 12000, Currency::EUR}},  // 120.00
        {"B", Price{"B", 25000, Currency::EUR}},  // 250.00
    };
    const auto t = totals_by_currency(value_positions(ps, prices));
    ASSERT_EQ(t.size(), 1u);
    const auto& eur = t.at(Currency::EUR);
    EXPECT_EQ(eur.cost, Money(30000, Currency::EUR));
    EXPECT_EQ(eur.market_value, Money(37000, Currency::EUR));
    EXPECT_EQ(eur.unrealized, Money(7000, Currency::EUR));
}
