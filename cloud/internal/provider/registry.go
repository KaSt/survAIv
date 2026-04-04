package provider

import (
	"log/slog"
	"sync"
)

var (
	mu       sync.RWMutex
	llm      []Adapter
	data     []*DataAdapter
	initDone bool
)

// Init registers all built-in adapters.
func Init() {
	mu.Lock()
	defer mu.Unlock()
	if initDone {
		return
	}
	llm = []Adapter{
		&tx402Adapter{},
		&x402engineAdapter{},
		&customAdapter{}, // fallback — always last
	}
	data = []*DataAdapter{
		Claw402Data,
	}
	initDone = true
	slog.Info("provider registry initialized", "llm", len(llm), "data", len(data))
}

// FindByURL returns the Adapter whose MatchesURL returns true.
// Falls back to the custom adapter.
func FindByURL(url string) Adapter {
	mu.RLock()
	defer mu.RUnlock()
	for _, a := range llm {
		if a.MatchesURL(url) {
			return a
		}
	}
	// Return custom fallback (last).
	if len(llm) > 0 {
		return llm[len(llm)-1]
	}
	return nil
}

// FindDataByURL returns the DataAdapter matching the URL.
func FindDataByURL(url string) *DataAdapter {
	mu.RLock()
	defer mu.RUnlock()
	for _, d := range data {
		if d.MatchesURL(url) {
			return d
		}
	}
	return nil
}

// FindByName finds an LLM adapter by short name.
func FindByName(name string) Adapter {
	mu.RLock()
	defer mu.RUnlock()
	for _, a := range llm {
		if a.Name() == name {
			return a
		}
	}
	return nil
}

// GetAll returns all registered LLM adapters.
func GetAll() []Adapter {
	mu.RLock()
	defer mu.RUnlock()
	out := make([]Adapter, len(llm))
	copy(out, llm)
	return out
}

// ActiveProviderUsesX402 checks if the given base URL uses x402 auth.
func ActiveProviderUsesX402(baseURL string) bool {
	a := FindByURL(baseURL)
	if a != nil && a.Auth() == AuthX402 {
		return true
	}
	d := FindDataByURL(baseURL)
	if d != nil && d.AuthMethod == AuthX402 {
		return true
	}
	return false
}
