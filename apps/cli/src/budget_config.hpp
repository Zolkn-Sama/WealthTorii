#pragma once

#include <wealthtorii/money/money.hpp>

#include <filesystem>
#include <map>
#include <string>

namespace wealthtorii::cli {

    // Per-month budget limits, persisted as a tiny key=value text file.
    // Format (one per line, ignores blank lines and lines starting with '#'):
    //   currency=EUR
    //   housing=600,00
    //   groceries=300,00
    struct BudgetConfig {
        money::Currency currency = money::Currency::EUR;
        std::map<std::string, money::Money> limits;
    };

    [[nodiscard]] BudgetConfig load_budget_config(const std::filesystem::path& file);
    void save_budget_config(const std::filesystem::path& file, const BudgetConfig& cfg);

    // Parses key=value lines from an arbitrary stream. Exposed for testing.
    [[nodiscard]] BudgetConfig parse_budget_config(std::istream& in);
    void write_budget_config(std::ostream& out, const BudgetConfig& cfg);

    // Returns the path where the CLI stores budgets (mkdir -p the parent on first save).
    [[nodiscard]] std::filesystem::path default_budget_config_path();

} // namespace wealthtorii::cli
