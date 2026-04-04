package dashboard

import (
	"database/sql"
	"fmt"
	"log/slog"
	"net/http"

	"survaiv/internal/config"
	"survaiv/internal/wisdom"
)

// StartServer creates and starts the dashboard HTTP server.
func StartServer(cfg *config.Config, state *State, wis *wisdom.Tracker, db *sql.DB) error {
	mux := http.NewServeMux()
	RegisterRoutes(mux, state, cfg, db, wis)

	addr := fmt.Sprintf(":%d", cfg.Port)
	slog.Info("dashboard server starting", "addr", addr)

	return http.ListenAndServe(addr, mux)
}
