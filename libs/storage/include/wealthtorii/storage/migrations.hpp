#pragma once

#include "connection.hpp"

#include <filesystem>
#include <string>

namespace wealthtorii::storage {

    // Applies every .sql file under `migrations_dir` in lexicographic order, idempotently.
    // The schema relies on `CREATE TABLE IF NOT EXISTS` etc. — no migration metadata table yet.
    void apply_migrations(Connection& conn, const std::filesystem::path& migrations_dir);

    // Convenience: applies the project default migrations dir (set at compile time).
    void apply_default_migrations(Connection& conn);

} // namespace wealthtorii::storage
