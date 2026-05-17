#include "wealthtorii/analytics/analytics.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <unordered_map>

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

    namespace {
        // Lowercase, keep [a-z] only, collapse runs to single spaces, trim,
        // cap length — so "PRLV SEPA NETFLIX 4321" and "NETFLIX.COM 9987"
        // collapse toward a stable key.
        std::string normalise(const std::string& desc) {
            std::string n;
            n.reserve(desc.size());
            bool space = true; // start trimmed
            for (const char raw : desc) {
                const auto c = static_cast<unsigned char>(raw);
                const char lo = static_cast<char>(std::tolower(c));
                if (lo >= 'a' && lo <= 'z') {
                    n.push_back(lo);
                    space = false;
                } else if (!space) {
                    n.push_back(' ');
                    space = true;
                }
            }
            while (!n.empty() && n.back() == ' ') {
                n.pop_back();
            }
            if (n.size() > 24) {
                n.resize(24);
            }
            return n;
        }

        std::chrono::year_month_day add_one_month(
            std::chrono::year_month_day d) {
            const auto ym =
                std::chrono::year_month{d.year(), d.month()} +
                std::chrono::months{1};
            std::chrono::year_month_day cand{ym.year(), ym.month(), d.day()};
            if (!cand.ok()) {
                return std::chrono::year_month_day{
                    ym.year() / ym.month() / std::chrono::last};
            }
            return cand;
        }

        std::int64_t days_between(std::chrono::year_month_day a,
                                  std::chrono::year_month_day b) {
            return (std::chrono::sys_days{b} - std::chrono::sys_days{a}).count();
        }
    } // namespace

    std::vector<RecurringItem> detect_recurring(const ledger::Journal& journal,
                                                money::Currency currency,
                                                unsigned min_occurrences) {
        struct Entry {
            std::chrono::year_month_day date;
            std::int64_t minor;
            std::optional<std::string> category;
        };
        std::unordered_map<std::string, std::vector<Entry>> groups;
        std::unordered_map<std::string, std::string> label_of;

        for (const auto& tx : journal.transactions()) {
            if (tx.amount().currency() != currency) {
                continue;
            }
            const auto key = normalise(tx.description());
            if (key.empty()) {
                continue;
            }
            groups[key].push_back(
                Entry{tx.date(), tx.amount().minor_units(), tx.category_id()});
            label_of.try_emplace(key, tx.description());
        }

        std::vector<RecurringItem> out;
        for (auto& [key, entries] : groups) {
            if (entries.size() < min_occurrences) {
                continue;
            }
            std::sort(entries.begin(), entries.end(),
                      [](const Entry& a, const Entry& b) {
                          return std::chrono::sys_days{a.date} <
                                 std::chrono::sys_days{b.date};
                      });

            std::vector<std::int64_t> gaps;
            gaps.reserve(entries.size() - 1);
            for (std::size_t i = 1; i < entries.size(); ++i) {
                gaps.push_back(
                    days_between(entries[i - 1].date, entries[i].date));
            }
            std::sort(gaps.begin(), gaps.end());
            const std::int64_t median = gaps[gaps.size() / 2];
            if (median < 25 || median > 35) {
                continue; // not ~monthly
            }

            std::int64_t sum = 0;
            std::int64_t lo = entries.front().minor;
            std::int64_t hi = entries.front().minor;
            bool same_sign = true;
            const bool neg0 = entries.front().minor < 0;
            for (const auto& e : entries) {
                sum += e.minor;
                lo = std::min(lo, e.minor);
                hi = std::max(hi, e.minor);
                if ((e.minor < 0) != neg0) {
                    same_sign = false;
                }
            }
            if (!same_sign) {
                continue;
            }
            const std::int64_t n =
                static_cast<std::int64_t>(entries.size());
            const std::int64_t mean = sum / n;
            const std::int64_t tol =
                std::max<std::int64_t>(std::llabs(mean) / 5, 200); // 20% or 2€
            if (hi - lo > tol) {
                continue; // amount not stable enough
            }

            // Most frequent category in the group (first wins on ties).
            std::map<std::string, int> cat_count;
            for (const auto& e : entries) {
                if (e.category.has_value()) {
                    ++cat_count[*e.category];
                }
            }
            std::optional<std::string> category;
            int best = 0;
            for (const auto& [c, cnt] : cat_count) {
                if (cnt > best) {
                    best = cnt;
                    category = c;
                }
            }

            RecurringItem item;
            item.label = label_of[key];
            item.category_id = category;
            item.average_amount = money::Money(mean, currency);
            item.occurrences = static_cast<unsigned>(entries.size());
            item.last_date = entries.back().date;
            item.next_date = add_one_month(entries.back().date);
            out.push_back(std::move(item));
        }

        std::sort(out.begin(), out.end(),
                  [](const RecurringItem& a, const RecurringItem& b) {
                      return std::chrono::sys_days{a.next_date} <
                             std::chrono::sys_days{b.next_date};
                  });
        return out;
    }

} // namespace wealthtorii::analytics
