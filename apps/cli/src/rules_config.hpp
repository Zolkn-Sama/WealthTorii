#pragma once

#include <wealthtorii/import/bp_csv.hpp>

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace wealthtorii::cli {

    // One user-defined regex categorisation rule. The pattern is stored verbatim so it can be
    // round-tripped through the config file; the compiled regex lives in Categorizer.
    // bp_subcategory is optional: when set, it overrides the BP sub-category emitted on export.
    struct Rule {
        std::string pattern;
        std::string category_id;
        std::optional<std::string> bp_subcategory;
    };

    // Ordered list of user rules; insertion order is the matching priority (first match wins).
    struct RulesConfig {
        std::vector<Rule> rules;
    };

    [[nodiscard]] RulesConfig load_rules_config(const std::filesystem::path& file);
    void save_rules_config(const std::filesystem::path& file, const RulesConfig& cfg);

    // Stream variants for testing. Line format: "pattern => category_id". Comments start with #.
    [[nodiscard]] RulesConfig parse_rules_config(std::istream& in);
    void write_rules_config(std::ostream& out, const RulesConfig& cfg);

    [[nodiscard]] std::filesystem::path default_rules_config_path();

    // Builds a Categorizer that applies user rules first (highest priority), then the supplied
    // base rules (typically default_overrides()). Invalid regexes in the user config produce a
    // std::runtime_error mentioning the offending pattern.
    [[nodiscard]] import_::Categorizer build_categorizer(const RulesConfig& user,
                                                          const import_::Categorizer& base);

} // namespace wealthtorii::cli
