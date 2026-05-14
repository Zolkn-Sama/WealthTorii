#pragma once

#include "category.hpp"

#include <wealthtorii/ledger/journal.hpp>
#include <wealthtorii/money/money.hpp>

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wealthtorii::budget {

    // A monthly budget : limit (positive Money) per category id, all in the same currency.
    class Budget {
    public:
        Budget(std::chrono::year_month month, money::Currency currency);

        [[nodiscard]] std::chrono::year_month month() const noexcept { return month_; }
        [[nodiscard]] money::Currency currency() const noexcept { return currency_; }

        // Sets or replaces the limit for a category. Limit must be non-negative and use the
        // budget currency.
        void set_limit(std::string category_id, money::Money limit);

        [[nodiscard]] std::optional<money::Money> limit_for(std::string_view category_id) const;

        // Sum of every category limit.
        [[nodiscard]] money::Money total_limit() const;

        [[nodiscard]] const std::map<std::string, money::Money>& limits() const noexcept {
            return limits_;
        }

    private:
        std::chrono::year_month month_;
        money::Currency currency_;
        std::map<std::string, money::Money> limits_;
    };

    struct BudgetLine {
        std::string category_id;
        money::Money spent;    // positive magnitude of outflows
        money::Money budgeted; // limit (0 if none)
        money::Money delta;    // budgeted - spent (positive = under budget, negative = over)
    };

    // Compares the budget limits against the journal's outflows in budget.month(). One line per
    // category that either has a limit or any outflow. Uncategorised outflows appear with
    // category_id="" and budgeted=0.
    [[nodiscard]] std::vector<BudgetLine> spending_vs_budget(const Budget& budget,
                                                             const ledger::Journal& journal);

} // namespace wealthtorii::budget
