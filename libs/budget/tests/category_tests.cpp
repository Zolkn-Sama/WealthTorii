#include <gtest/gtest.h>

#include "wealthtorii/budget/category.hpp"

#include <stdexcept>

using namespace wealthtorii::budget;

TEST(BucketTest, RoundTripsToAndFromString) {
    for (auto b : {Bucket::NEEDS, Bucket::WANTS, Bucket::SAVINGS_INVEST,
                   Bucket::INCOME, Bucket::TRANSFERS}) {
        const auto label = to_string(b);
        const auto parsed = bucket_from_string(label);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, b);
    }
}

TEST(BucketTest, FromStringReturnsNulloptForUnknown) {
    EXPECT_FALSE(bucket_from_string("FOO").has_value());
}

TEST(CategoryTest, ConstructsValid) {
    const Category c{"housing", "Logement", Bucket::NEEDS};
    EXPECT_EQ(c.id(), "housing");
    EXPECT_EQ(c.name(), "Logement");
    EXPECT_EQ(c.bucket(), Bucket::NEEDS);
}

TEST(CategoryTest, RejectsEmptyIdOrName) {
    EXPECT_THROW(Category("", "X", Bucket::NEEDS), std::invalid_argument);
    EXPECT_THROW(Category("X", "", Bucket::NEEDS), std::invalid_argument);
}

TEST(CategoryRegistryTest, AddAndFind) {
    CategoryRegistry r;
    r.add({"housing", "Logement", Bucket::NEEDS});
    r.add({"groceries", "Courses", Bucket::NEEDS});

    EXPECT_EQ(r.size(), 2u);
    ASSERT_NE(r.find("housing"), nullptr);
    EXPECT_EQ(r.find("housing")->name(), "Logement");
    EXPECT_EQ(r.find("missing"), nullptr);
}

TEST(CategoryRegistryTest, RejectsDuplicateId) {
    CategoryRegistry r;
    r.add({"housing", "Logement", Bucket::NEEDS});
    EXPECT_THROW(r.add({"housing", "Autre", Bucket::WANTS}), std::invalid_argument);
}

TEST(CategoryRegistryTest, InBucketFilters) {
    const auto r = default_registry();
    const auto needs = r.in_bucket(Bucket::NEEDS);
    const auto wants = r.in_bucket(Bucket::WANTS);
    const auto invest = r.in_bucket(Bucket::SAVINGS_INVEST);
    const auto income = r.in_bucket(Bucket::INCOME);
    const auto transfers = r.in_bucket(Bucket::TRANSFERS);

    EXPECT_GE(needs.size(), 4u);
    EXPECT_GE(wants.size(), 3u);
    EXPECT_GE(invest.size(), 2u);
    EXPECT_GE(income.size(), 2u);
    EXPECT_GE(transfers.size(), 2u);
    EXPECT_EQ(needs.size() + wants.size() + invest.size() + income.size() + transfers.size(),
              r.size());
}

TEST(DefaultRegistryTest, ContainsExpectedCategories) {
    const auto r = default_registry();
    EXPECT_NE(r.find("housing"), nullptr);
    EXPECT_NE(r.find("transport"), nullptr);
    EXPECT_NE(r.find("groceries"), nullptr);
    EXPECT_NE(r.find("subscriptions-essential"), nullptr);
    EXPECT_NE(r.find("savings"), nullptr);
    EXPECT_NE(r.find("investments"), nullptr);
}
