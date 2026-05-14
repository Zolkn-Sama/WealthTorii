#pragma once

#include "transaction.hpp"

#include <wealthtorii/money/money.hpp>

#include <chrono>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wealthtorii::ledger {

    // In-memory append-only journal of transactions. Backed by a vector to keep insertion order;
    // queries scan linearly (acceptable for the personal-finance scale targeted in Phase 2 — a
    // typical user posts at most a few thousand transactions per year).
    class Journal {
    public:
        Journal() = default;

        void add(Transaction tx);

        [[nodiscard]] std::size_t size() const noexcept { return transactions_.size(); }
        [[nodiscard]] bool empty() const noexcept { return transactions_.empty(); }

        [[nodiscard]] std::span<const Transaction> transactions() const noexcept {
            return {transactions_.data(), transactions_.size()};
        }

        [[nodiscard]] std::vector<Transaction> for_month(std::chrono::year_month month) const;
        [[nodiscard]] std::vector<Transaction> for_account(std::string_view account_id) const;
        [[nodiscard]] std::vector<Transaction> for_category(std::string_view category_id) const;

        // Net total over a month (sum of all amounts). Inflows positive, outflows negative.
        // Throws money::CurrencyMismatch if transactions span multiple currencies in that month.
        [[nodiscard]] money::Money total_for_month(std::chrono::year_month month,
                                                   money::Currency currency) const;

        // Total of outflows (absolute value) per category for the given month. Uncategorised
        // outflows are aggregated under the empty key "".
        [[nodiscard]] std::map<std::string, money::Money> outflow_by_category(
            std::chrono::year_month month, money::Currency currency) const;

        // Sum of inflows for the given month.
        [[nodiscard]] money::Money inflow_for_month(std::chrono::year_month month,
                                                    money::Currency currency) const;

    private:
        std::vector<Transaction> transactions_;
    };

} // namespace wealthtorii::ledger
