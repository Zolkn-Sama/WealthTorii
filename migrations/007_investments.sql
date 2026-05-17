-- Investments: a per-user manual price store and aggregated (average-cost)
-- positions. Quantities are stored scaled by 1e6 (micro-units) to allow
-- fractional shares/crypto; cost and prices are minor units (cf. ADR-0001).

CREATE TABLE IF NOT EXISTS instrument_prices (
    user_id     TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    symbol      TEXT NOT NULL,
    currency    CHAR(3) NOT NULL,
    price_minor BIGINT NOT NULL CHECK (price_minor >= 0),
    as_of       DATE NOT NULL,
    PRIMARY KEY (user_id, symbol)
);

CREATE TABLE IF NOT EXISTS positions (
    id             TEXT PRIMARY KEY,
    user_id        TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    account_id     TEXT,                       -- optional grouping label
    symbol         TEXT NOT NULL,
    quantity_micro BIGINT NOT NULL,            -- quantity * 1e6
    cost_minor     BIGINT NOT NULL,            -- total cost basis, minor units
    currency       CHAR(3) NOT NULL,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_positions_user ON positions (user_id);
