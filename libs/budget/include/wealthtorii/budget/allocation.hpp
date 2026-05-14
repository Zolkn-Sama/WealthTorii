#pragma once

#include "category.hpp"

#include <wealthtorii/money/money.hpp>

#include <map>

namespace wealthtorii::budget {

    // Result of an allocation rule: one positive amount per bucket. The sum of amounts equals
    // the input income to the cent (the residual centime is distributed to the largest bucket
    // first, cf. money::split_proportional).
    using BucketAllocation = std::map<Bucket, money::Money>;

    // Applies the 50/30/20 rule. Income must be strictly positive.
    [[nodiscard]] BucketAllocation allocate_50_30_20(const money::Money& income);

    // Optional helper: spreads a bucket envelope across the registry's categories of that bucket
    // using equal weights. Returns an empty map if the bucket has no categories.
    [[nodiscard]] std::map<std::string, money::Money> distribute_evenly(
        const money::Money& bucket_envelope,
        const CategoryRegistry& registry,
        Bucket bucket);

} // namespace wealthtorii::budget
