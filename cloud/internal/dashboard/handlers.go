package dashboard

import (
	crand "crypto/rand"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"strconv"
	"strings"
	"sync"

	"survaiv/internal/config"
	"survaiv/internal/wisdom"

	"github.com/go-chi/chi/v5"
)

// ── Auth state ──────────────────────────────────────────────────

var (
	authMu      sync.RWMutex
	pendingPin  string
	activeToken string
)

func cryptoRandIntn(max int) int {
	var buf [8]byte
	crand.Read(buf[:])
	return int(binary.LittleEndian.Uint64(buf[:]) % uint64(max))
}

func generateToken() string {
	b := make([]byte, 32)
	crand.Read(b)
	return hex.EncodeToString(b)
}

func extractToken(r *http.Request) string {
	// Check Authorization header first.
	auth := r.Header.Get("Authorization")
	if strings.HasPrefix(auth, "Bearer ") {
		return auth[7:]
	}
	// Fallback: query param ?token= (for EventSource which can't set headers).
	if t := r.URL.Query().Get("token"); t != "" {
		return t
	}
	return ""
}

// authMiddleware protects all API endpoints (except /api/auth and /) with token validation.
// If no owner has claimed the agent yet, all endpoints are open.
func authMiddleware(cfg *config.Config) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			// Always allow dashboard page and auth endpoint.
			if r.URL.Path == "/" || r.URL.Path == "/api/auth" {
				next.ServeHTTP(w, r)
				return
			}

			// If no owner claimed yet, allow all.
			if cfg.OwnerPin() == "" {
				next.ServeHTTP(w, r)
				return
			}

			// Validate token.
			token := extractToken(r)
			authMu.RLock()
			valid := activeToken != "" && token == activeToken
			authMu.RUnlock()
			if !valid {
				w.Header().Set("Content-Type", "application/json")
				w.WriteHeader(http.StatusUnauthorized)
				w.Write([]byte(`{"error":"unauthorized"}`))
				return
			}

			next.ServeHTTP(w, r)
		})
	}
}

// NewRouter creates the HTTP router for the dashboard.
func NewRouter(state *State, cfg *config.Config) chi.Router {
	r := chi.NewRouter()

	r.Use(authMiddleware(cfg))

	r.Get("/", serveIndex)
	r.Get("/api/state", handleState(state, cfg))
	r.Get("/api/positions", handlePositions(state))
	r.Get("/api/decisions", handleDecisions(state))
	r.Get("/api/equity-history", handleEquityHistory(state))
	r.Get("/api/scouted", handleScouted(state))
	r.Get("/api/wisdom", handleWisdom)
	r.Get("/api/knowledge", handleKnowledgeExport)
	r.Post("/api/knowledge", handleKnowledgeImport)
	r.Post("/api/wisdom/freeze", handleWisdomFreeze)
	r.Post("/api/wisdom/rules", handleWisdomRules)
	r.Get("/api/events", handleSSE(state))
	r.Get("/api/auth", handleAuthGet(cfg))
	r.Post("/api/auth", handleAuthPost(cfg))
	r.Post("/api/config", handleConfig(cfg, state))
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

func handleState(state *State, cfg *config.Config) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		raw := state.ToJSON()
		var data map[string]interface{}
		json.Unmarshal(raw, &data)
		data["news_provider"] = cfg.NewsProvider()
		data["has_news_key"] = cfg.NewsAPIKey() != ""
		data["tool_usage"] = cfg.ToolUsageLevel()
		b, _ := json.Marshal(data)
		w.Write(b)
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

func handleKnowledgeExport(w http.ResponseWriter, r *http.Request) {
	data, err := wisdom.ExportKnowledge()
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Disposition", "attachment; filename=survaiv-knowledge.json")
	w.Write(data)
}

func handleKnowledgeImport(w http.ResponseWriter, r *http.Request) {
	body, err := io.ReadAll(io.LimitReader(r.Body, 512*1024))
	if err != nil {
		http.Error(w, "read error", http.StatusBadRequest)
		return
	}
	if err := wisdom.ImportKnowledge(body); err != nil {
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprintf(w, `{"ok":false,"msg":"%s"}`, err.Error())
		return
	}
	w.Header().Set("Content-Type", "application/json")
	fmt.Fprint(w, `{"ok":true,"msg":"Knowledge imported"}`)
}

func handleWisdomFreeze(w http.ResponseWriter, r *http.Request) {
	body, _ := io.ReadAll(io.LimitReader(r.Body, 128))
	freeze := strings.Contains(string(body), "true")
	wisdom.SetFrozen(freeze)
	w.Header().Set("Content-Type", "application/json")
	if freeze {
		fmt.Fprint(w, `{"ok":true,"frozen":true}`)
	} else {
		fmt.Fprint(w, `{"ok":true,"frozen":false}`)
	}
}

func handleWisdomRules(w http.ResponseWriter, r *http.Request) {
	body, err := io.ReadAll(io.LimitReader(r.Body, 16*1024))
	if err != nil {
		http.Error(w, "read error", http.StatusBadRequest)
		return
	}
	var payload struct {
		Rules string `json:"rules"`
	}
	rules := string(body)
	if json.Unmarshal(body, &payload) == nil && payload.Rules != "" {
		rules = payload.Rules
	}
	wisdom.SetCustomRules(rules)
	w.Header().Set("Content-Type", "application/json")
	fmt.Fprintf(w, `{"ok":true,"size":%d}`, len(rules))
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

// ── Auth handlers ───────────────────────────────────────────────

func handleAuthGet(cfg *config.Config) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")

		pin := cfg.OwnerPin()
		if pin == "" {
			// Not yet claimed — generate a pending PIN.
			authMu.Lock()
			if pendingPin == "" {
				pendingPin = config.GeneratePin()
			}
			displayPin := pendingPin
			authMu.Unlock()

			json.NewEncoder(w).Encode(map[string]interface{}{
				"claimed":     false,
				"display_pin": displayPin,
			})
			return
		}

		// Claimed — check if caller has valid token.
		token := extractToken(r)
		authMu.RLock()
		valid := activeToken != "" && token == activeToken
		authMu.RUnlock()

		json.NewEncoder(w).Encode(map[string]interface{}{
			"claimed":   true,
			"needs_pin": !valid,
		})
	}
}

