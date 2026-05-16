-- User accounts (application login) + freemium plan, and per-user tenancy on
-- financial accounts. Distinct from the `accounts` table, which models *bank*
-- accounts (ledger::Account).
--
-- plan: 'free' | 'premium'. Premium-only endpoints are gated in the API layer.
-- password_hash holds a self-contained Argon2id string (libsodium
-- crypto_pwhash_str: algo + params + salt + hash), so no separate salt column.

CREATE TABLE IF NOT EXISTS users (
    id            TEXT PRIMARY KEY,
    email         TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    plan          TEXT NOT NULL DEFAULT 'free' CHECK (plan IN ('free', 'premium')),
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Multi-tenancy: every financial account belongs to a user. Nullable so the
-- local CLI (which has no auth) can keep creating user-less rows; the API
-- always sets and filters on user_id.
ALTER TABLE accounts
    ADD COLUMN IF NOT EXISTS user_id TEXT REFERENCES users(id) ON DELETE CASCADE;

CREATE INDEX IF NOT EXISTS idx_accounts_user ON accounts (user_id);
