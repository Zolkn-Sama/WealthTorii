//
// Created by Enzo Landrecy on 20/03/2026.
//

#include "wealthtorii/money/currency.hpp"

#include <array>

namespace wealthtorii::money {
    namespace {

        constexpr std::array<std::pair<std::string_view, Currency>, 3> kCurrencies{{
            {"EUR", Currency::EUR},
            {"USD", Currency::USD},
            {"CHF", Currency::CHF}
        }};

    } // namespace

    std::string_view to_string(const Currency currency) noexcept {
        switch (currency) {
            case Currency::EUR: return "EUR";
            case Currency::USD: return "USD";
            case Currency::CHF: return "CHF";
        }

        return "UNKNOWN";
    }

    std::optional<Currency> currency_from_string(const std::string_view value) noexcept {
        for (const auto& [code, currency] : kCurrencies) {
            if (code == value) {
                return currency;
            }
        }

        return std::nullopt;
    }

    std::ostream& operator<<(std::ostream& os, const Currency currency) {
        os << to_string(currency);
        return os;
    }

} // namespace wealthtorii::money