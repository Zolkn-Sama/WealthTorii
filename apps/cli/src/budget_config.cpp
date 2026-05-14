#include "budget_config.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace wealthtorii::cli {

    namespace {
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

    BudgetConfig parse_budget_config(std::istream& in) {
        BudgetConfig cfg;
        std::string line;
        std::size_t lineno = 0;
        while (std::getline(in, line)) {
            ++lineno;
            const auto sv = trim(line);
            if (sv.empty() || sv.front() == '#') {
                continue;
            }
            const auto eq = sv.find('=');
            if (eq == std::string_view::npos) {
                throw std::runtime_error("BudgetConfig: line " + std::to_string(lineno) +
                                         " missing '=' separator");
            }
            const auto key = std::string(trim(sv.substr(0, eq)));
            const auto value = trim(sv.substr(eq + 1));

            if (key == "currency") {
                const auto parsed = money::currency_from_string(value);
                if (!parsed.has_value()) {
                    throw std::runtime_error("BudgetConfig: unknown currency '" +
                                             std::string(value) + "'");
                }
                cfg.currency = *parsed;
                continue;
            }
            const auto parsed_amount = money::Money::from_string(value, cfg.currency);
            if (!parsed_amount.has_value()) {
                throw std::runtime_error("BudgetConfig: malformed amount on line " +
                                         std::to_string(lineno));
            }
            cfg.limits[key] = *parsed_amount;
        }
        return cfg;
    }

    void write_budget_config(std::ostream& out, const BudgetConfig& cfg) {
        out << "currency=" << money::to_string(cfg.currency) << '\n';
        for (const auto& [cat, limit] : cfg.limits) {
            // Amount in plain integer.decimal form, no currency suffix (one currency per file).
            const auto minor = limit.minor_units();
            const auto integral = minor / 100;
            const auto fraction = minor % 100;
            out << cat << '=' << integral << ','
                << std::setw(2) << std::setfill('0') << fraction << '\n';
        }
    }

    BudgetConfig load_budget_config(const std::filesystem::path& file) {
        if (!std::filesystem::exists(file)) {
            return BudgetConfig{};
        }
        std::ifstream in(file);
        if (!in) {
            throw std::runtime_error("BudgetConfig: cannot open " + file.string());
        }
        return parse_budget_config(in);
    }

    void save_budget_config(const std::filesystem::path& file, const BudgetConfig& cfg) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file);
        if (!out) {
            throw std::runtime_error("BudgetConfig: cannot write " + file.string());
        }
        write_budget_config(out, cfg);
    }

    std::filesystem::path default_budget_config_path() {
        const char* home = std::getenv("HOME");
        if (home == nullptr) {
            home = "."; // best-effort fallback
        }
        return std::filesystem::path{home} / ".wealthtorii" / "budgets.conf";
    }

} // namespace wealthtorii::cli
