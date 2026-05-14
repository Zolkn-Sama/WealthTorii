#include <gtest/gtest.h>

#include "commands.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#ifndef WEALTHTORII_DATA_CSV
#define WEALTHTORII_DATA_CSV ""
#endif

using namespace wealthtorii::cli;

namespace {
    std::filesystem::path make_temp_config_path() {
        auto tmp = std::filesystem::temp_directory_path() /
                   ("wt_test_" + std::to_string(std::rand()) + ".conf");
        return tmp;
    }

    Args span_of(const std::vector<std::string_view>& v) {
        return {v.data(), v.size()};
    }

    Environment make_temp_env() {
        Environment env;
        env.budget_config_path = make_temp_config_path();
        env.rules_config_path = make_temp_config_path();
        return env;
    }

    void cleanup_env(const Environment& env) {
        std::filesystem::remove(env.budget_config_path);
        std::filesystem::remove(env.rules_config_path);
    }
}

TEST(CommandHelp, MentionsCoreCommands) {
    std::ostringstream out, err;
    EXPECT_EQ(cmd_help({}, out, err), 0);
    const auto text = out.str();
    EXPECT_NE(text.find("allocate"), std::string::npos);
    EXPECT_NE(text.find("import"), std::string::npos);
    EXPECT_NE(text.find("report"), std::string::npos);
    EXPECT_NE(text.find("budget"), std::string::npos);
}

TEST(CommandAllocate, PrintsSplit) {
    std::ostringstream out, err;
    std::vector<std::string_view> args{"1800"};
    EXPECT_EQ(cmd_allocate(span_of(args), out, err), 0);
    const auto s = out.str();
    EXPECT_NE(s.find("NEEDS"), std::string::npos);
    EXPECT_NE(s.find("900.00 EUR"), std::string::npos);    // 50%
    EXPECT_NE(s.find("540.00 EUR"), std::string::npos);    // 30%
    EXPECT_NE(s.find("360.00 EUR"), std::string::npos);    // 20%
}

TEST(CommandAllocate, AcceptsFrenchFormat) {
    std::ostringstream out, err;
    std::vector<std::string_view> args{"1234,56"};
    EXPECT_EQ(cmd_allocate(span_of(args), out, err), 0);
    EXPECT_FALSE(out.str().empty());
}

TEST(CommandAllocate, ErrorsOnMissingArg) {
    std::ostringstream out, err;
    EXPECT_NE(cmd_allocate({}, out, err), 0);
    EXPECT_FALSE(err.str().empty());
}

TEST(CommandAllocate, ErrorsOnNegativeIncome) {
    std::ostringstream out, err;
    std::vector<std::string_view> args{"-100"};
    EXPECT_NE(cmd_allocate(span_of(args), out, err), 0);
}

TEST(CommandCategories, ListsAllBuckets) {
    std::ostringstream out, err;
    EXPECT_EQ(cmd_categories({}, out, err), 0);
    const auto s = out.str();
    EXPECT_NE(s.find("NEEDS"), std::string::npos);
    EXPECT_NE(s.find("WANTS"), std::string::npos);
    EXPECT_NE(s.find("SAVINGS_INVEST"), std::string::npos);
    EXPECT_NE(s.find("INCOME"), std::string::npos);
    EXPECT_NE(s.find("housing"), std::string::npos);
    EXPECT_NE(s.find("salary"), std::string::npos);
}

TEST(CommandBudget, SetAndShowRoundTrips) {
    Environment env = make_temp_env();
    auto cleanup = [&]() { cleanup_env(env); };

    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"set", "housing", "600"};
        ASSERT_EQ(cmd_budget(span_of(args), out, err, env), 0) << err.str();
        EXPECT_NE(out.str().find("housing"), std::string::npos);
    }
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"set", "groceries", "300,50"};
        ASSERT_EQ(cmd_budget(span_of(args), out, err, env), 0) << err.str();
    }
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"show"};
        ASSERT_EQ(cmd_budget(span_of(args), out, err, env), 0) << err.str();
        const auto s = out.str();
        EXPECT_NE(s.find("housing"), std::string::npos);
        EXPECT_NE(s.find("600.00 EUR"), std::string::npos);
        EXPECT_NE(s.find("groceries"), std::string::npos);
        EXPECT_NE(s.find("300.50 EUR"), std::string::npos);
        EXPECT_NE(s.find("TOTAL"), std::string::npos);
    }
    cleanup();
}

TEST(CommandBudget, RejectsNegativeLimit) {
    Environment env = make_temp_env();
    std::ostringstream out, err;
    std::vector<std::string_view> args{"set", "housing", "-100"};
    EXPECT_NE(cmd_budget(span_of(args), out, err, env), 0);
    cleanup_env(env);
}

TEST(CommandRules, AddShowRemoveCycle) {
    Environment env = make_temp_env();

    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"add", R"(\bsteam\w*)", "subscriptions-leisure"};
        ASSERT_EQ(cmd_rules(span_of(args), out, err, env), 0) << err.str();
        EXPECT_NE(out.str().find("added"), std::string::npos);
    }
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"show"};
        ASSERT_EQ(cmd_rules(span_of(args), out, err, env), 0) << err.str();
        EXPECT_NE(out.str().find("steam"), std::string::npos);
        EXPECT_NE(out.str().find("subscriptions-leisure"), std::string::npos);
    }
    {
        // Re-adding the same pattern updates rather than duplicates.
        std::ostringstream out, err;
        std::vector<std::string_view> args{"add", R"(\bsteam\w*)", "leisure"};
        ASSERT_EQ(cmd_rules(span_of(args), out, err, env), 0) << err.str();
        EXPECT_NE(out.str().find("updated"), std::string::npos);
    }
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"remove", R"(\bsteam\w*)"};
        ASSERT_EQ(cmd_rules(span_of(args), out, err, env), 0) << err.str();
    }
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"remove", R"(does-not-exist)"};
        EXPECT_NE(cmd_rules(span_of(args), out, err, env), 0);
    }
    cleanup_env(env);
}

