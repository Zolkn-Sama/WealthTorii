#include "wealthtorii/storage/connection.hpp"

#include <cstdlib>
#include <stdexcept>

namespace wealthtorii::storage {

    Connection::Connection(const std::string& database_url)
        : conn_(std::make_unique<pqxx::connection>(database_url)) {}

    std::optional<std::string> Connection::database_url_from_env() {
        const char* url = std::getenv("DATABASE_URL");
        if (url == nullptr || url[0] == '\0') {
            return std::nullopt;
        }
        return std::string{url};
    }

    std::optional<Connection> Connection::from_env() {
        const auto url = database_url_from_env();
        if (!url.has_value()) return std::nullopt;
        return Connection{*url};
    }

    bool Connection::can_connect(const std::string& database_url) noexcept {
        try {
            const pqxx::connection conn(database_url);
            return conn.is_open();
        } catch (...) {
            return false;
        }
    }

} // namespace wealthtorii::storage
