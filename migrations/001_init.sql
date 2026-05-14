-- WealthTorii initial schema.
-- Money is stored as BIGINT minor units (cf. ADR-0001) + currency CHAR(3).
-- Categories live in code (cf. budget::default_registry) — the DB only stores ids as text,
-- so user-defined categories can be added later without a migration.

CREATE TABLE IF NOT EXISTS accounts (
    id           TEXT PRIMARY KEY,
    name         TEXT NOT NULL,
    currency     CHAR(3) NOT NULL,
    type         TEXT NOT NULL,
    is_active    BOOLEAN NOT NULL DEFAULT TRUE,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS transactions (
    id             TEXT PRIMARY KEY,
    account_id     TEXT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    occurred_on    DATE NOT NULL,
    minor_units    BIGINT NOT NULL,
    currency       CHAR(3) NOT NULL,
    description    TEXT NOT NULL DEFAULT '',
    category_id    TEXT,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_transactions_account_date ON transactions (account_id, occurred_on);
CREATE INDEX IF NOT EXISTS idx_transactions_category     ON transactions (category_id);

CREATE TABLE IF NOT EXISTS budgets (
    month         DATE NOT NULL,             -- always stored as first-of-month
    currency      CHAR(3) NOT NULL,
    category_id   TEXT NOT NULL,
    minor_units   BIGINT NOT NULL CHECK (minor_units >= 0),
    PRIMARY KEY (month, category_id)
);
