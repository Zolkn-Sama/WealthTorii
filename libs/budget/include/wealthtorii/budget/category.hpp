#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace wealthtorii::budget {

    // Spending buckets used by the 50/30/20 allocation rule, plus INCOME for inflow categories
    // (salary, social benefits, transfers received). INCOME is intentionally NOT part of the
    // allocation rule's output — it tags inflows so reports can decompose where money comes from.
    //   NEEDS          : 50% target — logement, voiture, courses, abonnements essentiels
    //   WANTS          : 30% target — loisirs, restaurants, abonnements non essentiels
    //   SAVINGS_INVEST : 20% target — épargne, livrets, investissements
    //   INCOME         : tag-only — salaire, CAF, virements reçus, remboursements
    enum class Bucket {
        NEEDS,
        WANTS,
        SAVINGS_INVEST,
        INCOME,
        TRANSFERS,  // neutral internal/transit movements — excluded from 50/30/20 and from
                    // spending/income reports (they net out across accounts).
    };

    [[nodiscard]] std::string_view to_string(Bucket bucket) noexcept;
    [[nodiscard]] std::optional<Bucket> bucket_from_string(std::string_view value) noexcept;

    class Category {
    public:
        Category(std::string id, std::string name, Bucket bucket);

        [[nodiscard]] const std::string& id() const noexcept { return id_; }
        [[nodiscard]] const std::string& name() const noexcept { return name_; }
        [[nodiscard]] Bucket bucket() const noexcept { return bucket_; }

        [[nodiscard]] friend bool operator==(const Category& lhs, const Category& rhs) noexcept = default;

    private:
        std::string id_;
        std::string name_;
        Bucket bucket_;
    };

    // Lookup table for categories. Ids are unique; adding a duplicate id throws.
    class CategoryRegistry {
    public:
        CategoryRegistry() = default;

        void add(Category category);

        [[nodiscard]] const Category* find(std::string_view id) const noexcept;
        [[nodiscard]] const std::vector<Category>& all() const noexcept { return categories_; }
        [[nodiscard]] std::size_t size() const noexcept { return categories_.size(); }

        // Convenience: filter by bucket.
        [[nodiscard]] std::vector<const Category*> in_bucket(Bucket bucket) const;

    private:
        std::vector<Category> categories_;
    };

    // Default WealthTorii category set tuned for a French personal finance user:
    //   NEEDS    : housing, utilities, groceries, transport, insurance, subscriptions-essential
    //   WANTS    : dining, leisure, shopping, subscriptions-leisure
    //   SAVINGS  : savings, investments
    [[nodiscard]] CategoryRegistry default_registry();

    std::ostream& operator<<(std::ostream& os, Bucket bucket);

} // namespace wealthtorii::budget
