#include <gtest/gtest.h>

#include "wealthtorii/market_data/stooq.hpp"

using namespace wealthtorii::market_data;

TEST(StooqPath, BuildsCsvQueryAndEncodes) {
    EXPECT_EQ(stooq_path("aapl.us"), "/q/l/?s=aapl.us&f=sd2t2ohlc&h&e=csv");
    EXPECT_EQ(stooq_path("a b"), "/q/l/?s=a%20b&f=sd2t2ohlc&h&e=csv");
}

TEST(ParseStooq, ValidLineUsesClose) {
    const std::string body =
        "Symbol,Date,Time,Open,High,Low,Close\r\n"
        "AAPL.US,2026-05-16,22:00:05,189.50,191.0,188.7,190.12\r\n";
    const auto q = parse_stooq_csv(body);
    ASSERT_TRUE(q.has_value());
    EXPECT_EQ(q->symbol, "AAPL.US");
    EXPECT_EQ(q->price_minor, 19012);
    EXPECT_EQ(q->as_of, "2026-05-16");
}

TEST(ParseStooq, IntegerCloseAndNoHeader) {
    const auto q = parse_stooq_csv("BTC.V,2026-05-16,22:00:00,1,2,3,16000\n");
    ASSERT_TRUE(q.has_value());
    EXPECT_EQ(q->price_minor, 1600000);
}

TEST(ParseStooq, NotAvailableReturnsNullopt) {
    const std::string body =
        "Symbol,Date,Time,Open,High,Low,Close\r\n"
        "ZZZZ.US,N/D,N/D,N/D,N/D,N/D,N/D\r\n";
    EXPECT_FALSE(parse_stooq_csv(body).has_value());
}

TEST(ParseStooq, MalformedReturnsNullopt) {
    EXPECT_FALSE(parse_stooq_csv("garbage\r\n").has_value());
    EXPECT_FALSE(parse_stooq_csv("").has_value());
}
