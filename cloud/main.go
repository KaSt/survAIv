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
	"survaiv/internal/telemetry"
	"survaiv/internal/tui"
	"survaiv/internal/wallet"
	"survaiv/internal/wisdom"
	"survaiv/internal/x402"
)

// Version is set at build time via -ldflags.
var Version = "dev"

func main() {
	headless := flag.Bool("headless", false, "Run without TUI (dashboard only)")
	configFile := flag.String("config", "", "Path to config file (default: auto-detect survaiv.toml)")
	listenFlag := flag.String("listen", "", "Listen address (e.g. 127.0.0.1 for local-only, 0.0.0.0 for all)")
	portFlag := flag.Int("port", 0, "Dashboard HTTP port (default: 8080)")
	coresFlag := flag.String("cores", "", "Max CPU cores to use (number or percentage, e.g. 4 or 50%)")
	mdnsFlag := flag.Bool("mdns", false, "Enable mDNS hostname advertising (<agent-name>.local)")
	version := flag.Bool("version", false, "Print version and exit")
	flag.Parse()

	if *version {
		fmt.Println("survaiv", Version)
		return
	}

	// 1. Open database.
	database, err := db.Open(db.DSN())
	if err != nil {
		slog.Error("failed to open database", "err", err)
		os.Exit(1)
	}
	defer database.Close()

	// 2. Load config.
	cfg := config.Load(database, *configFile)
	if *headless {
		cfg.Headless = true
	}
	if *listenFlag != "" {
		cfg.ListenAddr = *listenFlag
	}
	if *portFlag != 0 {
		cfg.Port = *portFlag
	}
	if *coresFlag != "" {
		cfg.MaxCores = config.ParseCoresFlag(*coresFlag)
	}
	if *mdnsFlag {
		cfg.MDNS = true
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

	// 9. Create agent.
	agnt := agent.New(cfg, httpClient, ldgr, x402mgr, dashState, wisTracker)

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
