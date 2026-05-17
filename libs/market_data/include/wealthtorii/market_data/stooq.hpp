#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wealthtorii::market_data {

    // A parsed quote: last price in minor units, with the source date.
    struct Quote {
        std::string symbol;
        std::int64_t price_minor = 0;
        std::string as_of; // ISO yyyy-mm-dd, "" if absent
    };

    // Path+query for the Stooq light CSV endpoint (host: https://stooq.com).
    // Fields: symbol,date,time,open,high,low,close.
    [[nodiscard]] std::string stooq_path(std::string_view symbol);

    // Parses a Stooq light CSV body. Returns nullopt when the symbol is
    // unknown (Stooq emits "N/D") or the body is malformed. The `close`
    // column is used as the price.
    [[nodiscard]] std::optional<Quote> parse_stooq_csv(std::string_view body);

} // namespace wealthtorii::market_data
