#include <gtest/gtest.h>

#include "wealthtorii/ledger/account.hpp"

#include <sstream>
#include <stdexcept>

using namespace wealthtorii::ledger;

TEST(AccountTest, DefaultIsActiveCashEur) {
    const Account account{};

    EXPECT_EQ(account.id(), "CASH");
    EXPECT_EQ(account.name(), "CASH");
    EXPECT_EQ(account.currency(), wealthtorii::money::Currency::EUR);
    EXPECT_EQ(account.type(), AccountType::CASH);
    EXPECT_TRUE(account.is_active());
}

TEST(AccountTest, ParameterizedConstructorStoresFields) {
    const Account account{"BP_CHECKING", "Banque Populaire — courant",
                          wealthtorii::money::Currency::EUR,
                          AccountType::CASH};

    EXPECT_EQ(account.id(), "BP_CHECKING");
    EXPECT_EQ(account.name(), "Banque Populaire — courant");
    EXPECT_EQ(account.currency(), wealthtorii::money::Currency::EUR);
    EXPECT_EQ(account.type(), AccountType::CASH);
    EXPECT_TRUE(account.is_active());
}

TEST(AccountTest, ActivateAndDeactivateFlipState) {
    Account account{"S1", "Livret A", wealthtorii::money::Currency::EUR,
                    AccountType::SAVINGS};

    account.deactivate();
    EXPECT_FALSE(account.is_active());

    account.activate();
    EXPECT_TRUE(account.is_active());
}

TEST(AccountTest, EmptyIdRejected) {
    EXPECT_THROW(
        Account("", "name", wealthtorii::money::Currency::EUR, AccountType::CASH),
        std::invalid_argument);
}

TEST(AccountTest, EmptyNameRejected) {
    EXPECT_THROW(
        Account("id", "", wealthtorii::money::Currency::EUR, AccountType::CASH),
        std::invalid_argument);
}

TEST(AccountTest, EqualityComparesAllFields) {
    const Account a{"X", "X", wealthtorii::money::Currency::EUR, AccountType::CASH};
    const Account b{"X", "X", wealthtorii::money::Currency::EUR, AccountType::CASH};
    const Account c{"X", "Y", wealthtorii::money::Currency::EUR, AccountType::CASH};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(AccountTest, ToStringMentionsKeyFields) {
    const Account account{"S1", "Livret A", wealthtorii::money::Currency::EUR,
                          AccountType::SAVINGS};
    const auto repr = to_string(account);

    EXPECT_NE(repr.find("S1"), std::string::npos);
    EXPECT_NE(repr.find("Livret A"), std::string::npos);
    EXPECT_NE(repr.find("EUR"), std::string::npos);
    EXPECT_NE(repr.find("SAVINGS"), std::string::npos);
    EXPECT_NE(repr.find("true"), std::string::npos);
}

TEST(AccountTypeTest, RoundTrip) {
    for (auto t : {AccountType::CASH, AccountType::BROKERAGE,
                   AccountType::CRYPTO, AccountType::SAVINGS,
                   AccountType::EXTERNAL}) {
        const auto label = to_string(t);
        const auto parsed = account_type_from_string(label);
        ASSERT_TRUE(parsed.has_value()) << "no round-trip for " << label;
        EXPECT_EQ(*parsed, t);
    }
}

TEST(AccountTypeTest, FromStringReturnsNulloptForUnknown) {
    EXPECT_FALSE(account_type_from_string("FOO").has_value());
}
