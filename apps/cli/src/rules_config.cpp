#include "rules_config.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace wealthtorii::cli {

    namespace {
        constexpr std::string_view kSeparator = " => ";

        std::string_view trim(std::string_view s) noexcept {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
                s.remove_prefix(1);
            }
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
                s.remove_suffix(1);
            }
            return s;
        }
    } // namespace

    RulesConfig parse_rules_config(std::istream& in) {
        RulesConfig cfg;
        std::string line;
        std::size_t lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            const auto sv = trim(line);
            if (sv.empty() || sv.front() == '#') {
                continue;
            }
            const auto pos = sv.find(kSeparator);
            if (pos == std::string_view::npos) {
                throw std::runtime_error("RulesConfig: line " + std::to_string(lineno) +
                                         " missing ' => ' separator");
            }
            const auto pattern = std::string(trim(sv.substr(0, pos)));
            const auto rhs = trim(sv.substr(pos + kSeparator.size()));

            // Optional BP sub-category override: "wt_cat / bp_subcategory".
            std::string category;
            std::optional<std::string> bp_sub;
            if (const auto slash = rhs.find(" / "); slash != std::string_view::npos) {
                category = std::string(trim(rhs.substr(0, slash)));
                bp_sub = std::string(trim(rhs.substr(slash + 3)));
                if (bp_sub->empty()) bp_sub.reset();
            } else {
                category = std::string(rhs);
            }
            if (pattern.empty() || category.empty()) {
                throw std::runtime_error("RulesConfig: line " + std::to_string(lineno) +
                                         " has empty pattern or category");
            }
            cfg.rules.push_back(Rule{pattern, category, std::move(bp_sub)});
        }
        return cfg;
    }

    void write_rules_config(std::ostream& out, const RulesConfig& cfg) {
        out << "# WealthTorii user categorisation rules.\n"
            << "# Format: <regex> => <category_id>            (overrides WT category only)\n"
            << "#         <regex> => <category_id> / <bp_subcategory>  (also overrides BP sub-cat)\n"
            << "# Patterns are case-insensitive. First match wins; user rules beat the built-in defaults.\n\n";
        for (const auto& r : cfg.rules) {
            out << r.pattern << kSeparator << r.category_id;
            if (r.bp_subcategory.has_value()) {
                out << " / " << *r.bp_subcategory;
            }
            out << '\n';
        }
    }

    RulesConfig load_rules_config(const std::filesystem::path& file) {
        if (!std::filesystem::exists(file)) {
            return RulesConfig{};
        }
        std::ifstream in(file);
        if (!in) {
            throw std::runtime_error("RulesConfig: cannot open " + file.string());
        }
        return parse_rules_config(in);
    }

    void save_rules_config(const std::filesystem::path& file, const RulesConfig& cfg) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file);
        if (!out) {
            throw std::runtime_error("RulesConfig: cannot write " + file.string());
        }
        write_rules_config(out, cfg);
    }

    std::filesystem::path default_rules_config_path() {
        const char* home = std::getenv("HOME");
        if (home == nullptr) {
            home = ".";
        }
        return std::filesystem::path{home} / ".wealthtorii" / "rules.conf";
    }

    import_::Categorizer build_categorizer(const RulesConfig& user,
                                            const import_::Categorizer& base) {
        import_::Categorizer merged;
        for (const auto& r : user.rules) {
            try {
                if (r.bp_subcategory.has_value()) {
                    merged.add_rule_with_subcategory(r.pattern, r.category_id, *r.bp_subcategory);
                } else {
                    merged.add_rule(r.pattern, r.category_id);
                }
            } catch (const std::regex_error& e) {
                throw std::runtime_error("RulesConfig: invalid regex '" + r.pattern + "': " +
                                         e.what());
            }
        }
        merged.extend(base);
        return merged;
    }

} // namespace wealthtorii::cli
