#include <gtest/gtest.h>

#include "wealthtorii/import/bp_csv.hpp"

#include <sstream>

using namespace wealthtorii::import_;
using wealthtorii::money::Currency;
using wealthtorii::money::Money;

namespace {
    // Minimal synthetic CSV mimicking BP's header + 3 rows.
    constexpr const char* kSampleCsv =
        "Date de comptabilisation;Libelle simplifie;Libelle operation;Reference;"
        "Informations complementaires;Type operation;Categorie;Sous categorie;"
        "Debit;Credit;Date operation;Date de valeur;Pointage operation\r\n"
        "30/04/2026;STEAMGAMES;Steam Purchase 75PARIS 8;5J4D609;280426 CB****6440-;"
        "Carte bancaire;Loisirs et vacances;Video, Musique et jeux;-6,64;;"
        "30/04/2026;30/04/2026;0\r\n"
        "29/04/2026;VIVIANE LANDRECY;VIR INST VIVIANE LANDRECY;WL3TDN0;Mois d Avril-NOTPROVIDED;"
        "Virement recu;A categoriser - rentree d'argent;Virement recu - a categoriser;;+450,00;"
        "29/04/2026;29/04/2026;0\r\n"
        "28/04/2026;SFR;PRLV SEPA SFR;0FEFPHG;SFR Prlvt SEPA;Prelevement;Logement - maison;"
        "Internet et telephonie;-20,99;;28/04/2026;28/04/2026;0\r\n";
}

TEST(BpCsvParse, ParsesThreeRowsSkippingHeader) {
    std::istringstream in(kSampleCsv);
    const auto rows = parse_bp_csv(in);

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].libelle_simplifie, "STEAMGAMES");
    EXPECT_EQ(rows[0].amount, Money(-664, Currency::EUR));
    EXPECT_EQ(rows[1].amount, Money(45000, Currency::EUR));
    EXPECT_EQ(rows[2].categorie_bp, "Logement - maison");
    EXPECT_EQ(rows[2].sous_categorie_bp, "Internet et telephonie");
}

TEST(BpCsvParse, ParsesDmyCorrectly) {
    std::istringstream in(kSampleCsv);
    const auto rows = parse_bp_csv(in);

    EXPECT_EQ(static_cast<int>(rows[0].operation_date.year()), 2026);
    EXPECT_EQ(static_cast<unsigned>(rows[0].operation_date.month()), 4u);
    EXPECT_EQ(static_cast<unsigned>(rows[0].operation_date.day()), 30u);
}

TEST(BpCsvParse, RejectsRowWithBothDebitAndCredit) {
    std::istringstream in(
        "Date de comptabilisation;Libelle simplifie;Libelle operation;Reference;"
        "Informations complementaires;Type operation;Categorie;Sous categorie;"
        "Debit;Credit;Date operation;Date de valeur;Pointage operation\r\n"
        "30/04/2026;X;X;R;;Carte bancaire;Alimentation;X;-10,00;+10,00;30/04/2026;30/04/2026;0\r\n");
    EXPECT_THROW(static_cast<void>(parse_bp_csv(in)), std::runtime_error);
}

TEST(BpCsvParse, RejectsRowWithNeitherDebitNorCredit) {
    std::istringstream in(
        "Date de comptabilisation;Libelle simplifie;Libelle operation;Reference;"
        "Informations complementaires;Type operation;Categorie;Sous categorie;"
        "Debit;Credit;Date operation;Date de valeur;Pointage operation\r\n"
        "30/04/2026;X;X;R;;Carte bancaire;Alimentation;X;;;30/04/2026;30/04/2026;0\r\n");
    EXPECT_THROW(static_cast<void>(parse_bp_csv(in)), std::runtime_error);
}

TEST(BpCsvParse, RejectsRowWithWrongFieldCount) {
    std::istringstream in(
        "Date de comptabilisation;Libelle simplifie;Libelle operation;Reference;"
        "Informations complementaires;Type operation;Categorie;Sous categorie;"
        "Debit;Credit;Date operation;Date de valeur;Pointage operation\r\n"
        "30/04/2026;X;X;R;;Carte bancaire;Alimentation;X;-10,00;\r\n");
    EXPECT_THROW(static_cast<void>(parse_bp_csv(in)), std::runtime_error);
}

// --- BP category mapping ---

TEST(BpMap, AlimentationDefaultsToGroceries) {
    EXPECT_EQ(map_bp_category("Alimentation", "Hyper/supermarche", false), "groceries");
}

TEST(BpMap, AlimentationRestaurantBecomesDining) {
    EXPECT_EQ(map_bp_category("Alimentation", "Restaurant", false), "dining");
    EXPECT_EQ(map_bp_category("Alimentation", "Bar et cafe", false), "dining");
}

