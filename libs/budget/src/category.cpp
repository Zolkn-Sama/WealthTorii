#include "wealthtorii/budget/category.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace wealthtorii::budget {

    namespace {
        constexpr std::array<std::pair<std::string_view, Bucket>, 5> kBucketLabels{{
            {"NEEDS", Bucket::NEEDS},
            {"WANTS", Bucket::WANTS},
            {"SAVINGS_INVEST", Bucket::SAVINGS_INVEST},
            {"INCOME", Bucket::INCOME},
            {"TRANSFERS", Bucket::TRANSFERS},
        }};

        void require_non_empty(std::string_view value, std::string_view field) {
            if (value.empty()) {
                throw std::invalid_argument(std::string(field) + " cannot be empty");
            }
        }
    } // namespace

    std::string_view to_string(const Bucket bucket) noexcept {
        for (const auto& [label, b] : kBucketLabels) {
            if (b == bucket) return label;
        }
        return "UNKNOWN";
    }

    std::optional<Bucket> bucket_from_string(std::string_view value) noexcept {
        for (const auto& [label, b] : kBucketLabels) {
            if (label == value) return b;
        }
        return std::nullopt;
    }

    std::ostream& operator<<(std::ostream& os, const Bucket bucket) {
        os << to_string(bucket);
        return os;
    }

    Category::Category(std::string id, std::string name, Bucket bucket)
        : id_(std::move(id)), name_(std::move(name)), bucket_(bucket) {
        require_non_empty(id_, "Category.id");
        require_non_empty(name_, "Category.name");
    }

    void CategoryRegistry::add(Category category) {
        if (find(category.id()) != nullptr) {
            throw std::invalid_argument("CategoryRegistry: duplicate id '" + category.id() + "'");
        }
        categories_.push_back(std::move(category));
    }

    const Category* CategoryRegistry::find(std::string_view id) const noexcept {
        for (const auto& c : categories_) {
            if (c.id() == id) {
                return &c;
            }
        }
        return nullptr;
    }

    std::vector<const Category*> CategoryRegistry::in_bucket(Bucket bucket) const {
        std::vector<const Category*> out;
        for (const auto& c : categories_) {
            if (c.bucket() == bucket) {
                out.push_back(&c);
            }
        }
        return out;
    }

    CategoryRegistry default_registry() {
        CategoryRegistry r;
        // NEEDS
        r.add({"housing", "Logement", Bucket::NEEDS});
        r.add({"utilities", "Énergie & télécoms", Bucket::NEEDS});
        r.add({"groceries", "Courses", Bucket::NEEDS});
        r.add({"transport", "Voiture & transports", Bucket::NEEDS});
        r.add({"insurance", "Assurances", Bucket::NEEDS});
        r.add({"subscriptions-essential", "Abonnements essentiels", Bucket::NEEDS});
        r.add({"health", "Santé", Bucket::NEEDS});
        r.add({"education-family", "Éducation & famille", Bucket::NEEDS});
        r.add({"admin-legal", "Juridique & administratif", Bucket::NEEDS});
        r.add({"bank-fees", "Frais bancaires", Bucket::NEEDS});
        // WANTS
        r.add({"dining", "Restaurants & sorties", Bucket::WANTS});
        r.add({"leisure", "Loisirs", Bucket::WANTS});
        r.add({"shopping", "Achats plaisir", Bucket::WANTS});
        r.add({"subscriptions-leisure", "Abonnements loisirs", Bucket::WANTS});
        // SAVINGS / INVEST
        r.add({"savings", "Épargne", Bucket::SAVINGS_INVEST});
        r.add({"investments", "Investissements", Bucket::SAVINGS_INVEST});
        // INCOME (tag-only — not part of 50/30/20 allocation)
        r.add({"salary", "Salaire", Bucket::INCOME});
        r.add({"social-benefits", "Aides & allocations", Bucket::INCOME});
        r.add({"transfers-in", "Virements reçus", Bucket::INCOME});
        r.add({"refunds", "Remboursements", Bucket::INCOME});
        r.add({"gifts", "Dons & cadeaux reçus", Bucket::INCOME});
        r.add({"rental-income", "Revenus locatifs", Bucket::INCOME});
        r.add({"housing-support", "Aide logement (famille)", Bucket::INCOME});
        // TRANSFERS — neutral movements, excluded from inflow/outflow reports.
        r.add({"transfer-internal", "Virement interne (entre comptes)", Bucket::TRANSFERS});
        r.add({"transfer-out", "Virement sortant (à catégoriser)", Bucket::TRANSFERS});
        return r;
    }

} // namespace wealthtorii::budget
