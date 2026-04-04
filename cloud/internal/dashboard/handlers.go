package dashboard

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"time"

	"survaiv/internal/config"
	"survaiv/internal/wisdom"
)

// RegisterRoutes registers all dashboard HTTP routes on the given mux.
func RegisterRoutes(mux *http.ServeMux, state *State, cfg *config.Config, db *sql.DB, wis *wisdom.Tracker) {
	// Dashboard HTML.
	mux.HandleFunc("GET /", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write([]byte(indexHTML))
	})

	// API: full state.
	mux.HandleFunc("GET /api/state", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(state.ToJSON()))
	})

	// API: positions with unrealized P&L.
	mux.HandleFunc("GET /api/positions", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(state.PositionsJSON()))
	})

	// API: decision history.
	mux.HandleFunc("GET /api/history", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(state.DecisionsJSON()))
	})

	// API: equity chart data.
	mux.HandleFunc("GET /api/equity", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(state.EquityHistoryJSON()))
	})

	// API: scouted markets.
	mux.HandleFunc("GET /api/scouted", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(state.ScoutedMarketsJSON()))
	})

	// API: wisdom stats.
	mux.HandleFunc("GET /api/wisdom", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(wis.StatsJSON()))
	})

	// API: SSE event stream.
	mux.HandleFunc("GET /api/events", func(w http.ResponseWriter, r *http.Request) {
		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "streaming unsupported", http.StatusInternalServerError)
			return
		}

		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")
		w.Header().Set("Access-Control-Allow-Origin", "*")

		// Send initial state.
		fmt.Fprintf(w, "event: state\ndata: %s\n\n", state.ToJSON())
		flusher.Flush()

		ch := state.Subscribe()
		defer state.Unsubscribe(ch)

		// Keepalive ticker.
		ticker := time.NewTicker(15 * time.Second)
		defer ticker.Stop()

		for {
			select {
			case <-r.Context().Done():
				return
			case msg, ok := <-ch:
				if !ok {
					return
				}
				fmt.Fprint(w, msg)
				flusher.Flush()
			case <-ticker.C:
				fmt.Fprint(w, ": keepalive\n\n")
				flusher.Flush()
			}
		}
	})

	// API: update LLM config (paper mode only).
	mux.HandleFunc("POST /api/llm-config", func(w http.ResponseWriter, r *http.Request) {
		if !cfg.PaperOnly {
			http.Error(w, "LLM config changes disabled in live mode", http.StatusForbidden)
			return
		}

		body, err := io.ReadAll(io.LimitReader(r.Body, 1024))
		if err != nil || len(body) == 0 {
			http.Error(w, "empty body", http.StatusBadRequest)
			return
		}

		var req struct {
			OaiURL   string `json:"oai_url"`
			OaiModel string `json:"oai_model"`
			ApiKey   string `json:"api_key"`
		}
		if err := json.Unmarshal(body, &req); err != nil {
			http.Error(w, "invalid JSON", http.StatusBadRequest)
			return
		}

		if req.OaiURL != "" {
			cfg.OaiURL = req.OaiURL
			cfg.Set("oai_url", req.OaiURL)
		}
		if req.OaiModel != "" {
			cfg.OaiModel = req.OaiModel
			cfg.Set("oai_model", req.OaiModel)
		}
		if req.ApiKey != "" {
			cfg.ApiKey = req.ApiKey
			cfg.Set("api_key", req.ApiKey)
		}

		state.SetOaiConfig(cfg.OaiURL, cfg.OaiModel)

		slog.Info("LLM config updated", "url", cfg.OaiURL, "model", cfg.OaiModel)

		w.Header().Set("Content-Type", "application/json")
		resp, _ := json.Marshal(map[string]interface{}{
			"ok":        true,
			"oai_url":   cfg.OaiURL,
			"oai_model": cfg.OaiModel,
		})
		w.Write(resp)
	})

	// API: backup config.
	mux.HandleFunc("GET /api/backup", func(w http.ResponseWriter, r *http.Request) {
		full := r.URL.Query().Get("full") == "1"

		backup := map[string]interface{}{
			"oai_url":    cfg.OaiURL,
			"oai_model":  cfg.OaiModel,
			"api_key":    maskIf(!full, cfg.ApiKey),
			"wallet_pk":  maskIf(!full, cfg.WalletKey),
			"loop_sec":   cfg.LoopSeconds,
			"bankroll":   cfg.StartingBankroll,
			"reserve":    cfg.Reserve,
			"max_pos":    cfg.MaxPositions,
			"mkt_limit":  cfg.MarketLimit,
			"paper_only": cfg.PaperOnly,
			"loss_lim":   cfg.DailyLossLimit,
			"firmware":   map[string]string{"version": "cloud", "date": time.Now().Format("2006-01-02")},
		}

		w.Header().Set("Content-Type", "application/json")
		w.Header().Set("Content-Disposition", `attachment; filename="survaiv-backup.json"`)
		json.NewEncoder(w).Encode(backup)
	})

	// API: restore config.
	mux.HandleFunc("POST /api/restore", func(w http.ResponseWriter, r *http.Request) {
		body, err := io.ReadAll(io.LimitReader(r.Body, 4096))
		if err != nil || len(body) == 0 {
			http.Error(w, "empty body", http.StatusBadRequest)
			return
		}

		var backup map[string]json.RawMessage
		if err := json.Unmarshal(body, &backup); err != nil {
			http.Error(w, "invalid JSON", http.StatusBadRequest)
			return
		}

		restoreString := func(key string, target *string) {
			if raw, ok := backup[key]; ok {
				var val string
				if json.Unmarshal(raw, &val) == nil && val != "" && val != "***" {
					*target = val
					cfg.Set(key, val)
				}
			}
		}

		restoreString("oai_url", &cfg.OaiURL)
		restoreString("oai_model", &cfg.OaiModel)
		restoreString("api_key", &cfg.ApiKey)

		slog.Info("config restored from backup")

		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(`{"ok":true,"msg":"Config restored."}`))
	})

	// API: wisdom knowledge export.
	mux.HandleFunc("GET /api/knowledge", func(w http.ResponseWriter, r *http.Request) {
		data, err := wis.ExportKnowledge()
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.Header().Set("Content-Disposition", `attachment; filename="survaiv-knowledge.json"`)
		w.Write(data)
	})

	// API: wisdom knowledge import.
	mux.HandleFunc("POST /api/knowledge", func(w http.ResponseWriter, r *http.Request) {
		body, err := io.ReadAll(io.LimitReader(r.Body, 16384))
		if err != nil || len(body) == 0 {
			http.Error(w, "empty body", http.StatusBadRequest)
			return
		}

		if err := wis.ImportKnowledge(body); err != nil {
			http.Error(w, "Invalid knowledge file: "+err.Error(), http.StatusBadRequest)
			return
		}

		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(`{"ok":true,"msg":"Knowledge imported successfully"}`))
	})

	// API: wisdom freeze toggle.
	mux.HandleFunc("POST /api/wisdom/freeze", func(w http.ResponseWriter, r *http.Request) {
		body, err := io.ReadAll(io.LimitReader(r.Body, 128))
		if err != nil || len(body) == 0 {
			http.Error(w, "empty body", http.StatusBadRequest)
			return
		}

		var req struct {
			Frozen bool `json:"frozen"`
		}
		json.Unmarshal(body, &req)
		wis.SetFrozen(req.Frozen)

		w.Header().Set("Content-Type", "application/json")
		resp, _ := json.Marshal(map[string]interface{}{"ok": true, "frozen": req.Frozen})
		w.Write(resp)
	})
}

func maskIf(mask bool, val string) string {
	if mask && val != "" {
		return "***"
	}
	return val
}
