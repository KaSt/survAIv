package models

import (
	"log/slog"
	"strings"
)

// ModelSelection is the result of model selection.
type ModelSelection struct {
	Model       *ModelInfo
	ProviderURL string
	ModelID     string
	Price       float64
}

// SelectModel picks the best model for a task given the provider URL and budget.
func SelectModel(providerURL string, task TaskComplexity, remainingUsdc float64, estimatedCycles int) ModelSelection {
	useEngine := strings.Contains(providerURL, "x402-gateway") || strings.Contains(providerURL, "x402engine")

	if estimatedCycles <= 0 {
		estimatedCycles = 1
	}
	budgetPerCycle := remainingUsdc / float64(estimatedCycles)

	mu.RLock()
	defer mu.RUnlock()

	var best *ModelInfo
	bestScore := -1.0

	score := func(list []ModelInfo) {
		minReasoning := 1
		switch task {
		case Standard:
			minReasoning = 2
		case Complex:
			minReasoning = 3
		case Expert:
			minReasoning = 4
		}

		for i := range list {
			m := &list[i]
			var mid string
			var price float64
			if useEngine {
				mid = m.EngineID
				price = m.EnginePrice
			} else {
				mid = m.Tx402ID
				price = m.Tx402Price
			}
			if mid == "" || price <= 0 {
				continue
			}
			if m.Reasoning < minReasoning {
				continue
			}
			if price > budgetPerCycle*0.25 && task != Expert {
				continue
			}

			quality := float64(m.Reasoning * m.Reasoning)
			s := quality / price
			if remainingUsdc < 2.0 {
				s *= 1.0 / (price*1000.0 + 1.0)
			}
			if int(m.MinTask) <= int(task) {
				s *= 1.2
			}
			if s > bestScore {
				bestScore = s
				info := *m
				best = &info
			}
		}
	}

	score(dynamic)
	score(hardcoded)

	// Fallback: cheapest model.
	if best == nil {
		cheapest := 1e9
		for i := range hardcoded {
			m := &hardcoded[i]
			var mid string
			var price float64
			if useEngine {
				mid = m.EngineID
				price = m.EnginePrice
			} else {
				mid = m.Tx402ID
				price = m.Tx402Price
			}
			if mid == "" || price <= 0 {
				continue
			}
			if price < cheapest {
				cheapest = price
				info := *m
				best = &info
			}
		}
	}

	sel := ModelSelection{ProviderURL: providerURL}
	if best != nil {
		sel.Model = best
		if useEngine {
			sel.ModelID = best.EngineID
			sel.Price = best.EnginePrice
		} else {
			sel.ModelID = best.Tx402ID
			sel.Price = best.Tx402Price
		}
		slog.Info("model selected", "name", best.Name, "price", sel.Price, "task", task)
	}
	return sel
}
