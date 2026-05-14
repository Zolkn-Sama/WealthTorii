#pragma once

#include <wealthtorii/money/currency.hpp>

#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace wealthtorii::ledger {

    enum class AccountType {
        CASH,
        BROKERAGE,
        CRYPTO,
        SAVINGS,
        EXTERNAL
    };

    [[nodiscard]] std::string_view to_string(AccountType type) noexcept;
    [[nodiscard]] std::optional<AccountType> account_type_from_string(std::string_view value) noexcept;

    class Account {
    public:
        Account();
        Account(std::string id,
                std::string name,
                money::Currency currency,
                AccountType type,
                bool is_active = true);

        [[nodiscard]] const std::string& id() const noexcept { return id_; }
        [[nodiscard]] const std::string& name() const noexcept { return name_; }
        [[nodiscard]] money::Currency currency() const noexcept { return currency_; }
        [[nodiscard]] AccountType type() const noexcept { return type_; }
        [[nodiscard]] bool is_active() const noexcept { return is_active_; }

        void activate() noexcept;
        void deactivate() noexcept;

        [[nodiscard]] friend bool operator==(const Account& lhs, const Account& rhs) noexcept = default;

    private:
        std::string id_;
        std::string name_;
        money::Currency currency_;
        AccountType type_;
        bool is_active_;
    };

    [[nodiscard]] std::string to_string(const Account& account);
    std::ostream& operator<<(std::ostream& os, const Account& account);

} // namespace wealthtorii::ledger
