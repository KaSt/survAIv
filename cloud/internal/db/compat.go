package db

import (
	"database/sql"
	"strconv"
	"strings"
)

// Upsert inserts or replaces a row in a key-value config table.
// SQLite uses INSERT OR REPLACE; Postgres uses ON CONFLICT ... DO UPDATE.
func Upsert(db *sql.DB, table, keyCol, valCol, key, value string) error {
	var q string
	if ActiveDriver == Postgres {
		q = "INSERT INTO " + table + " (" + keyCol + ", " + valCol + ") VALUES ($1, $2) " +
			"ON CONFLICT (" + keyCol + ") DO UPDATE SET " + valCol + " = EXCLUDED." + valCol
	} else {
		q = "INSERT OR REPLACE INTO " + table + " (" + keyCol + ", " + valCol + ") VALUES (?, ?)"
	}
	_, err := db.Exec(q, key, value)
	return err
}

// Q rewrites a query with ? placeholders to use $1, $2, ... for Postgres.
// For SQLite, returns the query unchanged.
func Q(query string) string {
	if ActiveDriver != Postgres {
		return query
	}
	var b strings.Builder
	n := 1
	for i := 0; i < len(query); i++ {
		if query[i] == '?' {
			b.WriteByte('$')
			b.WriteString(strconv.Itoa(n))
			n++
		} else {
			b.WriteByte(query[i])
		}
	}
	return b.String()
}