TEST(BpMap, LogementMaisonInternetBecomesUtilities) {
    EXPECT_EQ(map_bp_category("Logement - maison", "Internet et telephonie", false), "utilities");
}

TEST(BpMap, LogementMaisonDefaultsToHousing) {
    EXPECT_EQ(map_bp_category("Logement - maison", "Loyer", false), "housing");
}

TEST(BpMap, TransportsMapsToTransport) {
    EXPECT_EQ(map_bp_category("Transports", "Transports en commun", false), "transport");
    EXPECT_EQ(map_bp_category("Transports", "Entretien de vehicule", false), "transport");
}

TEST(BpMap, LoisirsMapsToLeisure) {
    EXPECT_EQ(map_bp_category("Loisirs et vacances", "Video, Musique et jeux", false), "leisure");
}

TEST(BpMap, BankFeesRoutedSeparatelyFromInsurance) {
    EXPECT_EQ(map_bp_category("Banque et assurances", "Frais bancaires", false), "bank-fees");
    EXPECT_EQ(map_bp_category("Banque et assurances", "Assurance habitation", false), "insurance");
}

TEST(BpMap, ACategoriserRoutesToTransfersTaxonomy) {
    // Aligned with SORTED_DATA.xlsx: "A categoriser - rentree d'argent" / "Virement recu - a
    // categoriser" → transfers-in. "A categoriser - sortie d'argent" → transfer-out.
    EXPECT_EQ(map_bp_category("A categoriser - rentree d'argent",
                               "Virement recu - a categoriser", true),
              "transfers-in");
    EXPECT_EQ(map_bp_category("A categoriser - sortie d'argent",
                               "Virement emis - a categoriser", false),
              "transfer-out");
}

TEST(BpMap, RevenusInflowMapsToIncomeSubcategories) {
    EXPECT_EQ(map_bp_category("Revenus et rentrees d'argent", "Salaire", true), "salary");
    EXPECT_EQ(map_bp_category("Revenus et rentrees d'argent", "Allocation logement", true),
              "social-benefits");
    // Generic "Autre" under Revenus now falls into the catch-all transfers-in.
    EXPECT_EQ(map_bp_category("Revenus et rentrees d'argent", "Autre", true), "transfers-in");
    // Cleaned-cat path (Rentree d'argent comes from cleanup_bp_taxonomy).
    EXPECT_EQ(map_bp_category("Rentree d'argent", "Dons et cadeaux recus", true), "gifts");
    EXPECT_EQ(map_bp_category("Rentree d'argent", "Revenus locatifs", true), "rental-income");
    EXPECT_EQ(map_bp_category("Rentree d'argent", "Virement Logement", true), "housing-support");
    EXPECT_EQ(map_bp_category("Rentree d'argent", "Virement interne", true), "transfer-internal");
}

TEST(BpMap, VirementInterneOnOutflowIsTransferInternal) {
    EXPECT_EQ(map_bp_category("Sortie d'argent", "Virement interne", false), "transfer-internal");
}

TEST(CleanupBpTaxonomy, RenamesACategoriserCategories) {
    EXPECT_EQ(cleanup_bp_taxonomy("Virement recu", "A categoriser - rentree d'argent",
                                   "Virement recu - a categoriser").category,
              "Rentree d'argent");
    EXPECT_EQ(cleanup_bp_taxonomy("Virement", "A categoriser - sortie d'argent",
                                   "Virement emis - a categoriser").category,
              "Sortie d'argent");
    EXPECT_EQ(cleanup_bp_taxonomy("Virement recu", "Revenus et rentrees d'argent",
                                   "Salaire").category,
              "Rentree d'argent");
}

TEST(CleanupBpTaxonomy, RoutesTransactionExclueByOperationType) {
    const auto out_in = cleanup_bp_taxonomy("Virement recu", "Transaction exclue",
                                             "Transaction exclue");
    EXPECT_EQ(out_in.category, "Rentree d'argent");
    EXPECT_EQ(out_in.subcategory, "Virement interne");

    const auto out_out = cleanup_bp_taxonomy("Virement", "Transaction exclue", "Transaction exclue");
    EXPECT_EQ(out_out.category, "Sortie d'argent");
    EXPECT_EQ(out_out.subcategory, "Virement interne");
}

// --- Categorizer regex ---

TEST(Categorizer, FirstRuleWins) {
    Categorizer c;
    c.add_rule(R"(\bnetflix\b)", "subscriptions-leisure");
    c.add_rule(R"(.*)", "shopping");

    EXPECT_EQ(c.categorize("Netflix SARL Paris"), "subscriptions-leisure");
    EXPECT_EQ(c.categorize("anything else"), "shopping");
}

