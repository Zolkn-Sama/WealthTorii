-- Adds the BP-style cleaned taxonomy (cf. SORTED_DATA.xlsx) and the user-side reconciliation
-- flag (Colonne 1) to every transaction.

ALTER TABLE transactions
    ADD COLUMN IF NOT EXISTS bp_category    TEXT,
    ADD COLUMN IF NOT EXISTS bp_subcategory TEXT,
    ADD COLUMN IF NOT EXISTS type_operation TEXT NOT NULL DEFAULT '',
    ADD COLUMN IF NOT EXISTS is_reconciled  BOOLEAN NOT NULL DEFAULT FALSE;