func handleAuthPost(cfg *config.Config) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")

		var req struct {
			Action string `json:"action"`
			Pin    string `json:"pin"`
		}
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, "invalid JSON", http.StatusBadRequest)
			return
		}

		switch req.Action {
		case "claim":
			authMu.Lock()
			if pendingPin == "" || req.Pin != pendingPin {
				authMu.Unlock()
				json.NewEncoder(w).Encode(map[string]interface{}{
					"ok":    false,
					"error": "Wrong PIN",
				})
				return
			}
			cfg.Set("owner_pin", req.Pin)
			pendingPin = ""
			token := generateToken()
			activeToken = token
			authMu.Unlock()

			slog.Info("agent claimed by owner")
			json.NewEncoder(w).Encode(map[string]interface{}{
				"ok":    true,
				"token": token,
			})

		case "login":
			stored := cfg.OwnerPin()
			if stored == "" || req.Pin != stored {
				json.NewEncoder(w).Encode(map[string]interface{}{
					"ok":    false,
					"error": "Wrong PIN",
				})
				return
			}
			token := generateToken()
			authMu.Lock()
			activeToken = token
			authMu.Unlock()

			slog.Info("owner authenticated")
			json.NewEncoder(w).Encode(map[string]interface{}{
				"ok":    true,
				"token": token,
			})

		default:
			json.NewEncoder(w).Encode(map[string]interface{}{
				"ok":    false,
				"error": "unknown action",
			})
		}
	}
}

// ── Config handler ──────────────────────────────────────────────

func handleConfig(cfg *config.Config, state *State) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		var req struct {
			PaperOnly    *int    `json:"paper_only"`
			AgentName    *string `json:"agent_name"`
			NewsProvider *string `json:"news_provider"`
			NewsAPIKey   *string `json:"news_api_key"`
			ToolUsage    *int    `json:"tool_usage"`
		}
		if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
			http.Error(w, "invalid JSON", http.StatusBadRequest)
			return
		}
		if req.PaperOnly != nil {
			paper := *req.PaperOnly == 1
			if paper {
				cfg.Set("paper_only", "true")
			} else {
				cfg.Set("paper_only", "false")
			}
			state.SetPaperOnly(paper)
			slog.Info("trading mode changed", "paper_only", paper)
		}
		if req.AgentName != nil && *req.AgentName != "" {
			cfg.Set("agent_name", *req.AgentName)
			state.SetAgentName(*req.AgentName)
			slog.Info("agent name changed", "name", *req.AgentName)
		}
		if req.NewsProvider != nil && *req.NewsProvider != "" {
			cfg.Set("news_provider", *req.NewsProvider)
			slog.Info("news provider changed", "provider", *req.NewsProvider)
		}
		if req.NewsAPIKey != nil {
			cfg.Set("news_api_key", *req.NewsAPIKey)
			slog.Info("news API key updated")
		}
		if req.ToolUsage != nil {
			v := *req.ToolUsage
			if v < 0 {
				v = 0
			}
			if v > 2 {
				v = 2
			}
			cfg.Set("tool_usage", strconv.Itoa(v))
			state.SetToolUsage(v)
			slog.Info("tool usage level changed", "level", v)
		}
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(`{"ok":true}`))
	}
}
