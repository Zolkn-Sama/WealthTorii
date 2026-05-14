#include "wealthtorii/analytics/analytics.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace wealthtorii::analytics {

    namespace {
        std::chrono::year_month ym_of(std::chrono::year_month_day d) noexcept {
            return std::chrono::year_month{d.year(), d.month()};
        }
    } // namespace

    std::vector<MonthlyTotals> monthly_totals(const ledger::Journal& journal,
                                              money::Currency currency) {
        std::map<std::chrono::year_month, MonthlyTotals> by_month;
        for (const auto& tx : journal.transactions()) {
            const auto ym = ym_of(tx.date());
            auto [it, inserted] = by_month.try_emplace(
                ym, MonthlyTotals{ym, money::Money::zero(currency), money::Money::zero(currency),
                                   money::Money::zero(currency)});
            if (tx.is_inflow()) {
                it->second.inflow += tx.amount();
            } else {
                it->second.outflow += -tx.amount();
            }
            it->second.net += tx.amount();
        }
        std::vector<MonthlyTotals> out;
        out.reserve(by_month.size());
        for (auto& [_, mt] : by_month) {
            out.push_back(std::move(mt));
        }
        return out;
    }

    std::map<std::string, std::map<std::chrono::year_month, money::Money>>
    outflow_by_category_by_month(const ledger::Journal& journal, money::Currency currency) {
        std::map<std::string, std::map<std::chrono::year_month, money::Money>> out;
        for (const auto& tx : journal.transactions()) {
            if (!tx.is_outflow()) continue;
            const auto ym = ym_of(tx.date());
            const auto key = tx.category_id().value_or("");
            auto& bucket = out[key];
            auto [it, inserted] = bucket.try_emplace(ym, money::Money::zero(currency));
            it->second += -tx.amount();
        }
        return out;
    }

    std::map<std::string, money::Money> rolling_outflow_average(const ledger::Journal& journal,
                                                                  std::chrono::year_month ending,
                                                                  unsigned window_months,
                                                                  money::Currency currency) {
        if (window_months == 0) {
            throw std::invalid_argument("rolling_outflow_average: window_months must be > 0");
        }
        const auto by_cat = outflow_by_category_by_month(journal, currency);

        // Build the list of months in the window.
        std::vector<std::chrono::year_month> months_in_window;
        months_in_window.reserve(window_months);
        auto cursor = ending;
        for (unsigned i = 0; i < window_months; ++i) {
            months_in_window.push_back(cursor);
            cursor = cursor - std::chrono::months{1};
        }

        std::map<std::string, money::Money> out;
        for (const auto& [cat, monthly] : by_cat) {
            money::Money sum = money::Money::zero(currency);
            for (const auto& ym : months_in_window) {
                const auto it = monthly.find(ym);
                if (it != monthly.end()) sum += it->second;
            }
            // Integer division by window_months. Loses up to (window_months - 1) minor units.
            const auto average_minor = sum.minor_units() / static_cast<std::int64_t>(window_months);
            out.emplace(cat, money::Money(average_minor, currency));
        }
        return out;
    }

    std::vector<BudgetSuggestion> suggest_budget(const ledger::Journal& journal,
                                                  std::chrono::year_month ending,
                                                  unsigned window_months,
                                                  money::Currency currency,
                                                  std::int64_t safety_ratio_pct,
                                                  std::int64_t round_to_minor) {
        if (safety_ratio_pct < 0) {
            throw std::invalid_argument("suggest_budget: safety_ratio_pct must be >= 0");
        }
        if (round_to_minor <= 0) {
            throw std::invalid_argument("suggest_budget: round_to_minor must be > 0");
        }
        const auto avg = rolling_outflow_average(journal, ending, window_months, currency);

        std::vector<BudgetSuggestion> out;
        out.reserve(avg.size());
        for (const auto& [cat, m] : avg) {
            if (m.is_zero()) continue;
            const std::int64_t base = m.minor_units();
            const std::int64_t with_safety = base * (100 + safety_ratio_pct) / 100;
            // Round up to the next multiple of round_to_minor.
            const std::int64_t rounded = ((with_safety + round_to_minor - 1) / round_to_minor)
                                          * round_to_minor;
            out.push_back(BudgetSuggestion{cat, m, money::Money(rounded, currency)});
        }
        // Stable order: largest suggested limit first.
        std::sort(out.begin(), out.end(), [](const BudgetSuggestion& a, const BudgetSuggestion& b) {
            return a.suggested_limit.minor_units() > b.suggested_limit.minor_units();
        });
        return out;
    }

} // namespace wealthtorii::analytics
