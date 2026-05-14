#pragma once

#include "budget_config.hpp"
#include "rules_config.hpp"

#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wealthtorii::cli {

    struct Environment {
        std::filesystem::path budget_config_path;
        std::filesystem::path rules_config_path;
    };

    using Args = std::span<const std::string_view>;

    int cmd_help(Args args, std::ostream& out, std::ostream& err);
    int cmd_allocate(Args args, std::ostream& out, std::ostream& err);
    int cmd_categories(Args args, std::ostream& out, std::ostream& err);
    int cmd_import(Args args, std::ostream& out, std::ostream& err, const Environment& env);
    int cmd_report(Args args, std::ostream& out, std::ostream& err, const Environment& env);
    int cmd_budget(Args args, std::ostream& out, std::ostream& err, const Environment& env);
    int cmd_rules(Args args, std::ostream& out, std::ostream& err, const Environment& env);
    int cmd_sync(Args args, std::ostream& out, std::ostream& err, const Environment& env);
    int cmd_suggest(Args args, std::ostream& out, std::ostream& err, const Environment& env);
    int cmd_export(Args args, std::ostream& out, std::ostream& err, const Environment& env);

    // Top-level dispatcher used by main and tests.
    int run(Args args, std::ostream& out, std::ostream& err, const Environment& env);

} // namespace wealthtorii::cli
