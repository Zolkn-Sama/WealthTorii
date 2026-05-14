#include "wealthtorii/budget/allocation.hpp"

#include <stdexcept>
#include <vector>

namespace wealthtorii::budget {

    BucketAllocation allocate_50_30_20(const money::Money& income) {
        if (income.is_negative() || income.is_zero()) {
            throw std::invalid_argument("allocate_50_30_20: income must be strictly positive");
        }
        const auto parts = money::split_proportional(income, {50, 30, 20});
        BucketAllocation out;
        out.emplace(Bucket::NEEDS, parts[0]);
        out.emplace(Bucket::WANTS, parts[1]);
        out.emplace(Bucket::SAVINGS_INVEST, parts[2]);
        return out;
    }

    std::map<std::string, money::Money> distribute_evenly(const money::Money& bucket_envelope,
                                                          const CategoryRegistry& registry,
                                                          Bucket bucket) {
        const auto cats = registry.in_bucket(bucket);
        std::map<std::string, money::Money> out;
        if (cats.empty()) {
            return out;
        }
        std::vector<std::int64_t> weights(cats.size(), 1);
        const auto parts = money::split_proportional(bucket_envelope, weights);
        for (std::size_t i = 0; i < cats.size(); ++i) {
            out.emplace(cats[i]->id(), parts[i]);
        }
        return out;
    }

} // namespace wealthtorii::budget
