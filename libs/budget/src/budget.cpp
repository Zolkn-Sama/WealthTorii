#include "wealthtorii/budget/budget.hpp"

#include <set>
#include <stdexcept>
#include <utility>

namespace wealthtorii::budget {

    Budget::Budget(std::chrono::year_month month, money::Currency currency)
        : month_(month), currency_(currency) {
        if (!month_.ok()) {
            throw std::invalid_argument("Budget.month must be a valid year_month");
        }
    }

    void Budget::set_limit(std::string category_id, money::Money limit) {
        if (category_id.empty()) {
            throw std::invalid_argument("Budget.set_limit: category_id cannot be empty");
        }
        if (limit.is_negative()) {
            throw std::invalid_argument("Budget.set_limit: limit must be non-negative");
        }
        if (limit.currency() != currency_) {
            throw money::CurrencyMismatch(currency_, limit.currency());
        }
        limits_[std::move(category_id)] = limit;
    }

    std::optional<money::Money> Budget::limit_for(std::string_view category_id) const {
        const auto it = limits_.find(std::string(category_id));
        if (it == limits_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    money::Money Budget::total_limit() const {
        money::Money total = money::Money::zero(currency_);
        for (const auto& [_, limit] : limits_) {
            total += limit;
        }
        return total;
    }

    std::vector<BudgetLine> spending_vs_budget(const Budget& budget,
                                               const ledger::Journal& journal) {
        const auto outflows = journal.outflow_by_category(budget.month(), budget.currency());

        std::set<std::string> keys;
        for (const auto& [cat, _] : outflows) keys.insert(cat);
        for (const auto& [cat, _] : budget.limits()) keys.insert(cat);

        std::vector<BudgetLine> lines;
        lines.reserve(keys.size());
        for (const auto& key : keys) {
            const auto spent_it = outflows.find(key);
            const money::Money spent =
                spent_it != outflows.end() ? spent_it->second : money::Money::zero(budget.currency());
            const money::Money budgeted =
                budget.limit_for(key).value_or(money::Money::zero(budget.currency()));
            lines.push_back({key, spent, budgeted, budgeted - spent});
        }
        return lines;
    }

} // namespace wealthtorii::budget
