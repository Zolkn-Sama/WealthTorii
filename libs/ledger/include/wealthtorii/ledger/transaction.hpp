#pragma once

#include <wealthtorii/money/money.hpp>

#include <chrono>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace wealthtorii::ledger {

    // A single posted movement on an account. Sign convention:
    //   amount > 0  → inflow  (credit, salaire, virement reçu)
    //   amount < 0  → outflow (débit, dépense, virement émis)
    // Zero amounts are rejected at construction.
    //
    // Tracks two parallel categorisations:
    //   - category_id  : canonical WealthTorii category (housing, groceries, …) used for
    //                    analytics and 50/30/20 bucketing.
    //   - bp_category / bp_subcategory : the BP-style taxonomy (Alimentation / Restaurant, …)
    //                    preserved verbatim for round-tripping into CSV/XLSX exports.
    // is_reconciled mirrors the "Colonne 1" boolean from SORTED_DATA.xlsx (user-side pointage).
    class Transaction {
    public:
        Transaction(std::string id,
                    std::chrono::year_month_day date,
                    std::string account_id,
                    money::Money amount,
                    std::string description,
                    std::optional<std::string> category_id = std::nullopt,
                    std::optional<std::string> bp_category = std::nullopt,
                    std::optional<std::string> bp_subcategory = std::nullopt,
                    std::string type_operation = "",
                    bool is_reconciled = false);

        [[nodiscard]] const std::string& id() const noexcept { return id_; }
        [[nodiscard]] std::chrono::year_month_day date() const noexcept { return date_; }
        [[nodiscard]] const std::string& account_id() const noexcept { return account_id_; }
        [[nodiscard]] const money::Money& amount() const noexcept { return amount_; }
        [[nodiscard]] const std::string& description() const noexcept { return description_; }
        [[nodiscard]] const std::optional<std::string>& category_id() const noexcept { return category_id_; }
        [[nodiscard]] const std::optional<std::string>& bp_category() const noexcept { return bp_category_; }
        [[nodiscard]] const std::optional<std::string>& bp_subcategory() const noexcept { return bp_subcategory_; }
        [[nodiscard]] const std::string& type_operation() const noexcept { return type_operation_; }
        [[nodiscard]] bool is_reconciled() const noexcept { return is_reconciled_; }

        [[nodiscard]] bool is_inflow() const noexcept { return !amount_.is_negative(); }
        [[nodiscard]] bool is_outflow() const noexcept { return amount_.is_negative(); }

        void assign_category(std::string category_id);
        void clear_category() noexcept;
        void set_bp_category(std::string category, std::string subcategory);
        void set_type_operation(std::string type) noexcept { type_operation_ = std::move(type); }
        void mark_reconciled(bool flag) noexcept { is_reconciled_ = flag; }

        [[nodiscard]] friend bool operator==(const Transaction& lhs, const Transaction& rhs) noexcept = default;

    private:
        std::string id_;
        std::chrono::year_month_day date_;
        std::string account_id_;
        money::Money amount_;
        std::string description_;
        std::optional<std::string> category_id_;
        std::optional<std::string> bp_category_;
        std::optional<std::string> bp_subcategory_;
        std::string type_operation_;
        bool is_reconciled_;
    };

    [[nodiscard]] std::string to_string(const Transaction& tx);
    std::ostream& operator<<(std::ostream& os, const Transaction& tx);

} // namespace wealthtorii::ledger
