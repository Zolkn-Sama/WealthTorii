#include <gtest/gtest.h>

#include "rules_config.hpp"

#include <sstream>

using namespace wealthtorii::cli;
using wealthtorii::import_::Categorizer;
using wealthtorii::import_::default_overrides;

TEST(RulesConfig, RoundTripsThroughStreams) {
    RulesConfig cfg;
    cfg.rules.push_back({R"(virement\s+vers\s+sci\s+joly)", "housing"});
    cfg.rules.push_back({R"(\bnetflix\b)", "subscriptions-leisure"});

    std::ostringstream out;
    write_rules_config(out, cfg);

    std::istringstream in(out.str());
    const auto reloaded = parse_rules_config(in);
    ASSERT_EQ(reloaded.rules.size(), 2u);
    EXPECT_EQ(reloaded.rules[0].pattern, R"(virement\s+vers\s+sci\s+joly)");
    EXPECT_EQ(reloaded.rules[0].category_id, "housing");
    EXPECT_EQ(reloaded.rules[1].pattern, R"(\bnetflix\b)");
    EXPECT_EQ(reloaded.rules[1].category_id, "subscriptions-leisure");
}

TEST(RulesConfig, IgnoresCommentsAndBlanks) {
    std::istringstream in(
        "# header\n"
        "\n"
        "foo => groceries\n"
        "# trailing\n"
        "bar => transport\n");
    const auto cfg = parse_rules_config(in);
    ASSERT_EQ(cfg.rules.size(), 2u);
}

TEST(RulesConfig, RejectsMalformedLines) {
    std::istringstream missing(R"(no_separator_here)");
    EXPECT_THROW(static_cast<void>(parse_rules_config(missing)), std::runtime_error);

    std::istringstream empty_lhs(" => housing");
    EXPECT_THROW(static_cast<void>(parse_rules_config(empty_lhs)), std::runtime_error);

    std::istringstream empty_rhs("foo => ");
    EXPECT_THROW(static_cast<void>(parse_rules_config(empty_rhs)), std::runtime_error);
}

TEST(BuildCategorizer, UserRulesBeatBaseRules) {
    // default_overrides routes "steam" → leisure ; user rule overrides to subscriptions-leisure.
    RulesConfig user;
    user.rules.push_back({R"(\bsteam\w*)", "subscriptions-leisure"});

    const auto base = default_overrides();
    const auto merged = build_categorizer(user, base);

    EXPECT_EQ(merged.categorize("STEAMGAMES Paris"), "subscriptions-leisure");
}

TEST(BuildCategorizer, FallsBackToBaseWhenNoUserMatch) {
    RulesConfig user;
    user.rules.push_back({R"(this-will-never-match)", "savings"});
    const auto merged = build_categorizer(user, default_overrides());

    EXPECT_EQ(merged.categorize("CARREFOUR CITY"), "groceries");
}

TEST(BuildCategorizer, RejectsInvalidUserRegex) {
    RulesConfig user;
    user.rules.push_back({"((((unbalanced", "x"});
    EXPECT_THROW(static_cast<void>(build_categorizer(user, default_overrides())),
                 std::runtime_error);
}

TEST(CategorizerExtend, AppendsRulesAtLowerPriority) {
    Categorizer first;
    first.add_rule(R"(foo)", "leisure");
    Categorizer second;
    second.add_rule(R"(foo)", "groceries"); // would win if it came first

    first.extend(second);
    EXPECT_EQ(first.size(), 2u);
    EXPECT_EQ(first.categorize("foo bar"), "leisure"); // first match still wins
}
