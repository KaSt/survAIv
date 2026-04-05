package db

import (
	"database/sql"
	"log/slog"
	"os"
	"strings"

	_ "github.com/jackc/pgx/v5/stdlib"
	_ "modernc.org/sqlite"
)

// Driver identifies the active database backend.
type Driver string

const (
	SQLite   Driver = "sqlite"
	Postgres Driver = "postgres"
)

// ActiveDriver is set during Open and read by other packages.
var ActiveDriver Driver

// Open opens (or creates) the database and runs migrations.
// If dsn starts with "postgres://" or "postgresql://", PostgreSQL is used.
// Otherwise it's treated as a SQLite file path.
func Open(dsn string) (*sql.DB, error) {
	var db *sql.DB
	var err error

	if isPostgres(dsn) {
		ActiveDriver = Postgres
		db, err = sql.Open("pgx", dsn)
		if err != nil {
			return nil, err
		}
		db.SetMaxOpenConns(10)
		db.SetMaxIdleConns(5)
	} else {
		ActiveDriver = SQLite
		db, err = sql.Open("sqlite", dsn+"?_journal_mode=WAL&_busy_timeout=5000")
		if err != nil {
			return nil, err
		}
		// Single writer, but allow concurrent readers.
		db.SetMaxOpenConns(1)
	}

	if err := Migrate(db); err != nil {
		db.Close()
		return nil, err
	}

	label := string(ActiveDriver)
	if ActiveDriver == SQLite {
		label += ":" + dsn
	}
	slog.Info("database opened", "driver", label)
	return db, nil
}

// DSN returns the effective DSN from env, preferring SURVAIV_DATABASE_URL over SURVAIV_DB_PATH.
func DSN() string {
	if u := os.Getenv("SURVAIV_DATABASE_URL"); u != "" {
		return u
	}
	if u := os.Getenv("DATABASE_URL"); u != "" {
		return u
	}
	if p := os.Getenv("SURVAIV_DB_PATH"); p != "" {
		return p
	}
	return "survaiv.db"
}

func isPostgres(dsn string) bool {
	return strings.HasPrefix(dsn, "postgres://") || strings.HasPrefix(dsn, "postgresql://")
}
