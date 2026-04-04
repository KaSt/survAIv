package dashboard

import (
	"context"
	"fmt"
	"log/slog"
	"net/http"

	"survaiv/internal/config"
)

// Serve starts the dashboard HTTP server. Blocks until context is cancelled.
func Serve(ctx context.Context, cfg *config.Config, state *State) error {
	router := NewRouter(state, cfg)
	addr := fmt.Sprintf("%s:%d", cfg.ListenAddr, cfg.Port)

	srv := &http.Server{
		Addr:    addr,
		Handler: router,
	}

	go func() {
		<-ctx.Done()
		srv.Shutdown(context.Background())
	}()

	slog.Info("dashboard server starting", "addr", addr)
	if err := srv.ListenAndServe(); err != http.ErrServerClosed {
		return err
	}
	return nil
}
