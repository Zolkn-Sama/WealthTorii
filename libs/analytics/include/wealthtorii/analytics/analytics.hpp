#pragma once

#include <wealthtorii/ledger/journal.hpp>
#include <wealthtorii/money/money.hpp>

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace wealthtorii::analytics {

    // One row per month observed in the journal (sorted ascending).
    struct MonthlyTotals {
        std::chrono::year_month month;
        money::Money inflow;
        money::Money outflow; // positive magnitude
        money::Money net;     // inflows - outflows
    };

    // Computes monthly totals over the entire journal for the given currency.
    // Months with no activity are omitted (no gap-filling).
    [[nodiscard]] std::vector<MonthlyTotals> monthly_totals(const ledger::Journal& journal,
                                                             money::Currency currency);

    // category_id -> {month -> outflow magnitude}. Uncategorised outflows are aggregated under "".
    [[nodiscard]] std::map<std::string, std::map<std::chrono::year_month, money::Money>>
    outflow_by_category_by_month(const ledger::Journal& journal, money::Currency currency);

    // Rolling average outflow per category over the `window_months` months ending at `ending`
    // (inclusive). Months without data count as zero, so a 3-month average over a partial history
    // can dilute itself toward zero — caller decides what to do.
    //
    // The result is positive (outflow magnitude) for each category seen.
    [[nodiscard]] std::map<std::string, money::Money> rolling_outflow_average(
        const ledger::Journal& journal, std::chrono::year_month ending,
        unsigned window_months, money::Currency currency);

    struct BudgetSuggestion {
        std::string category_id;
        money::Money rolling_average; // basis used
        money::Money suggested_limit; // round-up applied
    };

    // Suggests per-category budgets based on the trailing `window_months` ending at `ending`.
    // The suggestion is `ceil(rolling_average * (1 + safety_ratio_pct/100) / round_to) * round_to`.
    // Default safety_ratio_pct = 10 (10% headroom), round_to = 500 minor units (5 €).
    [[nodiscard]] std::vector<BudgetSuggestion> suggest_budget(
        const ledger::Journal& journal,
        std::chrono::year_month ending,
        unsigned window_months,
        money::Currency currency,
        std::int64_t safety_ratio_pct = 10,
        std::int64_t round_to_minor = 500);

} // namespace wealthtorii::analytics
