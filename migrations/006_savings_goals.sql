-- Savings goals ("projets saving") and their contributions, per user.
-- Progress is derived: saved = sum(contributions.minor_units). Contributions
-- are signed (positive = deposit, negative = withdrawal). target_date is the
-- optional deadline used to compute the required monthly effort.

CREATE TABLE IF NOT EXISTS savings_goals (
    id           TEXT PRIMARY KEY,
    user_id      TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name         TEXT NOT NULL,
    currency     CHAR(3) NOT NULL,
    target_minor BIGINT NOT NULL CHECK (target_minor > 0),
    target_date  DATE,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_savings_goals_user ON savings_goals (user_id);

CREATE TABLE IF NOT EXISTS savings_contributions (
    id          TEXT PRIMARY KEY,
    goal_id     TEXT NOT NULL REFERENCES savings_goals(id) ON DELETE CASCADE,
    occurred_on DATE NOT NULL,
    minor_units BIGINT NOT NULL,
    note        TEXT NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_savings_contrib_goal
    ON savings_contributions (goal_id);
