#include "wealthtorii/portfolio/portfolio.hpp"

namespace wealthtorii::portfolio {

    namespace {
        // quantity (micro) * price (minor) / 1e6, rounded, overflow-safe.
        std::int64_t market_value_minor(std::int64_t qty_micro,
                                        std::int64_t price_minor) {
            const __int128 prod =
                static_cast<__int128>(qty_micro) * price_minor;
            const __int128 half = kQtyScale / 2;
            return static_cast<std::int64_t>((prod + half) / kQtyScale);
        }
    } // namespace

    std::vector<Valuation> value_positions(
        const std::vector<Position>& positions,
        const std::map<std::string, Price>& prices) {
        std::vector<Valuation> out;
        out.reserve(positions.size());
        for (const auto& p : positions) {
            Valuation v;
            v.id = p.id;
            v.account_id = p.account_id;
            v.symbol = p.symbol;
            v.quantity_micro = p.quantity_micro;
            v.cost = money::Money(p.cost_minor, p.currency);

            const auto it = prices.find(p.symbol);
            if (it != prices.end() && it->second.currency == p.currency) {
                v.priced = true;
                v.market_value = money::Money(
                    market_value_minor(p.quantity_micro,
                                       it->second.price_minor),
                    p.currency);
            } else {
                v.priced = false;
                v.market_value = v.cost;
            }
            v.unrealized = v.market_value - v.cost;
            v.return_pct =
                p.cost_minor != 0
                    ? 100.0 * static_cast<double>(v.unrealized.minor_units()) /
                          static_cast<double>(p.cost_minor)
                    : 0.0;
            out.push_back(std::move(v));
        }
        return out;
    }

    std::map<money::Currency, PortfolioTotals> totals_by_currency(
        const std::vector<Valuation>& valuations) {
        std::map<money::Currency, PortfolioTotals> totals;
        for (const auto& v : valuations) {
            const auto ccy = v.cost.currency();
            auto it = totals.find(ccy);
            if (it == totals.end()) {
                it = totals
                         .emplace(ccy,
                                  PortfolioTotals{money::Money::zero(ccy),
                                                  money::Money::zero(ccy),
                                                  money::Money::zero(ccy)})
                         .first;
            }
            it->second.cost += v.cost;
            it->second.market_value += v.market_value;
            it->second.unrealized += v.unrealized;
        }
        return totals;
    }

} // namespace wealthtorii::portfolio
