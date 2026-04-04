package dashboard

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"net/http"

	"survaiv/internal/config"
	"survaiv/internal/wisdom"

	"github.com/go-chi/chi/v5"
)

// NewRouter creates the HTTP router for the dashboard.
func NewRouter(state *State, cfg *config.Config) chi.Router {
	r := chi.NewRouter()

	r.Get("/", serveIndex)
	r.Get("/api/state", handleState(state))
	r.Get("/api/positions", handlePositions(state))
	r.Get("/api/decisions", handleDecisions(state))
	r.Get("/api/equity-history", handleEquityHistory(state))
	r.Get("/api/scouted", handleScouted(state))
	r.Get("/api/wisdom", handleWisdom)
	r.Get("/api/events", handleSSE(state))
	r.Post("/api/llm-config", handleLLMConfig(cfg, state))
	r.Get("/api/backup", handleBackup(cfg))
	r.Post("/api/restore", handleRestore(cfg))
	r.Post("/api/ota", func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "OTA not available in cloud mode", http.StatusNotImplemented)
	})

	return r
}

func serveIndex(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Write(IndexHTML)
}

func handleState(state *State) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write(state.ToJSON())
	}
}

func handlePositions(state *State) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write(state.PositionsJSON())
	}
}

func handleDecisions(state *State) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write(state.DecisionsJSON())
	}
}

func handleEquityHistory(state *State) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write(state.EquityHistoryJSON())
	}
}

func handleScouted(state *State) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write(state.ScoutedMarketsJSON())
	}
}

func handleWisdom(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	fmt.Fprint(w, wisdom.StatsJSON())
}

func handleSSE(state *State) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "Streaming not supported", http.StatusInternalServerError)
			return
		}

		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.Header().Set("Connection", "keep-alive")
		w.Header().Set("Access-Control-Allow-Origin", "*")

		ch := state.RegisterSSEClient()
		defer state.UnregisterSSEClient(ch)

		// Send initial state.
		fmt.Fprintf(w, "event: state\ndata: %s\n\n", state.ToJSON())
		flusher.Flush()

		for {
			select {
			case msg, ok := <-ch:
				if !ok {
					return
				}
				fmt.Fprint(w, msg)
				flusher.Flush()
			case <-r.Context().Done():
				return
			}
		}
	}
}

func handleLLMConfig(cfg *config.Config, state *State) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req struct {
			URL   string `json:"url"`
			Model string `json:"model"`
			Key   string `json:"key"`
		}
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, "invalid JSON", http.StatusBadRequest)
			return
		}
		if req.URL != "" {
			cfg.Set("oai_url", req.URL)
		}
		if req.Model != "" {
			cfg.Set("oai_model", req.Model)
		}
		if req.Key != "" {
			cfg.Set("api_key", req.Key)
		}

		slog.Info("LLM config updated", "url", req.URL, "model", req.Model)
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(`{"ok":true}`))
	}
}

func handleBackup(cfg *config.Config) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		data := map[string]string{
			"oai_url":   cfg.Get("oai_url"),
			"oai_model": cfg.Get("oai_model"),
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(data)
	}
}

func handleRestore(cfg *config.Config) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var data map[string]string
		if err := json.NewDecoder(r.Body).Decode(&data); err != nil {
			http.Error(w, "invalid JSON", http.StatusBadRequest)
			return
		}
		for k, v := range data {
			cfg.Set(k, v)
		}
		w.Write([]byte(`{"ok":true}`))
	}
}
