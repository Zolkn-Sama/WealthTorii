#pragma once

#include <wealthtorii/budget/category.hpp>
#include <wealthtorii/money/money.hpp>

#include <json/json.h>

#include <charconv>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace wealthtorii::api {

    // Money is serialised in a lossless, machine-friendly shape plus a human display string.
    [[nodiscard]] inline Json::Value money_json(const money::Money& m) {
        Json::Value v(Json::objectValue);
        v["minor_units"] = static_cast<Json::Int64>(m.minor_units());
        v["currency"] = std::string(money::to_string(m.currency()));
        v["display"] = money::to_string(m);
        return v;
    }

    [[nodiscard]] inline Json::Value error_json(std::string_view message) {
        Json::Value v(Json::objectValue);
        v["error"] = std::string(message);
        return v;
    }

    // Parses an "YYYY-MM" string into a year_month. Returns nullopt on malformed input.
    [[nodiscard]] inline std::optional<std::chrono::year_month>
    parse_year_month(std::string_view s) noexcept {
        if (s.size() != 7 || s[4] != '-') {
            return std::nullopt;
        }
        int y = 0;
        int mo = 0;
        if (std::from_chars(s.data(), s.data() + 4, y).ec != std::errc{}) {
            return std::nullopt;
        }
        if (std::from_chars(s.data() + 5, s.data() + 7, mo).ec != std::errc{}) {
            return std::nullopt;
        }
        const std::chrono::year_month ym{std::chrono::year{y},
                                         std::chrono::month{static_cast<unsigned>(mo)}};
        if (!ym.ok()) {
            return std::nullopt;
        }
        return ym;
    }

    [[nodiscard]] inline std::string format_year_month(std::chrono::year_month ym) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%04d-%02u", static_cast<int>(ym.year()),
                      static_cast<unsigned>(ym.month()));
        return std::string(buf);
    }

} // namespace wealthtorii::api
