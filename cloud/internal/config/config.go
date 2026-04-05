package config

import (
	crand "crypto/rand"
	"database/sql"
	"encoding/binary"
	"fmt"
	"log/slog"
	"math"
	"os"
	"runtime"
	"strconv"
	"strings"
	"sync"

	"survaiv/internal/db"
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
	ListenAddr       string
	MaxCores         int
	DBPath           string
	Headless         bool
	TelemetryURL     string
	TelemetrySec     int
	MDNS             bool
}

// Load reads configuration with the following precedence (highest wins):
//
//	hardcoded defaults → config file → environment variables → SQLite overrides
//
// The configFile parameter is an explicit path; if empty, standard locations
// are searched: ./survaiv.toml, then ~/.config/survaiv/config.toml.
func Load(db *sql.DB, configFile string) *Config {
	c := &Config{db: db}

	// Parse config file (may be nil if no file found).
	file := parseConfigFile(findConfigFile(configFile))

	c.OaiURL = resolve("SURVAIV_OAI_URL", "oai_url", file, "https://tx402.ai/v1")
	c.OaiModel = resolve("SURVAIV_OAI_MODEL", "oai_model", file, "deepseek/deepseek-v3.2")
	c.ApiKey = resolve("SURVAIV_API_KEY", "api_key", file, "")
	c.WalletKey = resolve("SURVAIV_WALLET_KEY", "wallet_key", file, "")
	c.PolygonRPC = resolve("SURVAIV_POLYGON_RPC", "polygon_rpc", file, "https://polygon-rpc.com")
	c.ClobURL = resolve("SURVAIV_CLOB_URL", "clob_url", file, "https://clob.polymarket.com")
	c.PaperOnly = resolveBool("SURVAIV_PAPER_ONLY", "paper_only", file, true)
	c.LoopSeconds = resolveInt("SURVAIV_LOOP_SECONDS", "loop_seconds", file, 900)
	c.StartingBankroll = resolveFloat("SURVAIV_STARTING_BANKROLL", "starting_bankroll", file, 25.0)
	c.Reserve = resolveFloat("SURVAIV_RESERVE", "reserve", file, 5.0)
	c.MaxPositions = resolveInt("SURVAIV_MAX_POSITIONS", "max_positions", file, 3)
	c.MarketLimit = resolveInt("SURVAIV_MARKET_LIMIT", "market_limit", file, 10)
	c.DailyLossLimit = resolveFloat("SURVAIV_DAILY_LOSS_LIMIT", "daily_loss_limit", file, 5.0)
	c.DBPath = resolve("SURVAIV_DB_PATH", "db_path", file, "survaiv.db")

	c.TelemetryURL = resolve("SURVAIV_TELEMETRY_URL", "telemetry_url", file, "")
	c.TelemetrySec = resolveInt("SURVAIV_TELEMETRY_SEC", "telemetry_sec", file, 300)
	c.MDNS = resolveBool("SURVAIV_MDNS", "mdns", file, true)

	// Port: prefer PORT (Heroku), then config file / SURVAIV_PORT, then 8080.
	if p := os.Getenv("PORT"); p != "" {
		c.Port = envIntFromStr(p, 8080)
	} else {
		c.Port = resolveInt("SURVAIV_PORT", "port", file, 8080)
	}

	// Listen address: "127.0.0.1" for local-only, "0.0.0.0" or "" for all interfaces.
	c.ListenAddr = resolve("SURVAIV_LISTEN", "listen", file, "")

	// Max cores: number or percentage (e.g. "4" or "50%"). 0 = use all.
	c.MaxCores = resolveCores("SURVAIV_MAX_CORES", "max_cores", file, 0)

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

	// Auto-generate agent name on first run.
	c.EnsureAgentName()

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
		// General DB lookup for keys not cached in struct (owner_pin, agent_name, etc.).
		if c.db != nil {
			var val string
			if err := c.db.QueryRow(db.Q("SELECT value FROM config WHERE key = ?"), key).Scan(&val); err == nil {
				return val
			}
		}
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
		if err := db.Upsert(c.db, "config", "key", "value", key, value); err != nil {
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

// ── Resolve helpers (env > file > default) ──────────────────────

// resolve returns the first non-empty value: env var → config file → fallback.
func resolve(envKey, fileKey string, file map[string]string, fallback string) string {
	if v := os.Getenv(envKey); v != "" {
		return v
	}
	if file != nil {
		if v, ok := file[fileKey]; ok && v != "" {
			return v
		}
	}
	return fallback
}

func resolveInt(envKey, fileKey string, file map[string]string, fallback int) int {
	if v := os.Getenv(envKey); v != "" {
		return envIntFromStr(v, fallback)
	}
	if file != nil {
		if v, ok := file[fileKey]; ok && v != "" {
			return envIntFromStr(v, fallback)
		}
	}
	return fallback
}

func resolveFloat(envKey, fileKey string, file map[string]string, fallback float64) float64 {
	parse := func(s string) float64 {
		f, err := strconv.ParseFloat(s, 64)
		if err != nil {
			return fallback
		}
		return f
	}
	if v := os.Getenv(envKey); v != "" {
		return parse(v)
	}
	if file != nil {
		if v, ok := file[fileKey]; ok && v != "" {
			return parse(v)
		}
	}
	return fallback
}

func resolveBool(envKey, fileKey string, file map[string]string, fallback bool) bool {
	parse := func(s string) bool {
		return strings.EqualFold(s, "true") || s == "1"
	}
	if v := os.Getenv(envKey); v != "" {
		return parse(v)
	}
	if file != nil {
		if v, ok := file[fileKey]; ok && v != "" {
			return parse(v)
		}
	}
	return fallback
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

// resolveCores parses a core limit that can be an integer ("4") or a
// percentage of total cores ("50%"). Returns 0 for "use all cores".
func resolveCores(envKey, fileKey string, file map[string]string, fallback int) int {
	raw := resolve(envKey, fileKey, file, "")
	if raw == "" {
		return fallback
	}
	return parseCores(raw)
}

// ParseCoresFlag is the exported entry point for CLI --cores flag parsing.
func ParseCoresFlag(s string) int { return parseCores(s) }

// parseCores interprets a string as either an absolute core count or a
// percentage (e.g. "50%"). Negative values and zero are treated as "all cores".
func parseCores(s string) int {
	s = strings.TrimSpace(s)
	total := runtime.NumCPU()

	if strings.HasSuffix(s, "%") {
		pct, err := strconv.ParseFloat(strings.TrimSuffix(s, "%"), 64)
		if err != nil || pct <= 0 {
			return 0
		}
		if pct > 100 {
			pct = 100
		}
		n := int(math.Round(float64(total) * pct / 100))
		if n < 1 {
			n = 1
		}
		if n > total {
			n = total
		}
		return n
	}

	n, err := strconv.Atoi(s)
	if err != nil || n <= 0 {
		return 0
	}
	if n > total {
		slog.Warn("max_cores exceeds available cores, clamping", "requested", n, "available", total)
		return total
	}
	return n
}

// ApplyMaxCores calls runtime.GOMAXPROCS with the configured core limit.
// Returns the actual value set (0 means all cores were used).
func (c *Config) ApplyMaxCores() int {
	total := runtime.NumCPU()
	n := c.MaxCores
	if n <= 0 || n > total {
		n = total
	}
	prev := runtime.GOMAXPROCS(n)
	if n < total {
		slog.Info("core limit applied", "max_cores", n, "total_cores", total, "previous", prev)
	}
	return n
}

// NewsAPIKey returns the Tavily or Brave Search API key.
func (c *Config) NewsAPIKey() string {
	v := c.Get("news_api_key")
	if v != "" {
		return v
	}
	if v := os.Getenv("SURVAIV_NEWS_API_KEY"); v != "" {
		return v
	}
	return ""
}

// ToolUsageLevel returns 0=frugal, 1=balanced (default), 2=generous.
func (c *Config) ToolUsageLevel() int {
	v := c.Get("tool_usage")
	if v == "0" {
		return 0
	}
	if v == "2" {
		return 2
	}
	return 1
}

// NewsProvider returns the news search provider ("tavily" or "brave").
func (c *Config) NewsProvider() string {
	v := c.Get("news_provider")
	if v != "" {
		return v
	}
	if v := os.Getenv("SURVAIV_NEWS_PROVIDER"); v != "" {
		return v
	}
	return "tavily"
}

// ── Agent identity & PIN helpers ────────────────────────────────

var (
	adjectives = []string{"swift", "brave", "calm", "dark", "eager", "fair", "glad", "happy", "keen", "loud", "neat", "proud", "quiet", "rare", "safe", "tall", "vast", "warm", "wise", "young"}
	animals    = []string{"bear", "crow", "deer", "eagle", "fox", "hawk", "lion", "lynx", "moth", "newt", "orca", "puma", "raven", "seal", "tiger", "viper", "whale", "wolf", "wren", "yak"}
)

func cryptoRandIntn(max int) int {
	var buf [8]byte
	crand.Read(buf[:])
	return int(binary.LittleEndian.Uint64(buf[:]) % uint64(max))
}

// OwnerPin returns the stored owner PIN, or empty if unclaimed.
func (c *Config) OwnerPin() string { return c.Get("owner_pin") }

// AgentName returns the agent's display name from config or env var fallback.
func (c *Config) AgentName() string {
	name := c.Get("agent_name")
	if name == "" {
		return os.Getenv("SURVAIV_AGENT_NAME")
	}
	return name
}

// GeneratePin generates a memorable PIN like "swift-fox-7492" using crypto/rand.
func GeneratePin() string {
	adj := adjectives[cryptoRandIntn(len(adjectives))]
	animal := animals[cryptoRandIntn(len(animals))]
	num := cryptoRandIntn(9000) + 1000
	return fmt.Sprintf("%s-%s-%d", adj, animal, num)
}

// generateAgentName generates a display name like "swift-fox" using crypto/rand.
func generateAgentName() string {
	adj := adjectives[cryptoRandIntn(len(adjectives))]
	animal := animals[cryptoRandIntn(len(animals))]
	return fmt.Sprintf("%s-%s", adj, animal)
}

// EnsureAgentName auto-generates and persists an agent name if none is set.
func (c *Config) EnsureAgentName() string {
	name := c.AgentName()
	if name == "" {
		name = generateAgentName()
		c.Set("agent_name", name)
		slog.Info("agent name generated", "name", name)
	}
	return name
}

// TelemetryUrl returns the active telemetry hub URL (runtime override > config).
func (c *Config) TelemetryUrl() string {
	u := c.Get("telemetry_url")
	if u != "" {
		return u
	}
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.TelemetryURL
}

// SetTelemetryUrl persists the telemetry hub URL.
func (c *Config) SetTelemetryUrl(url string) {
	c.Set("telemetry_url", url)
}

// TelemetryInterval returns the active telemetry interval in seconds.
func (c *Config) TelemetryInterval() int {
	s := c.Get("telemetry_sec")
	if s != "" {
		if v, err := strconv.Atoi(s); err == nil {
			return v
		}
	}
	c.mu.RLock()
	defer c.mu.RUnlock()
	if c.TelemetrySec > 0 {
		return c.TelemetrySec
	}
	return 300
}

// SetTelemetryInterval persists the telemetry interval in seconds.
func (c *Config) SetTelemetryInterval(sec int) {
	c.Set("telemetry_sec", strconv.Itoa(sec))
}
