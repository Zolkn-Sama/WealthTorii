#include "wealthtorii/storage/migrations.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifndef WEALTHTORII_DEFAULT_MIGRATIONS_DIR
#define WEALTHTORII_DEFAULT_MIGRATIONS_DIR ""
#endif

namespace wealthtorii::storage {

    void apply_migrations(Connection& conn, const std::filesystem::path& migrations_dir) {
        if (!std::filesystem::exists(migrations_dir)) {
            throw std::runtime_error("migrations dir not found: " + migrations_dir.string());
        }
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(migrations_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".sql") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());

        for (const auto& file : files) {
            std::ifstream in(file);
            if (!in) {
                throw std::runtime_error("cannot read migration " + file.string());
            }
            std::ostringstream content;
            content << in.rdbuf();
            pqxx::work tx(conn.raw());
            tx.exec(content.str());
            tx.commit();
        }
    }

    void apply_default_migrations(Connection& conn) {
        apply_migrations(conn, WEALTHTORII_DEFAULT_MIGRATIONS_DIR);
    }

} // namespace wealthtorii::storage
