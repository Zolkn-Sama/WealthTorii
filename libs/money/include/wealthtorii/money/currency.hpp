//
// Created by Enzo Landrecy on 20/03/2026.
//

#pragma once

#include <optional>
#include <ostream>
#include <string_view>

namespace wealthtorii::money {

    enum class Currency {
        EUR,
        USD,
        CHF
    };

    std::string_view to_string(Currency currency) noexcept;
    std::optional<Currency> currency_from_string(std::string_view value) noexcept;

    std::ostream& operator<<(std::ostream& os, Currency currency);

} // namespace wealthtorii::money