TEST(Categorizer, NoMatchReturnsNullopt) {
    Categorizer c;
    c.add_rule(R"(\bnetflix\b)", "subscriptions-leisure");
    EXPECT_FALSE(c.categorize("CARREFOUR CITY").has_value());
}

TEST(Categorizer, IsCaseInsensitiveByDefault) {
    Categorizer c;
    c.add_rule(R"(steam)", "leisure");
    EXPECT_EQ(c.categorize("STEAMGAMES"), "leisure");
}

TEST(DefaultOverrides, KnowsCommonMerchants) {
    const auto c = default_overrides();
    EXPECT_EQ(c.categorize("NETFLIX SARL"), "subscriptions-leisure");
    EXPECT_EQ(c.categorize("SFR"), "utilities");
    EXPECT_EQ(c.categorize("TISSEO METRO"), "transport");
    EXPECT_EQ(c.categorize("CARREFOUR CITY NFC31"), "groceries");
    EXPECT_EQ(c.categorize("MC DONALDS TOULOUSE"), "dining");
}

// --- import_bp_csv pipeline ---

TEST(ImportBpCsv, RequiresAccountId) {
    std::istringstream in(kSampleCsv);
    EXPECT_THROW(static_cast<void>(import_bp_csv(in, ImportOptions{})), std::invalid_argument);
}

TEST(ImportBpCsv, ProducesTransactionsAndStats) {
    std::istringstream in(kSampleCsv);
    ImportOptions opts;
    opts.account_id = "BP_CHECKING";
    const auto report = import_bp_csv(in, opts);

    EXPECT_EQ(report.rows_seen, 3u);
    EXPECT_EQ(report.transactions.size(), 3u);
    EXPECT_EQ(report.rows_dropped, 0u);
    // Two clean categorisations (STEAMGAMES Loisirs → leisure, SFR Logement-Internet → utilities),
    // one "A categoriser" inflow stays uncategorised.
    EXPECT_GE(report.categorised, 2u);
}

TEST(ImportBpCsv, OverrideTakesPriorityOverBpMapping) {
    Categorizer override;
    override.add_rule(R"(\bsteam\b)", "subscriptions-leisure");

    std::istringstream in(kSampleCsv);
    ImportOptions opts;
    opts.account_id = "BP_CHECKING";
    opts.overrides = &override;
    const auto report = import_bp_csv(in, opts);

    const auto& steam = report.transactions[0];
    ASSERT_TRUE(steam.category_id().has_value());
    EXPECT_EQ(*steam.category_id(), "subscriptions-leisure");
}

TEST(ImportBpCsv, DropsTransactionExcluedWhenAsked) {
    constexpr const char* csv_with_excluded =
        "Date de comptabilisation;Libelle simplifie;Libelle operation;Reference;"
        "Informations complementaires;Type operation;Categorie;Sous categorie;"
        "Debit;Credit;Date operation;Date de valeur;Pointage operation\r\n"
        "14/04/2026;M ENZO LANDRECY;VIR M ENZO LANDRECY;X;;Virement recu;Transaction exclue;"
        "Transaction exclue;;+350,00;14/04/2026;14/04/2026;0\r\n"
        "30/04/2026;STEAMGAMES;Steam Purchase;Y;;Carte bancaire;Loisirs et vacances;"
        "Video;-6,64;;30/04/2026;30/04/2026;0\r\n";

    std::istringstream in(csv_with_excluded);
    ImportOptions opts;
    opts.account_id = "BP_CHECKING";
    opts.drop_excluded = true;
    const auto report = import_bp_csv(in, opts);

    EXPECT_EQ(report.rows_seen, 2u);
    EXPECT_EQ(report.rows_dropped, 1u);
    EXPECT_EQ(report.transactions.size(), 1u);
}

TEST(ImportBpCsv, KeepsTransactionExcluedWhenAsked) {
    constexpr const char* csv_with_excluded =
        "Date de comptabilisation;Libelle simplifie;Libelle operation;Reference;"
        "Informations complementaires;Type operation;Categorie;Sous categorie;"
        "Debit;Credit;Date operation;Date de valeur;Pointage operation\r\n"
        "14/04/2026;M ENZO LANDRECY;VIR M ENZO LANDRECY;X;;Virement recu;Transaction exclue;"
        "Transaction exclue;;+350,00;14/04/2026;14/04/2026;0\r\n";

    std::istringstream in(csv_with_excluded);
    ImportOptions opts;
    opts.account_id = "BP_CHECKING";
    opts.drop_excluded = false;
    const auto report = import_bp_csv(in, opts);

    EXPECT_EQ(report.transactions.size(), 1u);
    EXPECT_EQ(report.rows_dropped, 0u);
}
