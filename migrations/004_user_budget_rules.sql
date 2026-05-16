-- Per-user budget limits and categorisation rules. Replaces the global
-- ~/.wealthtorii/*.conf files for the API (the local CLI keeps using files).
--
-- One currency per user budget is enforced in the API layer; stored per row
-- for simplicity. Rules are order-sensitive (first match wins), so `ord`
-- preserves the user's insertion order.

CREATE TABLE IF NOT EXISTS user_budgets (
    user_id     TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    currency    CHAR(3) NOT NULL,
    category_id TEXT NOT NULL,
    minor_units BIGINT NOT NULL CHECK (minor_units >= 0),
    PRIMARY KEY (user_id, category_id)
);

CREATE TABLE IF NOT EXISTS user_rules (
    user_id        TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    ord            INTEGER NOT NULL,
    pattern        TEXT NOT NULL,
    category_id    TEXT NOT NULL,
    bp_subcategory TEXT,
    PRIMARY KEY (user_id, ord)
);