TEST(CommandRules, RejectsInvalidRegexOnAdd) {
    Environment env = make_temp_env();
    std::ostringstream out, err;
    std::vector<std::string_view> args{"add", "((unbalanced", "housing"};
    EXPECT_NE(cmd_rules(span_of(args), out, err, env), 0);
    EXPECT_NE(err.str().find("invalid regex"), std::string::npos);
    cleanup_env(env);
}

TEST(CommandRunDispatcher, UnknownCommandReturnsError) {
    std::ostringstream out, err;
    Environment env;
    std::vector<std::string_view> args{"nope"};
    EXPECT_NE(run(span_of(args), out, err, env), 0);
    EXPECT_NE(err.str().find("unknown command"), std::string::npos);
}

TEST(CommandRunDispatcher, NoArgsShowsHelp) {
    std::ostringstream out, err;
    Environment env;
    EXPECT_EQ(run({}, out, err, env), 0);
    EXPECT_NE(out.str().find("usage:"), std::string::npos);
}

// --- End-to-end on real DATA.csv ---

class RealDataCli : public ::testing::Test {
protected:
    void SetUp() override {
        std::ifstream f(WEALTHTORII_DATA_CSV);
        if (!f.good()) GTEST_SKIP() << "DATA.csv not available";
    }
};

TEST_F(RealDataCli, ImportAndReportSucceed) {
    Environment env = make_temp_env();

    // Import
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"import", WEALTHTORII_DATA_CSV, "--account", "BP"};
        ASSERT_EQ(run(span_of(args), out, err, env), 0) << err.str();
        EXPECT_NE(out.str().find("transactions"), std::string::npos);
    }

    // Report on April 2026
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"report", WEALTHTORII_DATA_CSV, "--account", "BP",
                                            "--month", "2026-04"};
        ASSERT_EQ(run(span_of(args), out, err, env), 0) << err.str();
        const auto s = out.str();
        EXPECT_NE(s.find("Report for 2026-04"), std::string::npos);
        EXPECT_NE(s.find("inflow"), std::string::npos);
        EXPECT_NE(s.find("outflow"), std::string::npos);
        EXPECT_NE(s.find("By bucket"), std::string::npos);
    }

    // Report defaults to latest month when --month omitted
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"report", WEALTHTORII_DATA_CSV, "--account", "BP"};
        ASSERT_EQ(run(span_of(args), out, err, env), 0) << err.str();
        EXPECT_NE(out.str().find("Report for 2026-04"), std::string::npos);
    }

    // Set a budget then report → should include the budget-vs-spending section
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"budget", "set", "groceries", "300"};
        ASSERT_EQ(run(span_of(args), out, err, env), 0) << err.str();
    }
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"report", WEALTHTORII_DATA_CSV, "--account", "BP",
                                            "--month", "2026-04"};
        ASSERT_EQ(run(span_of(args), out, err, env), 0) << err.str();
        EXPECT_NE(out.str().find("Budget vs spending"), std::string::npos);
        EXPECT_NE(out.str().find("groceries"), std::string::npos);
    }

    cleanup_env(env);
}

TEST_F(RealDataCli, UserRuleReclassifiesTransferToHousing) {
    // The 03/04/2026 "Virement vers Sci Joly" is auto-classified as transfer-out under the new
    // taxonomy (so it's excluded from the outflow breakdown — neutral movement). Adding a user
    // rule should reclassify it to housing and surface it in the breakdown.
    Environment env = make_temp_env();

    auto extract_section = [](const std::string& text, std::string_view header) {
        const auto start = text.find(header);
        if (start == std::string::npos) return std::string{};
        const auto stop = text.find("\n\n", start);
        return text.substr(start, stop == std::string::npos ? std::string::npos : stop - start);
    };

    std::string before_breakdown;
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"report", WEALTHTORII_DATA_CSV, "--account", "BP",
                                            "--month", "2026-04"};
        ASSERT_EQ(run(span_of(args), out, err, env), 0) << err.str();
        before_breakdown = extract_section(out.str(), "By category");
        // Under the cleaned taxonomy housing isn't yet present (Sci Joly = transfer-out).
        EXPECT_EQ(before_breakdown.find("housing"), std::string::npos);
    }

    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{
            "rules", "add", R"(virement\s+vers\s+sci\s+joly)", "housing"};
        ASSERT_EQ(run(span_of(args), out, err, env), 0) << err.str();
    }

    std::string after_breakdown;
    {
        std::ostringstream out, err;
        std::vector<std::string_view> args{"report", WEALTHTORII_DATA_CSV, "--account", "BP",
                                            "--month", "2026-04"};
        ASSERT_EQ(run(span_of(args), out, err, env), 0) << err.str();
        after_breakdown = extract_section(out.str(), "By category");
    }

    EXPECT_NE(after_breakdown.find("housing"), std::string::npos);

    cleanup_env(env);
}
