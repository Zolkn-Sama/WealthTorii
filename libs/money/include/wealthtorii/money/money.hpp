#pragma once

#include "currency.hpp"

#include <cstdint>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wealthtorii::money {

    class CurrencyMismatch : public std::runtime_error {
    public:
        CurrencyMismatch(Currency lhs, Currency rhs);

        [[nodiscard]] Currency lhs() const noexcept { return lhs_; }
        [[nodiscard]] Currency rhs() const noexcept { return rhs_; }

    private:
        Currency lhs_;
        Currency rhs_;
    };

    class Money {
    public:
        constexpr Money() noexcept
            : minor_units_(0), currency_(Currency::EUR) {}

        constexpr Money(std::int64_t minor_units, Currency currency) noexcept
            : minor_units_(minor_units), currency_(currency) {}

        [[nodiscard]] constexpr std::int64_t minor_units() const noexcept {
            return minor_units_;
        }

        [[nodiscard]] constexpr Currency currency() const noexcept {
            return currency_;
        }

        [[nodiscard]] constexpr bool is_zero() const noexcept {
            return minor_units_ == 0;
        }

        [[nodiscard]] constexpr bool is_negative() const noexcept {
            return minor_units_ < 0;
        }

        [[nodiscard]] static constexpr Money zero(Currency currency) noexcept {
            return Money{0, currency};
        }

        // Parses textual amounts in FR or EN format with an optional ISO currency suffix.
        // Accepts:
        //   "12,34 EUR" / "12.34 EUR" / "-1 234,56 EUR" / "1234.56" (defaults to fallback currency)
        // Thousands separators tolerated: space (0x20), non-breaking space (U+00A0 utf-8: 0xC2 0xA0),
        //   narrow non-breaking space (U+202F utf-8: 0xE2 0x80 0xAF). Returns nullopt on malformed input.
        [[nodiscard]] static std::optional<Money> from_string(
            std::string_view text, Currency fallback_currency = Currency::EUR) noexcept;

        Money& operator+=(const Money& other);
        Money& operator-=(const Money& other);
        Money& operator*=(std::int64_t factor) noexcept;

        [[nodiscard]] friend bool operator==(const Money& lhs, const Money& rhs) noexcept = default;

    private:
        std::int64_t minor_units_;
        Currency currency_;
    };

    [[nodiscard]] Money operator+(Money lhs, const Money& rhs);
    [[nodiscard]] Money operator-(Money lhs, const Money& rhs);
    [[nodiscard]] Money operator-(const Money& value) noexcept;
    [[nodiscard]] Money operator*(Money lhs, std::int64_t factor) noexcept;
    [[nodiscard]] Money operator*(std::int64_t factor, Money rhs) noexcept;

    // Splits an amount proportionally to the given integer weights, preserving every minor unit.
    // The residual (caused by integer division) is distributed one unit at a time onto the parts
    // with the highest fractional remainder, starting from the largest weight. Throws std::invalid_argument
    // on empty/zero/negative weights.
    [[nodiscard]] std::vector<Money> split_proportional(
        const Money& amount, const std::vector<std::int64_t>& weights);

    [[nodiscard]] std::string to_string(const Money& value);
    std::ostream& operator<<(std::ostream& os, const Money& value);

} // namespace wealthtorii::money
