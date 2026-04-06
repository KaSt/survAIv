package main

import (
	"context"
	"flag"
	"fmt"
	"log/slog"
	"os"
	"os/signal"
	"syscall"
	"time"

	tea "github.com/charmbracelet/bubbletea"

	"survaiv/internal/agent"
	"survaiv/internal/config"
	"survaiv/internal/dashboard"
	"survaiv/internal/db"
	"survaiv/internal/httpclient"
	"survaiv/internal/ledger"
	"survaiv/internal/models"
	"survaiv/internal/telemetry"
	"survaiv/internal/tui"
	"survaiv/internal/wallet"
	"survaiv/internal/wisdom"
	"survaiv/internal/x402"
)

// Version is set at build time via -ldflags.
var Version = "dev"

// shortLong registers a flag with both --long and -short names (same variable).
func shortLong[T any](p *T, long, short string, val T, usage string, register func(*T, string, T, string)) {
	register(p, long, val, usage)
	if short != "" {
		register(p, short, val, usage+" (short for --"+long+")")
	}
}

func main() {
	// ── CLI flags (--long / -short) ─────────────────────────────────
	// All options are also configurable via config file and environment
	// variables. CLI flags have highest precedence.

	var (
		fVersion   bool
		fHeadless  bool
		fConfig    string
		fListen    string
		fPort      int
		fCores     string
		fMdns      bool
		fPaper     bool
		fLlmURL    string
		fLlmKey    string
		fModel     string
		fWallet    string
		fNewsKey   string
		fName      string
		fBankroll  float64
		fReserve   float64
		fLoop      int
		fMaxPos    int
		fMarkets   int
		fDailyLoss float64
		fDB        string
		fTelemURL  string
		fTelemSec  int
		fPolyRPC   string
		fClobURL   string
	)

	shortLong(&fVersion, "version", "v", false, "Print version and exit", flag.BoolVar)
	shortLong(&fHeadless, "headless", "H", false, "Run without TUI (dashboard only)", flag.BoolVar)
	shortLong(&fConfig, "config", "c", "", "Path to config file (default: auto-detect survaiv.toml)", flag.StringVar)
	shortLong(&fListen, "listen", "l", "", "Listen address (e.g. 127.0.0.1 for local-only)", flag.StringVar)
	shortLong(&fPort, "port", "p", 0, "Dashboard HTTP port (default: 8080)", flag.IntVar)
	shortLong(&fCores, "cores", "", "", "Max CPU cores (number or percentage, e.g. 4 or 50%)", flag.StringVar)
	shortLong(&fMdns, "mdns", "", false, "Enable mDNS hostname advertising (<agent-name>.local)", flag.BoolVar)
	shortLong(&fPaper, "paper", "", false, "Force paper-only mode (no live trades)", flag.BoolVar)
	shortLong(&fLlmURL, "llm-url", "u", "", "LLM endpoint URL (e.g. https://api.openai.com/v1)", flag.StringVar)
	shortLong(&fLlmKey, "llm-key", "k", "", "LLM API key (optional for x402 providers)", flag.StringVar)
	shortLong(&fModel, "model", "m", "", "LLM model name (e.g. gpt-4o, deepseek/deepseek-v3.2)", flag.StringVar)
	shortLong(&fWallet, "wallet", "w", "", "Ethereum private key for x402 payments", flag.StringVar)
	shortLong(&fNewsKey, "news-key", "", "", "Tavily or Brave Search API key for news tool", flag.StringVar)
	shortLong(&fName, "name", "n", "", "Agent display name", flag.StringVar)
	shortLong(&fBankroll, "bankroll", "b", 0, "Starting bankroll in USDC (default: 25.0)", flag.Float64Var)
	shortLong(&fReserve, "reserve", "r", 0, "Reserve amount in USDC (default: 5.0)", flag.Float64Var)
	shortLong(&fLoop, "loop", "", 0, "Seconds between agent cycles (default: 900)", flag.IntVar)
	shortLong(&fMaxPos, "max-positions", "", 0, "Maximum concurrent positions (default: 3)", flag.IntVar)
	shortLong(&fMarkets, "market-limit", "", 0, "Markets to fetch per cycle (default: 10)", flag.IntVar)
	shortLong(&fDailyLoss, "daily-loss", "", 0, "Daily loss limit in USDC (default: 5.0)", flag.Float64Var)
	shortLong(&fDB, "db", "d", "", "SQLite database path (default: survaiv.db)", flag.StringVar)
	shortLong(&fTelemURL, "telemetry-url", "", "", "Telemetry hub URL", flag.StringVar)
	shortLong(&fTelemSec, "telemetry-sec", "", 0, "Telemetry reporting interval in seconds (default: 300)", flag.IntVar)
	shortLong(&fPolyRPC, "polygon-rpc", "", "", "Polygon JSON-RPC endpoint", flag.StringVar)
	shortLong(&fClobURL, "clob-url", "", "", "Polymarket CLOB API endpoint", flag.StringVar)

	flag.Parse()

	if fVersion {
		fmt.Println("survaiv", Version)
		return
	}

	// DB path override must happen before db.Open (which uses db.DSN()).
	if fDB != "" {
		os.Setenv("SURVAIV_DB_PATH", fDB)
	}

	// 1. Open database.
	database, err := db.Open(db.DSN())
	if err != nil {
		slog.Error("failed to open database", "err", err)
		os.Exit(1)
	}
	defer database.Close()

	// 2. Load config (file → env → SQLite overrides).
	cfg := config.Load(database, fConfig)

	// CLI flags override everything (highest precedence).
	if fHeadless {
		cfg.Headless = true
	}
	if fListen != "" {
		cfg.ListenAddr = fListen
	}
	if fPort != 0 {
		cfg.Port = fPort
	}
	if fCores != "" {
		cfg.MaxCores = config.ParseCoresFlag(fCores)
	}
	if fMdns {
		cfg.MDNS = true
	}
	if fPaper {
		cfg.PaperOnly = true
	}
	if fLlmURL != "" {
		cfg.OaiURL = fLlmURL
	}
	if fLlmKey != "" {
		cfg.ApiKey = fLlmKey
	}
	if fModel != "" {
		cfg.OaiModel = fModel
	}
	if fWallet != "" {
		cfg.WalletKey = fWallet
	}
	if fNewsKey != "" {
		cfg.Set("news_api_key", fNewsKey)
	}
	if fName != "" {
		cfg.Set("agent_name", fName)
	}
	if fBankroll > 0 {
		cfg.StartingBankroll = fBankroll
	}
	if fReserve > 0 {
		cfg.Reserve = fReserve
	}
	if fLoop > 0 {
		cfg.LoopSeconds = fLoop
	}
	if fMaxPos > 0 {
		cfg.MaxPositions = fMaxPos
	}
	if fMarkets > 0 {
		cfg.MarketLimit = fMarkets
	}
	if fDailyLoss > 0 {
		cfg.DailyLossLimit = fDailyLoss
	}
	if fTelemURL != "" {
		cfg.Set("telemetry_url", fTelemURL)
	}
	if fTelemSec > 0 {
		cfg.Set("telemetry_sec", fmt.Sprintf("%d", fTelemSec))
	}
	if fPolyRPC != "" {
		cfg.PolygonRPC = fPolyRPC
	}
	if fClobURL != "" {
		cfg.ClobURL = fClobURL
	}

	// Apply core limit before starting any goroutines.
	cfg.ApplyMaxCores()

	// 3. Init HTTP client.
	httpClient := httpclient.New()

	// 4. Setup wallet and x402 (optional).
	var x402mgr *x402.Payment
	if cfg.WalletKey != "" {
		w, wErr := wallet.New(cfg.WalletKey)
		if wErr != nil {
			slog.Error("wallet init failed", "err", wErr)
		} else {
			x402mgr = x402.New(w.PrivateKey())
			slog.Info("wallet loaded", "address", w.AddressHex())
		}
	}

	// 5. Create ledger.
	ldgr := ledger.New(cfg.StartingBankroll, cfg.Reserve, cfg.MaxPositions, database)

	// 6. Create shared dashboard state.
	dashState := dashboard.NewState()
	dashState.SetLiveMode(!cfg.PaperOnly)
	dashState.SetVersion(Version)
	dashState.SetLifetimeCycles(db.GetConfigInt(database, "lifetime_cycles"))

	// 7. Create wisdom tracker and register as default.
	wisTracker := wisdom.NewTracker(database, httpClient)
	wisdom.SetDefault(wisTracker)

	// 8. Start optional telemetry.
	telem := telemetry.New(cfg.TelemetryUrl(), cfg.TelemetryInterval(), dashState)
	defer telem.Stop()
	dashboard.TelemetryUpdater = func(url string, sec int) {
		telem.SetURL(url)
		telem.SetInterval(sec)
	}

	// 8b. Fetch live model catalog from OpenRouter.
	models.FetchOpenRouter()

	// 9. Create agent.
	agnt := agent.New(cfg, httpClient, ldgr, x402mgr, dashState, wisTracker)

	// Register knowledge-reset callback (needs agent + db + wisdom).
	dashState.SetResetKnowledgeFunc(func() {
		wisTracker.Reset()
		agnt.ResetCycleCount()
		db.SetConfigInt(database, "lifetime_cycles", 0)
	})

	// 10. Context for clean shutdown.
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer cancel()

	// 11. Start agent loop in background goroutine.
	go func() {
		cycle := 0
		lastResetDay := -1
		for {
			select {
			case <-ctx.Done():
				return
			default:
			}

			retryDelay := agnt.RunCycle(ctx)
			cycle++
			db.SetConfigInt(database, "lifetime_cycles", dashState.LifetimeCycles())

			// Reset daily loss accumulator at UTC midnight.
			today := time.Now().UTC().YearDay()
			if lastResetDay >= 0 && today != lastResetDay {
				ldgr.ResetDailyLoss()
				slog.Info("daily loss reset", "utc_yday", today)
			}
			lastResetDay = today

			// Periodically check outcomes and update wisdom rules.
			if cycle%4 == 0 {
				wisTracker.CheckOutcomes(ctx)
				wisTracker.EvaluateAndUpdateRules()
			}

			delay := time.Duration(cfg.LoopSeconds) * time.Second
			if retryDelay > 0 {
				delay = time.Duration(retryDelay) * time.Second
			}

			dashState.SetNextCycleEpoch(time.Now().Unix() + int64(delay.Seconds()))

			select {
			case <-ctx.Done():
				return
			case <-time.After(delay):
			}
		}
	}()

	// 12. Start dashboard server (always).
	go func() {
		if err := dashboard.Serve(ctx, cfg, dashState); err != nil {
			slog.Error("dashboard server failed", "err", err)
		}
	}()

	// 13. TUI or headless wait.
	if cfg.Headless {
		slog.Info("running in headless mode", "port", cfg.Port)
		<-ctx.Done()
		slog.Info("shutting down")
	} else {
		// Redirect slog to a ring buffer so logs don't corrupt the alt-screen.
		ringLog := tui.NewRingHandler(200)
		slog.SetDefault(slog.New(ringLog))

		p := tea.NewProgram(tui.NewModel(dashState, ringLog), tea.WithAltScreen())
		if _, err := p.Run(); err != nil {
			slog.Error("TUI error", "err", err)
			os.Exit(1)
		}
	}
}
