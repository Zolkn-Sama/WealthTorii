#pragma once

#include <wealthtorii/money/money.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace wealthtorii::portfolio {

    // Quantities are scaled by 1e6 (micro-units) so fractional shares / crypto
    // are representable as integers.
    inline constexpr std::int64_t kQtyScale = 1'000'000;

    // An aggregated (average-cost) holding. cost_minor is the total cost basis
    // in minor units of `currency`.
    struct Position {
        std::string id;
        std::string account_id; // optional grouping label, may be empty
        std::string symbol;
        std::int64_t quantity_micro = 0;
        std::int64_t cost_minor = 0;
        money::Currency currency = money::Currency::EUR;
    };

    // Latest manual price for a symbol, per minor unit of `currency`.
    struct Price {
        std::string symbol;
        std::int64_t price_minor = 0;
        money::Currency currency = money::Currency::EUR;
    };

    struct Valuation {
        std::string id;
        std::string account_id;
        std::string symbol;
        std::int64_t quantity_micro = 0;
        money::Money cost;          // cost basis
        money::Money market_value;  // quantity * price (== cost when unpriced)
        money::Money unrealized;    // market_value - cost
        double return_pct = 0.0;    // unrealized / cost * 100
        bool priced = false;        // a matching, same-currency price existed
    };

    // Values each position against the price map (keyed by symbol). When no
    // same-currency price is known, market_value falls back to cost and
    // priced=false (so callers can flag stale holdings).
    [[nodiscard]] std::vector<Valuation> value_positions(
        const std::vector<Position>& positions,
        const std::map<std::string, Price>& prices);

    struct PortfolioTotals {
        money::Money cost;
        money::Money market_value;
        money::Money unrealized;
    };

    // Per-currency totals across the given valuations.
    [[nodiscard]] std::map<money::Currency, PortfolioTotals> totals_by_currency(
        const std::vector<Valuation>& valuations);

} // namespace wealthtorii::portfolio
