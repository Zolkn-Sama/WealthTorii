#include "wealthtorii/money/money.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>

namespace wealthtorii::money {

    CurrencyMismatch::CurrencyMismatch(Currency lhs, Currency rhs)
        : std::runtime_error(
              std::string("currency mismatch: ")
                  .append(to_string(lhs))
                  .append(" vs ")
                  .append(to_string(rhs))),
          lhs_(lhs), rhs_(rhs) {}

    namespace {

        void ensure_same_currency(const Money& lhs, const Money& rhs) {
            if (lhs.currency() != rhs.currency()) {
                throw CurrencyMismatch(lhs.currency(), rhs.currency());
            }
        }

        // UTF-8 byte sequences for the thousands separators we accept.
        // Each entry is the prefix that must be skipped if matched.
        constexpr std::string_view kNbsp = "\xC2\xA0";          // U+00A0
        constexpr std::string_view kNarrowNbsp = "\xE2\x80\xAF"; // U+202F

        bool skip_thousands_sep(std::string_view& s) {
            if (!s.empty() && s.front() == ' ') {
                s.remove_prefix(1);
                return true;
            }
            if (s.size() >= kNbsp.size() && s.substr(0, kNbsp.size()) == kNbsp) {
                s.remove_prefix(kNbsp.size());
                return true;
            }
            if (s.size() >= kNarrowNbsp.size() && s.substr(0, kNarrowNbsp.size()) == kNarrowNbsp) {
                s.remove_prefix(kNarrowNbsp.size());
                return true;
            }
            return false;
        }

        std::string_view trim(std::string_view s) noexcept {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
                s.remove_prefix(1);
            }
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
                s.remove_suffix(1);
            }
            return s;
        }

    } // namespace

    std::optional<Money> Money::from_string(std::string_view text, Currency fallback_currency) noexcept {
        text = trim(text);
        if (text.empty()) {
            return std::nullopt;
        }

        // Optional trailing ISO currency suffix, separated by whitespace.
        Currency currency = fallback_currency;
        const auto last_space = text.find_last_of(" \t");
        if (last_space != std::string_view::npos) {
            const auto tail = trim(text.substr(last_space + 1));
            if (auto parsed = currency_from_string(tail); parsed.has_value()) {
                currency = *parsed;
                text = trim(text.substr(0, last_space));
            }
        }

        bool negative = false;
        if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
            negative = (text.front() == '-');
            text.remove_prefix(1);
        }

        if (text.empty()) {
            return std::nullopt;
        }

        std::string integral_digits;
        std::string fractional_digits;
        bool seen_decimal = false;
        bool seen_digit = false;

        while (!text.empty()) {
            if (skip_thousands_sep(text)) {
                continue;
            }
            const char c = text.front();
            if (c == '.' || c == ',') {
                if (seen_decimal) {
                    return std::nullopt;
                }
                seen_decimal = true;
                text.remove_prefix(1);
                continue;
            }
            if (c >= '0' && c <= '9') {
                seen_digit = true;
                (seen_decimal ? fractional_digits : integral_digits).push_back(c);
                text.remove_prefix(1);
                continue;
            }
            return std::nullopt;
        }

        if (!seen_digit) {
            return std::nullopt;
        }

        if (fractional_digits.size() > 2) {
            // Reject sub-cent precision rather than silently rounding.
            return std::nullopt;
        }
        while (fractional_digits.size() < 2) {
            fractional_digits.push_back('0');
        }
        if (integral_digits.empty()) {
            integral_digits = "0";
        }

        const std::string combined = integral_digits + fractional_digits;
        std::int64_t minor = 0;
        for (const char c : combined) {
            const int digit = c - '0';
            if (minor > (std::numeric_limits<std::int64_t>::max() - digit) / 10) {
                return std::nullopt; // overflow
            }
            minor = minor * 10 + digit;
        }
        if (negative) {
            minor = -minor;
        }
        return Money{minor, currency};
    }

    Money& Money::operator+=(const Money& other) {
        ensure_same_currency(*this, other);
        minor_units_ += other.minor_units();
        return *this;
    }

    Money& Money::operator-=(const Money& other) {
        ensure_same_currency(*this, other);
        minor_units_ -= other.minor_units();
        return *this;
    }

    Money& Money::operator*=(std::int64_t factor) noexcept {
        minor_units_ *= factor;
        return *this;
    }

    Money operator+(Money lhs, const Money& rhs) {
        lhs += rhs;
        return lhs;
    }

    Money operator-(Money lhs, const Money& rhs) {
        lhs -= rhs;
        return lhs;
    }

    Money operator-(const Money& value) noexcept {
        return Money{-value.minor_units(), value.currency()};
    }

    Money operator*(Money lhs, std::int64_t factor) noexcept {
        lhs *= factor;
        return lhs;
    }

    Money operator*(std::int64_t factor, Money rhs) noexcept {
        rhs *= factor;
        return rhs;
    }

    std::vector<Money> split_proportional(const Money& amount,
                                          const std::vector<std::int64_t>& weights) {
        if (weights.empty()) {
            throw std::invalid_argument("split_proportional: weights must not be empty");
        }
        for (const auto w : weights) {
            if (w <= 0) {
                throw std::invalid_argument("split_proportional: weights must be strictly positive");
            }
        }

        const std::int64_t total_weight = std::accumulate(weights.begin(), weights.end(), std::int64_t{0});
        const std::int64_t total = amount.minor_units();
        const std::int64_t sign = (total < 0) ? -1 : 1;
        const std::int64_t abs_total = (total < 0) ? -total : total;

        std::vector<std::int64_t> base(weights.size());
        std::vector<std::int64_t> remainders(weights.size());
        std::int64_t assigned = 0;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            const std::int64_t numerator = abs_total * weights[i];
            base[i] = numerator / total_weight;
            remainders[i] = numerator % total_weight;
            assigned += base[i];
        }

        std::int64_t residual = abs_total - assigned;
        // Distribute residual minor units to parts with the largest remainder, tie-broken by
        // largest weight then by index.
        std::vector<std::size_t> order(weights.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            if (remainders[a] != remainders[b]) return remainders[a] > remainders[b];
            if (weights[a] != weights[b]) return weights[a] > weights[b];
            return a < b;
        });
        for (std::size_t i = 0; residual > 0 && i < order.size(); ++i, --residual) {
            base[order[i]] += 1;
        }

        std::vector<Money> result;
        result.reserve(weights.size());
        for (const auto v : base) {
            result.emplace_back(sign * v, amount.currency());
        }
        return result;
    }

    std::string to_string(const Money& value) {
        const auto units = value.minor_units();
        const bool is_negative = units < 0;
        const auto abs_units = std::llabs(units);

        const auto integral = abs_units / 100;
        const auto fraction = abs_units % 100;

        std::ostringstream oss;

        if (is_negative) {
            oss << '-';
        }

        oss << integral
            << '.'
            << std::setw(2) << std::setfill('0') << fraction
            << ' ' << value.currency();

        return oss.str();
    }

    std::ostream& operator<<(std::ostream& os, const Money& value) {
        os << to_string(value);
        return os;
    }

} // namespace wealthtorii::money
