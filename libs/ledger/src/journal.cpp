#include "wealthtorii/ledger/journal.hpp"

#include <utility>

namespace wealthtorii::ledger {

    namespace {
        bool in_month(std::chrono::year_month_day date, std::chrono::year_month month) noexcept {
            return date.year() == month.year() && date.month() == month.month();
        }
    } // namespace

    void Journal::add(Transaction tx) {
        transactions_.push_back(std::move(tx));
    }

    std::vector<Transaction> Journal::for_month(std::chrono::year_month month) const {
        std::vector<Transaction> out;
        for (const auto& tx : transactions_) {
            if (in_month(tx.date(), month)) {
                out.push_back(tx);
            }
        }
        return out;
    }

    std::vector<Transaction> Journal::for_account(std::string_view account_id) const {
        std::vector<Transaction> out;
        for (const auto& tx : transactions_) {
            if (tx.account_id() == account_id) {
                out.push_back(tx);
            }
        }
        return out;
    }

    std::vector<Transaction> Journal::for_category(std::string_view category_id) const {
        std::vector<Transaction> out;
        for (const auto& tx : transactions_) {
            if (tx.category_id().has_value() && *tx.category_id() == category_id) {
                out.push_back(tx);
            }
        }
        return out;
    }

    money::Money Journal::total_for_month(std::chrono::year_month month,
                                          money::Currency currency) const {
        money::Money total = money::Money::zero(currency);
        for (const auto& tx : transactions_) {
            if (in_month(tx.date(), month)) {
                total += tx.amount();
            }
        }
        return total;
    }

    money::Money Journal::inflow_for_month(std::chrono::year_month month,
                                           money::Currency currency) const {
        money::Money total = money::Money::zero(currency);
        for (const auto& tx : transactions_) {
            if (in_month(tx.date(), month) && tx.is_inflow()) {
                total += tx.amount();
            }
        }
        return total;
    }

    std::map<std::string, money::Money> Journal::outflow_by_category(
        std::chrono::year_month month, money::Currency currency) const {
        std::map<std::string, money::Money> out;
        for (const auto& tx : transactions_) {
            if (!in_month(tx.date(), month) || !tx.is_outflow()) {
                continue;
            }
            const auto key = tx.category_id().value_or("");
            auto [it, inserted] = out.try_emplace(key, money::Money::zero(currency));
            it->second += -tx.amount(); // store outflows as positive magnitudes
        }
        return out;
    }

} // namespace wealthtorii::ledger
