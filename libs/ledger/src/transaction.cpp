#include "wealthtorii/ledger/transaction.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace wealthtorii::ledger {

    namespace {
        void require_non_empty(std::string_view value, std::string_view field) {
            if (value.empty()) {
                throw std::invalid_argument(std::string(field) + " cannot be empty");
            }
        }

        void require_valid_date(std::chrono::year_month_day date) {
            if (!date.ok()) {
                throw std::invalid_argument("Transaction.date is not a valid calendar date");
            }
        }
    } // namespace

    Transaction::Transaction(std::string id,
                             std::chrono::year_month_day date,
                             std::string account_id,
                             money::Money amount,
                             std::string description,
                             std::optional<std::string> category_id,
                             std::optional<std::string> bp_category,
                             std::optional<std::string> bp_subcategory,
                             std::string type_operation,
                             bool is_reconciled)
        : id_(std::move(id)),
          date_(date),
          account_id_(std::move(account_id)),
          amount_(amount),
          description_(std::move(description)),
          category_id_(std::move(category_id)),
          bp_category_(std::move(bp_category)),
          bp_subcategory_(std::move(bp_subcategory)),
          type_operation_(std::move(type_operation)),
          is_reconciled_(is_reconciled) {
        require_non_empty(id_, "Transaction.id");
        require_non_empty(account_id_, "Transaction.account_id");
        require_valid_date(date_);
        if (amount_.is_zero()) {
            throw std::invalid_argument("Transaction.amount must not be zero");
        }
        if (category_id_.has_value()) {
            require_non_empty(*category_id_, "Transaction.category_id");
        }
    }

    void Transaction::assign_category(std::string category_id) {
        require_non_empty(category_id, "Transaction.category_id");
        category_id_ = std::move(category_id);
    }

    void Transaction::clear_category() noexcept {
        category_id_.reset();
    }

    void Transaction::set_bp_category(std::string category, std::string subcategory) {
        bp_category_ = std::move(category);
        bp_subcategory_ = std::move(subcategory);
    }

    std::string to_string(const Transaction& tx) {
        std::ostringstream oss;
        oss << "tx[" << tx.id() << "] "
            << static_cast<int>(tx.date().year()) << '-'
            << static_cast<unsigned>(tx.date().month()) << '-'
            << static_cast<unsigned>(tx.date().day())
            << " account=" << tx.account_id()
            << " amount=" << tx.amount();
        if (tx.category_id().has_value()) {
            oss << " category=" << *tx.category_id();
        }
        if (!tx.description().empty()) {
            oss << " \"" << tx.description() << '"';
        }
        return oss.str();
    }

    std::ostream& operator<<(std::ostream& os, const Transaction& tx) {
        os << to_string(tx);
        return os;
    }

} // namespace wealthtorii::ledger
