#pragma once

#include <wealthtorii/ledger/transaction.hpp>
#include <wealthtorii/money/money.hpp>

#include <chrono>
#include <iosfwd>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace wealthtorii::import_ {

    // One row of a Banque Populaire CSV export. Mirrors the 13 columns we observed:
    //   Date de comptabilisation ; Libelle simplifie ; Libelle operation ; Reference ;
    //   Informations complementaires ; Type operation ; Categorie ; Sous categorie ;
    //   Debit ; Credit ; Date operation ; Date de valeur ; Pointage operation
    struct BpRow {
        std::chrono::year_month_day booking_date;
        std::string libelle_simplifie;
        std::string libelle_operation;
        std::string reference;
        std::string informations;
        std::string type_operation;
        std::string categorie_bp;
        std::string sous_categorie_bp;
        money::Money amount; // signed: debit -> negative, credit -> positive
        std::chrono::year_month_day operation_date;
        std::chrono::year_month_day value_date;
    };

    // Parses every row of a Banque Populaire CSV stream. Skips the header line. Throws
    // std::runtime_error on malformed rows. UTF-8/ASCII tolerated (BP strips accents on export).
    [[nodiscard]] std::vector<BpRow> parse_bp_csv(std::istream& in,
                                                  money::Currency currency = money::Currency::EUR);

    // Maps a Banque Populaire (Categorie, Sous categorie) to a WealthTorii category id.
    // Returns nullopt for ambiguous or excluded categories (e.g. "A categoriser …",
    // "Transaction exclue") so the caller can leave them uncategorised or run a regex override.
    // is_inflow disambiguates Alimentation→groceries vs dining via the sub-category.
    [[nodiscard]] std::optional<std::string> map_bp_category(std::string_view categorie_bp,
                                                              std::string_view sous_categorie_bp,
                                                              bool is_inflow);

    // Produces the cleaned-up (Categorie, Sous categorie) pair we want to *preserve* on the
    // Transaction (and re-emit on export). Aligned with SORTED_DATA.xlsx conventions:
    //   - "A categoriser - rentree d'argent" → "Rentree d'argent"
    //   - "A categoriser - sortie d'argent"  → "Sortie d'argent"
    //   - "Transaction exclue" with type "Virement"      → "Sortie d'argent" / "Virement interne"
    //   - "Transaction exclue" with type "Virement recu" → "Rentree d'argent" / "Virement interne"
    //   - "Revenus et rentrees d'argent" → "Rentree d'argent"
    // Subcategory is preserved verbatim unless explicitly remapped.
    struct CleanedBpPair {
        std::string category;
        std::string subcategory;
    };
    [[nodiscard]] CleanedBpPair cleanup_bp_taxonomy(std::string_view type_operation,
                                                    std::string_view categorie_bp,
                                                    std::string_view sous_categorie_bp);

    struct CategorizationMatch {
        std::string category_id;                    // canonical WealthTorii category
        std::optional<std::string> bp_subcategory;  // optional BP sub-cat override for export
    };

    // Regex-based override layer. Patterns are tested in insertion order; first match wins.
    class Categorizer {
    public:
        // Adds a rule overriding only the WealthTorii category.
        void add_rule(const std::string& pattern, std::string category_id,
                      std::regex::flag_type flags = std::regex::ECMAScript | std::regex::icase);

        // Adds a rule overriding both the WealthTorii category and the BP sub-category that
        // gets emitted on export.
        void add_rule_with_subcategory(const std::string& pattern, std::string category_id,
                                        std::string bp_subcategory,
                                        std::regex::flag_type flags =
                                            std::regex::ECMAScript | std::regex::icase);

        [[nodiscard]] std::optional<std::string> categorize(std::string_view description) const;
        [[nodiscard]] std::optional<CategorizationMatch> match(std::string_view description) const;

        // Appends every rule from `other` after the rules already in *this. Useful when layering
        // user rules (highest priority — inserted first) over a base set such as
        // default_overrides() (appended last).
        void extend(const Categorizer& other);

        [[nodiscard]] std::size_t size() const noexcept { return rules_.size(); }

    private:
        struct Rule {
            std::regex pattern;
            std::string category_id;
            std::optional<std::string> bp_subcategory;
        };
        std::vector<Rule> rules_;
    };

    // A small default override set tuned for common French merchants the BP categoriser misses
    // or coarse-bins (Netflix/Spotify/Steam into subscriptions-leisure, OPEN/SNCF into transport,
    // etc.). Users can extend or replace it.
    [[nodiscard]] Categorizer default_overrides();

    struct ImportOptions {
        std::string account_id;
        money::Currency currency = money::Currency::EUR;
        const Categorizer* overrides = nullptr; // optional, takes priority over BP mapping
        bool drop_excluded = false;             // default off — "Transaction exclue" rows are
                                                // re-tagged as transfer-internal and kept (cf.
                                                // SORTED_DATA.xlsx conventions). Set true to
                                                // revert to BP behaviour.
    };

    struct ImportReport {
        std::vector<ledger::Transaction> transactions;
        std::size_t rows_seen = 0;
        std::size_t rows_dropped = 0;
        std::size_t categorised = 0;
        std::size_t uncategorised = 0;
    };

    [[nodiscard]] ImportReport import_bp_csv(std::istream& in, const ImportOptions& opts);

    // Converts a single BpRow to a Transaction. The category resolution order is:
    //   1. overrides->categorize(libelle_simplifie + " " + libelle_operation), if any matches
    //   2. map_bp_category(categorie_bp, sous_categorie_bp, is_inflow)
    //   3. uncategorised
    [[nodiscard]] ledger::Transaction to_transaction(const BpRow& row,
                                                      std::string_view account_id,
                                                      const Categorizer* overrides = nullptr);

} // namespace wealthtorii::import_
