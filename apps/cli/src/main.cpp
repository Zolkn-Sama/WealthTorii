#include "commands.hpp"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(std::max(0, argc - 1)));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    wealthtorii::cli::Environment env;
    env.budget_config_path = wealthtorii::cli::default_budget_config_path();
    env.rules_config_path = wealthtorii::cli::default_rules_config_path();

    return wealthtorii::cli::run({args.data(), args.size()}, std::cout, std::cerr, env);
}
