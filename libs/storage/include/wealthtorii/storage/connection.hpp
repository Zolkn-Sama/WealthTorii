#pragma once

#include <pqxx/pqxx>

#include <memory>
#include <optional>
#include <string>

namespace wealthtorii::storage {

    // Thin wrapper around pqxx::connection that owns the underlying connection and exposes the
    // raw pqxx handle for repository code. Construction throws on invalid URL.
    class Connection {
    public:
        explicit Connection(const std::string& database_url);

        pqxx::connection& raw() noexcept { return *conn_; }
        [[nodiscard]] const pqxx::connection& raw() const noexcept { return *conn_; }

        // Returns the URL read from $DATABASE_URL or std::nullopt.
        [[nodiscard]] static std::optional<std::string> database_url_from_env();

        // Returns a Connection opened from $DATABASE_URL, or nullopt if the env var is unset.
        // Throws on connection failure when the env var is present.
        [[nodiscard]] static std::optional<Connection> from_env();

        // Probes connectivity without raising; useful in tests to decide whether to skip.
        [[nodiscard]] static bool can_connect(const std::string& database_url) noexcept;

    private:
        std::unique_ptr<pqxx::connection> conn_;
    };

} // namespace wealthtorii::storage
