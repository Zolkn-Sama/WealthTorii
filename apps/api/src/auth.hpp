#pragma once

#include <wealthtorii/storage/repository.hpp>

#include <sodium.h>

#include <jwt-cpp/jwt.h>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace wealthtorii::api::auth {

    // libsodium must be initialised once before any crypto call.
    inline bool ensure_sodium() {
        static const int rc = ::sodium_init(); // 0 = ok, 1 = already, -1 = fail
        return rc >= 0;
    }

    // Random 128-bit hex token, used as the opaque user id.
    inline std::string new_id() {
        unsigned char buf[16];
        ::randombytes_buf(buf, sizeof(buf));
        static constexpr char hex[] = "0123456789abcdef";
        std::string out;
        out.reserve(sizeof(buf) * 2);
        for (unsigned char b : buf) {
            out.push_back(hex[b >> 4]);
            out.push_back(hex[b & 0x0F]);
        }
        return out;
    }

    // Argon2id, salt + params embedded in the returned string.
    inline std::string hash_password(const std::string& password) {
        char hashed[crypto_pwhash_STRBYTES];
        if (::crypto_pwhash_str(hashed, password.c_str(), password.size(),
                                crypto_pwhash_OPSLIMIT_INTERACTIVE,
                                crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
            throw std::runtime_error("password hashing failed (out of memory)");
        }
        return std::string(hashed);
    }

    inline bool verify_password(const std::string& stored_hash,
                                const std::string& password) {
        return ::crypto_pwhash_str_verify(stored_hash.c_str(), password.c_str(),
                                          password.size()) == 0;
    }

    inline std::string jwt_secret() {
        if (const char* s = std::getenv("JWT_SECRET"); s != nullptr && s[0] != '\0') {
            return s;
        }
        // Dev fallback — set JWT_SECRET in production.
        return "wealthtorii-dev-secret-change-me";
    }

    inline constexpr const char* kIssuer = "wealthtorii";

    inline std::string make_token(const storage::User& u) {
        const auto now = std::chrono::system_clock::now();
        return jwt::create()
            .set_issuer(kIssuer)
            .set_type("JWT")
            .set_subject(u.id)
            .set_payload_claim("email", jwt::claim(std::string(u.email)))
            .set_payload_claim("plan", jwt::claim(std::string(u.plan)))
            .set_issued_at(now)
            .set_expires_at(now + std::chrono::hours{24})
            .sign(jwt::algorithm::hs256{jwt_secret()});
    }

    struct Claims {
        std::string user_id;
        std::string email;
        std::string plan;
    };

    // Verifies signature, issuer and expiry. Returns nullopt on any failure.
    inline std::optional<Claims> verify_token(const std::string& token) {
        try {
            const auto decoded = jwt::decode(token);
            jwt::verify()
                .allow_algorithm(jwt::algorithm::hs256{jwt_secret()})
                .with_issuer(kIssuer)
                .verify(decoded);
            Claims c;
            c.user_id = decoded.get_subject();
            c.email = decoded.get_payload_claim("email").as_string();
            c.plan = decoded.get_payload_claim("plan").as_string();
            return c;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    // Extracts the token from an "Authorization: Bearer <token>" header value.
    inline std::optional<std::string> bearer(std::string_view header) {
        constexpr std::string_view prefix = "Bearer ";
        if (header.size() <= prefix.size() || header.substr(0, prefix.size()) != prefix) {
            return std::nullopt;
        }
        return std::string(header.substr(prefix.size()));
    }

} // namespace wealthtorii::api::auth
