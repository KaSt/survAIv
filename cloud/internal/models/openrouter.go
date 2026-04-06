package models

import (
	"encoding/json"
	"io"
	"log/slog"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"time"
)

// FetchOpenRouter queries the OpenRouter /api/v1/models endpoint and merges
// the results into the dynamic model registry. Call once at startup.
func FetchOpenRouter() {
	client := &http.Client{Timeout: 15 * time.Second}
	resp, err := client.Get("https://openrouter.ai/api/v1/models")
	if err != nil {
		slog.Warn("openrouter model fetch failed", "err", err)
		return
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		slog.Warn("openrouter model read failed", "err", err)
		return
	}

	var catalog struct {
		Data []orModel `json:"data"`
	}
	if err := json.Unmarshal(body, &catalog); err != nil {
		slog.Warn("openrouter model parse failed", "err", err)
		return
	}

	// Filter to well-known model families, sort by context size desc.
	var filtered []orModel
	for _, m := range catalog.Data {
		if isNotableModel(m.ID) {
			filtered = append(filtered, m)
		}
	}
	sort.Slice(filtered, func(i, j int) bool {
		return filtered[i].ContextLength > filtered[j].ContextLength
	})

	// Cap at ~80 to keep registry reasonable.
	if len(filtered) > 80 {
		filtered = filtered[:80]
	}

	models := make([]ModelInfo, 0, len(filtered))
	for _, m := range filtered {
		ctxK := m.ContextLength / 1000
		if ctxK < 1 {
			ctxK = 1
		}

		promptPrice := parseFloat(m.Pricing.Prompt)
		completionPrice := parseFloat(m.Pricing.Completion)
		// Estimate per-request cost: ~2K prompt + ~1K completion tokens.
		perReq := promptPrice*2000 + completionPrice*1000

		reasoning := estimateReasoning(m.ID)
		speed := estimateSpeed(m.ID, promptPrice)

		models = append(models, ModelInfo{
			Name:     m.Name,
			Tx402ID:  m.ID, // OpenRouter IDs work as model identifiers
			ContextK: ctxK,
			Reasoning: reasoning,
			Speed:     speed,
			MinTask:   inferMinTask(reasoning),
			Notes:     "OpenRouter: " + truncate(m.Description, 80),
			// Store per-request estimate in Tx402Price for cost lookups.
			Tx402Price: perReq,
		})
	}

	AddDynamic(models)
	slog.Info("openrouter catalog loaded", "fetched", len(catalog.Data), "notable", len(models))
}

type orModel struct {
	ID            string `json:"id"`
	Name          string `json:"name"`
	Description   string `json:"description"`
	ContextLength int    `json:"context_length"`
	Pricing       struct {
		Prompt     string `json:"prompt"`
		Completion string `json:"completion"`
	} `json:"pricing"`
	TopProvider struct {
		ContextLength      int  `json:"context_length"`
		MaxCompletionTokens int  `json:"max_completion_tokens"`
		IsModerated        bool `json:"is_moderated"`
	} `json:"top_provider"`
}

// isNotableModel returns true for well-known model families worth tracking.
func isNotableModel(id string) bool {
	id = strings.ToLower(id)

	// Skip free tiers, beta variants, online/extended variants.
	if strings.HasSuffix(id, ":free") || strings.HasSuffix(id, ":beta") {
		return false
	}

	prefixes := []string{
		"openai/", "anthropic/", "google/", "meta-llama/",
		"deepseek/", "mistralai/", "qwen/", "x-ai/",
		"cohere/", "nvidia/", "microsoft/",
	}
	for _, p := range prefixes {
		if strings.HasPrefix(id, p) {
			return true
		}
	}

	// Catch specific well-known models by keyword.
	keywords := []string{
		"claude", "gpt-4", "gpt-5", "gemini", "llama",
		"opus", "sonnet", "haiku", "o1", "o3", "o4",
	}
	for _, kw := range keywords {
		if strings.Contains(id, kw) {
			return true
		}
	}
	return false
}

func estimateReasoning(id string) int {
	id = strings.ToLower(id)
	switch {
	case strings.Contains(id, "opus") || strings.Contains(id, "o1") ||
		strings.Contains(id, "o3") || strings.Contains(id, "o4") ||
		strings.Contains(id, "gpt-5") && !strings.Contains(id, "mini") && !strings.Contains(id, "nano"):
		return 5
	case strings.Contains(id, "sonnet") || strings.Contains(id, "r1") ||
		strings.Contains(id, "gpt-4o") && !strings.Contains(id, "mini") ||
		strings.Contains(id, "gemini-2.5-pro") || strings.Contains(id, "gemini-3"):
		return 4
	case strings.Contains(id, "haiku") || strings.Contains(id, "flash") ||
		strings.Contains(id, "mini") || strings.Contains(id, "gpt-4o-mini"):
		return 3
	case strings.Contains(id, "nano") || strings.Contains(id, "lite") ||
		strings.Contains(id, "8b"):
		return 2
	default:
		return 3
	}
}

func estimateSpeed(id string, promptPrice float64) int {
	id = strings.ToLower(id)
	switch {
	case strings.Contains(id, "nano") || strings.Contains(id, "flash") ||
		strings.Contains(id, "lite") || strings.Contains(id, "mini"):
		return 5
	case strings.Contains(id, "haiku") || strings.Contains(id, "8b"):
		return 5
	case strings.Contains(id, "opus") || strings.Contains(id, "o1") ||
		strings.Contains(id, "o3") || strings.Contains(id, "o4"):
		return 2
	case promptPrice > 0.00001: // expensive = likely slow
		return 2
	default:
		return 3
	}
}

func inferMinTask(reasoning int) TaskComplexity {
	switch {
	case reasoning >= 5:
		return Expert
	case reasoning >= 4:
		return Standard
	default:
		return Trivial
	}
}

func parseFloat(s string) float64 {
	f, _ := strconv.ParseFloat(s, 64)
	return f
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n-1] + "…"
}
