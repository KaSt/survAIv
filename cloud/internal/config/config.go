package config

import (
	"database/sql"
	"log/slog"
	"os"
	"strconv"
	"strings"
	"sync"
)

// Config holds all survaiv runtime configuration.
type Config struct {
	mu sync.RWMutex
	db *sql.DB

	OaiURL           string
	OaiModel         string
	ApiKey           string
	WalletKey        string
	PolygonRPC       string
	ClobURL          string
	PaperOnly        bool
	LoopSeconds      int
	StartingBankroll float64
	Reserve          float64
	MaxPositions     int
	MarketLimit      int
	DailyLossLimit   float64
	Port             int
	DBPath           string
	Headless         bool
}

// Load reads configuration from environment variables, falling back to defaults.
// If a database is provided, stored overrides are applied on top.
func Load(db *sql.DB) *Config {
	c := &Config{db: db}

	c.OaiURL = envStr("SURVAIV_OAI_URL", "https://tx402.ai/v1")
	c.OaiModel = envStr("SURVAIV_OAI_MODEL", "deepseek/deepseek-v3.2")
	c.ApiKey = envStr("SURVAIV_API_KEY", "")
	c.WalletKey = envStr("SURVAIV_WALLET_KEY", "")
	c.PolygonRPC = envStr("SURVAIV_POLYGON_RPC", "https://polygon-rpc.com")
	c.ClobURL = envStr("SURVAIV_CLOB_URL", "https://clob.polymarket.com")
	c.PaperOnly = envBool("SURVAIV_PAPER_ONLY", true)
	c.LoopSeconds = envInt("SURVAIV_LOOP_SECONDS", 900)
	c.StartingBankroll = envFloat("SURVAIV_STARTING_BANKROLL", 25.0)
	c.Reserve = envFloat("SURVAIV_RESERVE", 5.0)
	c.MaxPositions = envInt("SURVAIV_MAX_POSITIONS", 3)
	c.MarketLimit = envInt("SURVAIV_MARKET_LIMIT", 10)
	c.DailyLossLimit = envFloat("SURVAIV_DAILY_LOSS_LIMIT", 5.0)
	c.DBPath = envStr("SURVAIV_DB_PATH", "survaiv.db")

	// Port: prefer PORT (Heroku), then SURVAIV_PORT, then 8080.
	if p := os.Getenv("PORT"); p != "" {
		c.Port = envIntFromStr(p, 8080)
	} else {
		c.Port = envInt("SURVAIV_PORT", 8080)
	}

	// Headless mode: auto-detect Heroku or explicit flag.
	c.Headless = os.Getenv("DYNO") != ""

	// Apply stored overrides from SQLite config table.
	if db != nil {
		c.loadOverrides()
	}

	slog.Info("config loaded",
		"oai_url", c.OaiURL,
		"model", c.OaiModel,
		"paper", c.PaperOnly,
		"port", c.Port,
		"headless", c.Headless,
	)

	return c
}

// Get returns a config value by key name.
func (c *Config) Get(key string) string {
	c.mu.RLock()
	defer c.mu.RUnlock()

	switch key {
	case "oai_url":
		return c.OaiURL
	case "oai_model":
		return c.OaiModel
	case "api_key":
		return c.ApiKey
	case "wallet_key":
		return c.WalletKey
	case "polygon_rpc":
		return c.PolygonRPC
	case "clob_url":
		return c.ClobURL
	case "paper_only":
		if c.PaperOnly {
			return "true"
		}
		return "false"
	default:
		return ""
	}
}

// Set writes a config value and persists it to SQLite.
func (c *Config) Set(key, value string) {
	c.mu.Lock()
	defer c.mu.Unlock()

	switch key {
	case "oai_url":
		c.OaiURL = value
	case "oai_model":
		c.OaiModel = value
	case "api_key":
		c.ApiKey = value
	case "paper_only":
		c.PaperOnly = strings.EqualFold(value, "true") || value == "1"
	}

	if c.db != nil {
		_, err := c.db.Exec(
			`INSERT OR REPLACE INTO config (key, value) VALUES (?, ?)`,
			key, value,
		)
		if err != nil {
			slog.Error("config: failed to persist", "key", key, "err", err)
		}
	}
}

// loadOverrides reads config table and applies stored values.
func (c *Config) loadOverrides() {
	rows, err := c.db.Query(`SELECT key, value FROM config`)
	if err != nil {
		return // Table may not exist yet.
	}
	defer rows.Close()

	for rows.Next() {
		var k, v string
		if err := rows.Scan(&k, &v); err != nil {
			continue
		}
		switch k {
		case "oai_url":
			c.OaiURL = v
		case "oai_model":
			c.OaiModel = v
		case "api_key":
			c.ApiKey = v
		case "paper_only":
			c.PaperOnly = strings.EqualFold(v, "true") || v == "1"
		}
	}
}

// ── Environment helpers ─────────────────────────────────────────

func envStr(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

func envInt(key string, fallback int) int {
	return envIntFromStr(os.Getenv(key), fallback)
}

func envIntFromStr(s string, fallback int) int {
	if s == "" {
		return fallback
	}
	v, err := strconv.Atoi(s)
	if err != nil {
		return fallback
	}
	return v
}

func envFloat(key string, fallback float64) float64 {
	s := os.Getenv(key)
	if s == "" {
		return fallback
	}
	v, err := strconv.ParseFloat(s, 64)
	if err != nil {
		return fallback
	}
	return v
}

func envBool(key string, fallback bool) bool {
	s := os.Getenv(key)
	if s == "" {
		return fallback
	}
	return strings.EqualFold(s, "true") || s == "1"
}
