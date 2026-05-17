#include "wealthtorii/market_data/stooq.hpp"

#include <wealthtorii/money/money.hpp>

#include <array>
#include <vector>

namespace wealthtorii::market_data {

    namespace {
        std::string_view trim(std::string_view s) {
            while (!s.empty() &&
                   (s.front() == ' ' || s.front() == '\r' || s.front() == '\n')) {
                s.remove_prefix(1);
            }
            while (!s.empty() &&
                   (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) {
                s.remove_suffix(1);
            }
            return s;
        }

        std::vector<std::string_view> split(std::string_view line, char sep) {
            std::vector<std::string_view> out;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= line.size(); ++i) {
                if (i == line.size() || line[i] == sep) {
                    out.push_back(line.substr(start, i - start));
                    start = i + 1;
                }
            }
            return out;
        }
    } // namespace

    std::string stooq_path(std::string_view symbol) {
        std::string enc;
        enc.reserve(symbol.size());
        for (const char c : symbol) {
            const bool safe = (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                              c == '_';
            if (safe) {
                enc.push_back(c);
            } else {
                static constexpr char hex[] = "0123456789ABCDEF";
                enc.push_back('%');
                enc.push_back(hex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
                enc.push_back(hex[static_cast<unsigned char>(c) & 0xF]);
            }
        }
        return "/q/l/?s=" + enc + "&f=sd2t2ohlc&h&e=csv";
    }

    std::optional<Quote> parse_stooq_csv(std::string_view body) {
        for (const auto raw : split(body, '\n')) {
            const auto line = trim(raw);
            if (line.empty()) {
                continue;
            }
            if (line.substr(0, 7) == "Symbol,") {
                continue; // header
            }
            const auto cols = split(line, ',');
            if (cols.size() < 7) {
                return std::nullopt;
            }
            const auto close = trim(cols[6]);
            if (close.empty() || close == "N/D") {
                return std::nullopt;
            }
            const auto m = money::Money::from_string(close);
            if (!m.has_value()) {
                return std::nullopt;
            }
            Quote q;
            q.symbol = std::string(trim(cols[0]));
            q.price_minor = m->minor_units();
            const auto date = trim(cols[1]);
            if (date != "N/D") {
                q.as_of = std::string(date);
            }
            return q;
        }
        return std::nullopt;
    }

} // namespace wealthtorii::market_data
