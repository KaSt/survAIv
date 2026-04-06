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

// GetConfig reads a string from the config table.
func GetConfig(d *sql.DB, key string) string {
	var val string
	_ = d.QueryRow(Q("SELECT value FROM config WHERE key = ?"), key).Scan(&val)
	return val
}

// SetConfig writes a key-value pair to the config table.
func SetConfig(d *sql.DB, key, value string) {
	_ = Upsert(d, "config", "key", "value", key, value)
}

// GetConfigInt reads an integer from the config table (returns 0 on missing/error).
func GetConfigInt(d *sql.DB, key string) int {
	s := GetConfig(d, key)
	if s == "" {
		return 0
	}
	v, _ := strconv.Atoi(s)
	return v
}

// SetConfigInt writes an integer to the config table.
func SetConfigInt(d *sql.DB, key string, val int) {
	SetConfig(d, key, strconv.Itoa(val))
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
