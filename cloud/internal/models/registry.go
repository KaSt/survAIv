package models

import (
	"log/slog"
	"strings"
	"sync"
)

var (
	mu      sync.RWMutex
	dynamic []ModelInfo
)

// Count returns total model count (hardcoded + dynamic).
func Count() int {
	mu.RLock()
	defer mu.RUnlock()
	return len(hardcoded) + len(dynamic)
}

// Get returns the model at index i.
func Get(i int) ModelInfo {
	mu.RLock()
	defer mu.RUnlock()
	if i < len(hardcoded) {
		return hardcoded[i]
	}
	di := i - len(hardcoded)
	if di < len(dynamic) {
		return dynamic[di]
	}
	return hardcoded[0] // safety fallback
}

// All returns a copy of all models.
func All() []ModelInfo {
	mu.RLock()
	defer mu.RUnlock()
	out := make([]ModelInfo, 0, len(hardcoded)+len(dynamic))
	out = append(out, hardcoded...)
	out = append(out, dynamic...)
	return out
}

// CheapestPrice returns the lowest available price for a model.
func CheapestPrice(m ModelInfo) float64 {
	if m.Tx402Price > 0 && m.EnginePrice > 0 {
		if m.Tx402Price < m.EnginePrice {
			return m.Tx402Price
		}
		return m.EnginePrice
	}
	if m.Tx402Price > 0 {
		return m.Tx402Price
	}
	return m.EnginePrice
}

// LookupPrice finds the price for a model name string.
func LookupPrice(modelName string) float64 {
	m := LookupModel(modelName)
	if m == nil {
		return 0
	}
	return CheapestPrice(*m)
}

// LookupCost computes actual cost from token counts for a model.
// Returns 0 if model is unknown (e.g. local/LAN endpoint).
func LookupCost(modelName string, promptTokens, completionTokens int) float64 {
	m := LookupModel(modelName)
	if m == nil {
		return 0
	}
	return m.CostForTokens(promptTokens, completionTokens)
}

// LookupModel finds the full ModelInfo for a model name string.
func LookupModel(modelName string) *ModelInfo {
	if modelName == "" {
		return nil
	}
	query := normalizeName(stripOrgPrefix(modelName))

	mu.RLock()
	defer mu.RUnlock()

	// Exact match first.
	for _, list := range [][]ModelInfo{dynamic, hardcoded} {
		for i := range list {
			m := &list[i]
			if m.Tx402ID != "" && normalizeName(stripOrgPrefix(m.Tx402ID)) == query {
				return m
			}
			if m.EngineID != "" && normalizeName(m.EngineID) == query {
				return m
			}
		}
	}
	// Fuzzy match.
	for _, list := range [][]ModelInfo{dynamic, hardcoded} {
		for i := range list {
			m := &list[i]
			if m.Tx402ID != "" {
				s := normalizeName(stripOrgPrefix(m.Tx402ID))
				if strings.Contains(s, query) || strings.Contains(query, s) {
					return m
				}
			}
			if m.EngineID != "" {
				s := normalizeName(m.EngineID)
				if strings.Contains(s, query) || strings.Contains(query, s) {
					return m
				}
			}
		}
	}
	return nil
}

// AddDynamic merges catalog-discovered models into the dynamic list.
func AddDynamic(models []ModelInfo) {
	mu.Lock()
	defer mu.Unlock()
	for _, m := range models {
		found := false
		for i, d := range dynamic {
			if normalizeName(d.Name) == normalizeName(m.Name) {
				dynamic[i] = m // update
				found = true
				break
			}
		}
		if !found {
			dynamic = append(dynamic, m)
		}
	}
	slog.Info("model registry updated", "dynamic", len(dynamic), "total", len(hardcoded)+len(dynamic))
}

func stripOrgPrefix(id string) string {
	if i := strings.LastIndex(id, "/"); i >= 0 {
		return id[i+1:]
	}
	return id
}

func normalizeName(s string) string {
	s = strings.ToLower(s)
	s = strings.Map(func(r rune) rune {
		if r == ':' || r == '_' || r == '.' {
			return '-'
		}
		return r
	}, s)
	return s
}
