package db

import (
	"database/sql"
	"log/slog"
)

// sqliteMigrations uses SQLite-specific syntax.
var sqliteMigrations = []string{
	`CREATE TABLE IF NOT EXISTS config (
		key   TEXT PRIMARY KEY,
		value TEXT NOT NULL
	)`,

	`CREATE TABLE IF NOT EXISTS positions (
		market_id   TEXT NOT NULL,
		question    TEXT NOT NULL DEFAULT '',
		side        TEXT NOT NULL,
		entry_price REAL NOT NULL,
		shares      REAL NOT NULL,
		stake_usdc  REAL NOT NULL,
		is_live     INTEGER NOT NULL DEFAULT 0,
		order_id    TEXT NOT NULL DEFAULT '',
		opened_at   INTEGER NOT NULL DEFAULT (strftime('%s','now'))
	)`,

	`CREATE TABLE IF NOT EXISTS decisions (
		epoch         INTEGER NOT NULL,
		market_id     TEXT NOT NULL DEFAULT '',
		decision_type TEXT NOT NULL,
		side          TEXT NOT NULL DEFAULT '',
		confidence    REAL NOT NULL DEFAULT 0,
		edge_bps      REAL NOT NULL DEFAULT 0,
		rationale     TEXT NOT NULL DEFAULT '',
		outcome       TEXT NOT NULL DEFAULT '',
		resolved_at   INTEGER NOT NULL DEFAULT 0
	)`,

	`CREATE TABLE IF NOT EXISTS equity_snapshots (
		epoch     INTEGER NOT NULL,
		equity    REAL NOT NULL,
		cash      REAL NOT NULL,
		llm_spend REAL NOT NULL DEFAULT 0,
		pnl       REAL NOT NULL DEFAULT 0
	)`,

	`CREATE TABLE IF NOT EXISTS wisdom_rules (
		id         INTEGER PRIMARY KEY AUTOINCREMENT,
		rule_text  TEXT NOT NULL,
		created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
		score      REAL NOT NULL DEFAULT 0
	)`,

	`CREATE TABLE IF NOT EXISTS wisdom_stats (
		id              INTEGER PRIMARY KEY CHECK (id = 1),
		total           INTEGER NOT NULL DEFAULT 0,
		correct         INTEGER NOT NULL DEFAULT 0,
		holds_total     INTEGER NOT NULL DEFAULT 0,
		holds_correct   INTEGER NOT NULL DEFAULT 0,
		buys_total      INTEGER NOT NULL DEFAULT 0,
		buys_correct    INTEGER NOT NULL DEFAULT 0,
		frozen          INTEGER NOT NULL DEFAULT 0
	)`,

	`INSERT OR IGNORE INTO wisdom_stats (id) VALUES (1)`,

	`ALTER TABLE wisdom_stats ADD COLUMN custom_rules TEXT NOT NULL DEFAULT ''`,
}

// postgresMigrations uses PostgreSQL-compatible syntax.
var postgresMigrations = []string{
	`CREATE TABLE IF NOT EXISTS config (
		key   TEXT PRIMARY KEY,
		value TEXT NOT NULL
	)`,

	`CREATE TABLE IF NOT EXISTS positions (
		market_id   TEXT NOT NULL,
		question    TEXT NOT NULL DEFAULT '',
		side        TEXT NOT NULL,
		entry_price DOUBLE PRECISION NOT NULL,
		shares      DOUBLE PRECISION NOT NULL,
		stake_usdc  DOUBLE PRECISION NOT NULL,
		is_live     INTEGER NOT NULL DEFAULT 0,
		order_id    TEXT NOT NULL DEFAULT '',
		opened_at   BIGINT NOT NULL DEFAULT (EXTRACT(EPOCH FROM NOW())::BIGINT)
	)`,

	`CREATE TABLE IF NOT EXISTS decisions (
		epoch         BIGINT NOT NULL,
		market_id     TEXT NOT NULL DEFAULT '',
		decision_type TEXT NOT NULL,
		side          TEXT NOT NULL DEFAULT '',
		confidence    DOUBLE PRECISION NOT NULL DEFAULT 0,
		edge_bps      DOUBLE PRECISION NOT NULL DEFAULT 0,
		rationale     TEXT NOT NULL DEFAULT '',
		outcome       TEXT NOT NULL DEFAULT '',
		resolved_at   BIGINT NOT NULL DEFAULT 0
	)`,

	`CREATE TABLE IF NOT EXISTS equity_snapshots (
		epoch     BIGINT NOT NULL,
		equity    DOUBLE PRECISION NOT NULL,
		cash      DOUBLE PRECISION NOT NULL,
		llm_spend DOUBLE PRECISION NOT NULL DEFAULT 0,
		pnl       DOUBLE PRECISION NOT NULL DEFAULT 0
	)`,

	`CREATE TABLE IF NOT EXISTS wisdom_rules (
		id         SERIAL PRIMARY KEY,
		rule_text  TEXT NOT NULL,
		created_at BIGINT NOT NULL DEFAULT (EXTRACT(EPOCH FROM NOW())::BIGINT),
		score      DOUBLE PRECISION NOT NULL DEFAULT 0
	)`,

	`CREATE TABLE IF NOT EXISTS wisdom_stats (
		id              INTEGER PRIMARY KEY CHECK (id = 1),
		total           INTEGER NOT NULL DEFAULT 0,
		correct         INTEGER NOT NULL DEFAULT 0,
		holds_total     INTEGER NOT NULL DEFAULT 0,
		holds_correct   INTEGER NOT NULL DEFAULT 0,
		buys_total      INTEGER NOT NULL DEFAULT 0,
		buys_correct    INTEGER NOT NULL DEFAULT 0,
		frozen          INTEGER NOT NULL DEFAULT 0,
		custom_rules    TEXT NOT NULL DEFAULT ''
	)`,

	`INSERT INTO wisdom_stats (id) VALUES (1) ON CONFLICT DO NOTHING`,
}

// Migrate applies all schema migrations.
func Migrate(db *sql.DB) error {
	migrations := sqliteMigrations
	if ActiveDriver == Postgres {
		migrations = postgresMigrations
	}

	for _, stmt := range migrations {
		if _, err := db.Exec(stmt); err != nil {
			// ALTER TABLE ADD COLUMN fails if column already exists — that's fine (SQLite only).
			if contains(stmt, "ADD COLUMN") {
				continue
			}
			slog.Error("migration failed", "err", err, "stmt", stmt[:min(len(stmt), 80)])
			return err
		}
	}
	slog.Info("database migrations applied", "driver", string(ActiveDriver), "count", len(migrations))
	return nil
}

func contains(s, sub string) bool {
	for i := 0; i+len(sub) <= len(s); i++ {
		if s[i:i+len(sub)] == sub {
			return true
		}
	}
	return false
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
