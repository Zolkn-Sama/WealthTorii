#include "wealthtorii/import/bp_csv.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace wealthtorii::import_ {

    namespace {

        constexpr char kSep = ';';

        std::string_view rstrip_cr(std::string_view s) noexcept {
            if (!s.empty() && s.back() == '\r') {
                s.remove_suffix(1);
            }
            return s;
        }

        std::vector<std::string> split_fields(std::string_view line) {
            std::vector<std::string> out;
            out.reserve(13);
            std::size_t start = 0;
            for (std::size_t i = 0; i <= line.size(); ++i) {
                if (i == line.size() || line[i] == kSep) {
                    out.emplace_back(line.substr(start, i - start));
                    start = i + 1;
                }
            }
            return out;
        }

        std::optional<int> parse_uint(std::string_view s) noexcept {
            int value = 0;
            const auto* first = s.data();
            const auto* last = s.data() + s.size();
            const auto result = std::from_chars(first, last, value);
            if (result.ec != std::errc{} || result.ptr != last) {
                return std::nullopt;
            }
            return value;
        }

        // Parses "DD/MM/YYYY"; returns nullopt on any structural issue.
        std::optional<std::chrono::year_month_day> parse_dmy(std::string_view s) noexcept {
            if (s.size() != 10 || s[2] != '/' || s[5] != '/') {
                return std::nullopt;
            }
            const auto d = parse_uint(s.substr(0, 2));
            const auto m = parse_uint(s.substr(3, 2));
            const auto y = parse_uint(s.substr(6, 4));
            if (!d || !m || !y) {
                return std::nullopt;
            }
            const std::chrono::year_month_day ymd{
                std::chrono::year{*y},
                std::chrono::month{static_cast<unsigned>(*m)},
                std::chrono::day{static_cast<unsigned>(*d)}};
            if (!ymd.ok()) {
                return std::nullopt;
            }
            return ymd;
        }

        // BP debit/credit columns are signed strings: "-6,64", "+450,00" or empty.
        // Returns nullopt for an empty field; throws on malformed numbers.
        std::optional<money::Money> parse_signed_amount(std::string_view field,
                                                         money::Currency currency) {
            if (field.empty()) {
                return std::nullopt;
            }
            const auto parsed = money::Money::from_string(field, currency);
            if (!parsed.has_value()) {
                throw std::runtime_error(std::string("BP CSV: malformed amount '") +
                                         std::string(field) + "'");
            }
            return parsed;
        }

        std::string lower_copy(std::string_view s) {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        bool contains(std::string_view haystack, std::string_view needle) noexcept {
            return haystack.find(needle) != std::string_view::npos;
        }

    } // namespace

    std::vector<BpRow> parse_bp_csv(std::istream& in, money::Currency currency) {
        std::vector<BpRow> out;
        std::string line;
        bool first = true;
        std::size_t lineno = 0;

        while (std::getline(in, line)) {
            ++lineno;
            const std::string_view sv = rstrip_cr(line);
            if (sv.empty()) {
                continue;
            }
            if (first) {
                first = false;
                continue; // header
            }

            const auto fields = split_fields(sv);
            if (fields.size() != 13) {
                throw std::runtime_error("BP CSV: line " + std::to_string(lineno) +
                                         " has " + std::to_string(fields.size()) +
                                         " fields, expected 13");
            }

            const auto booking = parse_dmy(fields[0]);
            const auto operation = parse_dmy(fields[10]);
            const auto value = parse_dmy(fields[11]);
            if (!booking || !operation || !value) {
                throw std::runtime_error("BP CSV: line " + std::to_string(lineno) +
                                         " has malformed dates");
            }

            const auto debit = parse_signed_amount(fields[8], currency);
            const auto credit = parse_signed_amount(fields[9], currency);
            if (debit.has_value() == credit.has_value()) {
                throw std::runtime_error("BP CSV: line " + std::to_string(lineno) +
                                         " must have exactly one of debit/credit");
            }
            const money::Money amount = debit.has_value() ? *debit : *credit;

            out.push_back(BpRow{
                *booking,
                fields[1],
                fields[2],
                fields[3],
                fields[4],
                fields[5],
                fields[6],
                fields[7],
                amount,
                *operation,
                *value,
            });
        }
        return out;
    }

    std::optional<std::string> map_bp_category(std::string_view categorie_bp,
                                                std::string_view sous_categorie_bp,
                                                bool is_inflow) {
        const auto cat = lower_copy(categorie_bp);
        const auto sub = lower_copy(sous_categorie_bp);

        // INCOME and TRANSFERS take priority over the "à catégoriser" markers so that the
        // SORTED_DATA.xlsx conventions (Rentree d'argent / Virement interne, etc.) survive.
        if (is_inflow) {
            if (contains(sub, "virement interne")) {
                return "transfer-internal";
            }
            if (contains(cat, "rentree") || contains(cat, "revenus")) {
                if (contains(sub, "salaire") || contains(sub, "paie") || contains(sub, "remuneration")) {
                    return "salary";
                }
                if (contains(sub, "allocation") || contains(sub, "caf") || contains(sub, "aide")) {
                    return "social-benefits";
                }
                if (contains(sub, "remboursement")) {
                    return "refunds";
                }
                if (contains(sub, "don") || contains(sub, "cadeau")) {
                    return "gifts";
                }
                if (contains(sub, "revenus locatifs") || contains(sub, "locatif")) {
                    return "rental-income";
                }
                if (contains(sub, "virement logement")) {
                    return "housing-support";
                }
                return "transfers-in";
            }
            return std::nullopt;
        }

        // OUTFLOW branch.
        if (contains(sub, "virement interne")) {
            return "transfer-internal";
        }
        if (contains(cat, "sortie") || contains(cat, "transaction exclue") ||
            contains(cat, "a categoriser")) {
            return "transfer-out";
        }
        if (contains(cat, "logement")) {
            if (contains(sub, "internet") || contains(sub, "telephonie") ||
                contains(sub, "electricite") || contains(sub, "energie") ||
                contains(sub, "gaz") || contains(sub, "eau") || contains(sub, "fioul")) {
                return "utilities";
            }
            return "housing";
        }
        if (contains(cat, "transport")) {
            return "transport";
        }
        if (contains(cat, "alimentation")) {
            if (contains(sub, "restaurant") || contains(sub, "bar") ||
                contains(sub, "cafe") || contains(sub, "fast") ||
                contains(sub, "restauration")) {
                return "dining";
            }
            return "groceries";
        }
        if (contains(cat, "loisirs")) {
            if (contains(sub, "bar")) return "dining";
            return "leisure";
        }
        if (contains(cat, "shopping")) {
            return "shopping";
        }
        if (contains(cat, "sante")) {
            return "health";
        }
        if (contains(cat, "education") || contains(cat, "famille")) {
            return "education-family";
        }
        if (contains(cat, "juridique") || contains(cat, "administratif")) {
            return "admin-legal";
        }
        if (contains(cat, "banque") || contains(cat, "assurance")) {
            if (contains(sub, "frais")) {
                return "bank-fees";
            }
            return "insurance";
        }
        return std::nullopt;
    }

    CleanedBpPair cleanup_bp_taxonomy(std::string_view type_operation,
                                       std::string_view categorie_bp,
                                       std::string_view sous_categorie_bp) {
        const auto cat = lower_copy(categorie_bp);
        const auto type = lower_copy(type_operation);

        std::string clean_cat;
        std::string clean_sub(sous_categorie_bp);

        if (contains(cat, "transaction exclue")) {
            // Direction depends on the operation type — "Virement" out, "Virement recu" in.
            clean_cat = contains(type, "recu") ? "Rentree d'argent" : "Sortie d'argent";
            if (clean_sub.empty() || lower_copy(clean_sub) == "transaction exclue") {
                clean_sub = "Virement interne";
            }
        } else if (contains(cat, "a categoriser - rentree")) {
            clean_cat = "Rentree d'argent";
        } else if (contains(cat, "a categoriser - sortie")) {
            clean_cat = "Sortie d'argent";
        } else if (contains(cat, "revenus") && contains(cat, "rentree")) {
            clean_cat = "Rentree d'argent";
        } else {
            clean_cat = std::string(categorie_bp);
        }

        return {std::move(clean_cat), std::move(clean_sub)};
    }

    void Categorizer::add_rule(const std::string& pattern, std::string category_id,
                               std::regex::flag_type flags) {
        rules_.push_back(Rule{std::regex(pattern, flags), std::move(category_id), std::nullopt});
    }

    void Categorizer::add_rule_with_subcategory(const std::string& pattern, std::string category_id,
                                                 std::string bp_subcategory,
                                                 std::regex::flag_type flags) {
        rules_.push_back(Rule{std::regex(pattern, flags), std::move(category_id),
                               std::move(bp_subcategory)});
    }

    std::optional<std::string> Categorizer::categorize(std::string_view description) const {
        if (const auto m = match(description); m.has_value()) {
            return m->category_id;
        }
        return std::nullopt;
    }

    std::optional<CategorizationMatch> Categorizer::match(std::string_view description) const {
        const std::string s(description);
        for (const auto& rule : rules_) {
            if (std::regex_search(s, rule.pattern)) {
                return CategorizationMatch{rule.category_id, rule.bp_subcategory};
            }
        }
        return std::nullopt;
    }

    void Categorizer::extend(const Categorizer& other) {
        rules_.insert(rules_.end(), other.rules_.begin(), other.rules_.end());
    }

    Categorizer default_overrides() {
        Categorizer c;
        // Subscriptions (leisure)
        c.add_rule(R"(\b(netflix|spotify|deezer|disney|prime\s*video|youtube\s*premium|crunchyroll)\b)",
                   "subscriptions-leisure");
        // Subscriptions (essential utilities/telecom that BP may bucket elsewhere)
        c.add_rule(R"(\b(sfr|orange|bouygues|free\s*mobile|free\s*sas)\b)", "utilities");
        // Steam/gaming → leisure (BP files them as "Video, Musique et jeux" already → leisure,
        // kept here for clarity and to override if BP changes its taxonomy).
        c.add_rule(R"(\b(steam\w*|instant\s*gaming\w*|playstation|xbox|nintendo)\b)", "leisure");
        // Transport
        c.add_rule(R"(\b(tisseo|sncf|ratp|blablacar|uber|bolt|lime|free\s*now|total\s*access|totalenergies)\b)",
                   "transport");
        // Groceries vs dining (helps when BP only says "Alimentation")
        c.add_rule(R"(\b(carrefour|auchan|leclerc|monoprix|lidl|aldi|intermarche|biocoop|naturalia)\b)",
                   "groceries");
        c.add_rule(R"(\b(mc\s*donalds?|burger\s*king|kfc|subway|big\s*fernand|pizza|sushi|crous)\w*)",
                   "dining");
        // Savings transfers
        c.add_rule(R"(\b(virement\s+vers\s+(livret|pea|cto|assurance\s*vie))\b)", "savings");
        return c;
    }

    ledger::Transaction to_transaction(const BpRow& row, std::string_view account_id,
                                       const Categorizer* overrides) {
        std::string description = row.libelle_simplifie;
        if (!row.libelle_operation.empty() && row.libelle_operation != row.libelle_simplifie) {
            description.append(" — ").append(row.libelle_operation);
        }

        const bool inflow = !row.amount.is_negative();
        auto cleaned = cleanup_bp_taxonomy(row.type_operation, row.categorie_bp,
                                            row.sous_categorie_bp);

        std::optional<std::string> category;
        if (overrides != nullptr) {
            std::string haystack = row.libelle_simplifie;
            haystack.append(" ").append(row.libelle_operation);
            if (const auto m = overrides->match(haystack); m.has_value()) {
                category = m->category_id;
                if (m->bp_subcategory.has_value()) {
                    cleaned.subcategory = *m->bp_subcategory;
                }
            }
        }
        if (!category.has_value()) {
            // Run the WT mapper against the CLEANED pair so "Sortie d'argent" / "Virement interne"
            // route to transfer-internal etc.
            category = map_bp_category(cleaned.category, cleaned.subcategory, inflow);
        }

        // Build a stable, account-scoped id. BP's "Reference" is NOT unique (it is shared by
        // recurring transactions like a monthly rent), so we synthesise an id that combines the
        // account, the operation date and the amount with the reference (or libelle as fallback).
        // Tradeoff: a user who genuinely posts two identical transactions on the same day will
        // have them collapsed — acceptable for personal-finance scale.
        std::ostringstream id_oss;
        id_oss << account_id << '|'
               << static_cast<int>(row.operation_date.year()) << '-';
        const unsigned m = static_cast<unsigned>(row.operation_date.month());
        if (m < 10) id_oss << '0';
        id_oss << m << '-';
        const unsigned d = static_cast<unsigned>(row.operation_date.day());
        if (d < 10) id_oss << '0';
        id_oss << d << '|'
               << (!row.reference.empty() ? row.reference : row.libelle_simplifie) << '|'
               << row.amount.minor_units();
        std::string id = id_oss.str();

        return ledger::Transaction(std::move(id), row.operation_date, std::string(account_id),
                                   row.amount, std::move(description), std::move(category),
                                   cleaned.category, cleaned.subcategory, row.type_operation,
                                   false);
    }

    ImportReport import_bp_csv(std::istream& in, const ImportOptions& opts) {
        if (opts.account_id.empty()) {
            throw std::invalid_argument("import_bp_csv: ImportOptions.account_id is required");
        }

        const auto rows = parse_bp_csv(in, opts.currency);
        ImportReport report;
        report.rows_seen = rows.size();
        report.transactions.reserve(rows.size());

        for (const auto& row : rows) {
            if (opts.drop_excluded) {
                const auto t = lower_copy(row.type_operation);
                const auto c = lower_copy(row.categorie_bp);
                if (t.find("transaction exclue") != std::string::npos ||
                    c.find("transaction exclue") != std::string::npos) {
                    ++report.rows_dropped;
                    continue;
                }
            }
            auto tx = to_transaction(row, opts.account_id, opts.overrides);
            if (tx.category_id().has_value()) {
                ++report.categorised;
            } else {
                ++report.uncategorised;
            }
            report.transactions.push_back(std::move(tx));
        }
        return report;
    }

} // namespace wealthtorii::import_
