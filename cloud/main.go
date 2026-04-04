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
	"survaiv/internal/tui"
	"survaiv/internal/wallet"
	"survaiv/internal/wisdom"
	"survaiv/internal/x402"
)

// Version is set at build time via -ldflags.
var Version = "dev"

func main() {
	headless := flag.Bool("headless", false, "Run without TUI (dashboard only)")
	version := flag.Bool("version", false, "Print version and exit")
	flag.Parse()

	if *version {
		fmt.Println("survaiv", Version)
		return
	}

	// 1. Open database.
	database, err := db.Open(envOr("SURVAIV_DB_PATH", "survaiv.db"))
	if err != nil {
		slog.Error("failed to open database", "err", err)
		os.Exit(1)
	}
	defer database.Close()

	// 2. Load config.
	cfg := config.Load(database)
	if *headless {
		cfg.Headless = true
	}

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
	ldgr := ledger.New(cfg.StartingBankroll, cfg.Reserve, cfg.MaxPositions)

	// 6. Create shared dashboard state.
	dashState := dashboard.NewState()
	dashState.SetLiveMode(!cfg.PaperOnly)

	// 7. Create wisdom tracker and register as default.
	wisTracker := wisdom.NewTracker(database, httpClient)
	wisdom.SetDefault(wisTracker)

	// 8. Create agent.
	agnt := agent.New(cfg, httpClient, ldgr, x402mgr, dashState, wisTracker)

	// 9. Context for clean shutdown.
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer cancel()

	// 10. Start agent loop in background goroutine.
	go func() {
		cycle := 0
		for {
			select {
			case <-ctx.Done():
				return
			default:
			}

			retryDelay := agnt.RunCycle(ctx)
			cycle++

			// Periodically check outcomes and update wisdom rules.
			if cycle%4 == 0 {
				wisTracker.CheckOutcomes(ctx)
				wisTracker.EvaluateAndUpdateRules()
			}

			delay := time.Duration(cfg.LoopSeconds) * time.Second
			if retryDelay > 0 {
				delay = time.Duration(retryDelay) * time.Second
			}

			select {
			case <-ctx.Done():
				return
			case <-time.After(delay):
			}
		}
	}()

	// 11. Start dashboard server (always).
	go func() {
		if err := dashboard.Serve(ctx, cfg, dashState); err != nil {
			slog.Error("dashboard server failed", "err", err)
		}
	}()

	// 12. TUI or headless wait.
	if cfg.Headless {
		slog.Info("running in headless mode", "port", cfg.Port)
		<-ctx.Done()
		slog.Info("shutting down")
	} else {
		p := tea.NewProgram(tui.NewModel(dashState), tea.WithAltScreen())
		if _, err := p.Run(); err != nil {
			slog.Error("TUI error", "err", err)
			os.Exit(1)
		}
	}
}

func envOr(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}
