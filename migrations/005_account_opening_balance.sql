-- Opening balance per account, in minor units (cf. ADR-0001), same currency
-- as the account. The current balance is derived: opening_balance + sum of
-- the account's transaction amounts. Default 0 keeps existing rows and the
-- user-less CLI path working unchanged.

ALTER TABLE accounts
    ADD COLUMN IF NOT EXISTS opening_balance BIGINT NOT NULL DEFAULT 0;
