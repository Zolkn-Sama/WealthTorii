#include "wealthtorii/ledger/account.hpp"

#include <array>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace wealthtorii::ledger {

    namespace {

        constexpr std::array<std::pair<std::string_view, AccountType>, 5> kAccountTypes{{
            {"CASH", AccountType::CASH},
            {"BROKERAGE", AccountType::BROKERAGE},
            {"CRYPTO", AccountType::CRYPTO},
            {"SAVINGS", AccountType::SAVINGS},
            {"EXTERNAL", AccountType::EXTERNAL},
        }};

        void validate_not_empty(std::string_view value, std::string_view field_name) {
            if (value.empty()) {
                throw std::invalid_argument(std::string(field_name) + " cannot be empty");
            }
        }

    } // namespace

    std::string_view to_string(const AccountType type) noexcept {
        for (const auto& [label, t] : kAccountTypes) {
            if (t == type) {
                return label;
            }
        }
        return "UNKNOWN";
    }

    std::optional<AccountType> account_type_from_string(std::string_view value) noexcept {
        for (const auto& [label, t] : kAccountTypes) {
            if (label == value) {
                return t;
            }
        }
        return std::nullopt;
    }

    Account::Account()
        : id_("CASH"),
          name_("CASH"),
          currency_(money::Currency::EUR),
          type_(AccountType::CASH),
          is_active_(true) {}

    Account::Account(std::string id,
                     std::string name,
                     money::Currency currency,
                     AccountType type,
                     bool is_active)
        : id_(std::move(id)),
          name_(std::move(name)),
          currency_(currency),
          type_(type),
          is_active_(is_active) {
        validate_not_empty(id_, "Account.id");
        validate_not_empty(name_, "Account.name");
    }

    void Account::activate() noexcept { is_active_ = true; }
    void Account::deactivate() noexcept { is_active_ = false; }

    std::string to_string(const Account& account) {
        std::ostringstream oss;
        oss << "id=" << account.id()
            << " name=" << account.name()
            << " currency=" << account.currency()
            << " type=" << to_string(account.type())
            << " active=" << (account.is_active() ? "true" : "false");
        return oss.str();
    }

    std::ostream& operator<<(std::ostream& os, const Account& account) {
        os << to_string(account);
        return os;
    }

} // namespace wealthtorii::ledger
