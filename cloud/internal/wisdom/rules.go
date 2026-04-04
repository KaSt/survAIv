package wisdom

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"time"
)

// knowledgeExport is the JSON format for knowledge export/import.
// Matches the ESP32 survaiv-knowledge-v2 format exactly for cross-platform compatibility.
type knowledgeExport struct {
	Format      string              `json:"format"`
	ExportedAt  int64               `json:"exported_at"`
	CustomRules string              `json:"custom_rules,omitempty"`
	WisdomText  string              `json:"wisdom_text"`
	Stats       knowledgeStats      `json:"stats"`
	Models      []modelExport       `json:"models"`
	Decisions   []knowledgeDecision `json:"decisions"`
}

type knowledgeStats struct {
	Total        int              `json:"total"`
	Correct      int              `json:"correct"`
	HoldsTotal   int              `json:"holds_total"`
	HoldsCorrect int              `json:"holds_correct"`
	BuysTotal    int              `json:"buys_total"`
	BuysCorrect  int              `json:"buys_correct"`
	Categories   []categoryExport `json:"categories"`
}

type categoryExport struct {
	Name    string `json:"n"`
	Total   int    `json:"t"`
	Correct int    `json:"c"`
}

type modelExport struct {
	Name      string `json:"name"`
	FirstSeen int64  `json:"first_seen"`
	Decisions int    `json:"decisions"`
}

type knowledgeDecision struct {
	Epoch              int64   `json:"e"`
	MarketID           string  `json:"m"`
	Question           string  `json:"q"`
	Category           string  `json:"cat"`
	DecisionType       string  `json:"dt"`
	Signal             string  `json:"sig"`
	ModelName          string  `json:"mdl"`
	YesPriceAtDecision float64 `json:"yp"`
	Confidence         float64 `json:"conf"`
	EdgeBps            float64 `json:"eb"`
	Resolved           bool    `json:"res"`
	OutcomeYes         bool    `json:"oy"`
	FinalYesPrice      float64 `json:"fp"`
	Checked            bool    `json:"chk"`
	LastCheckEpoch     int64   `json:"lce"`
}

// ExportKnowledge exports the full wisdom state as JSON.
func (t *Tracker) ExportKnowledge() ([]byte, error) {
	t.mu.RLock()
	defer t.mu.RUnlock()

	exp := knowledgeExport{
		Format:      "survaiv-knowledge-v2",
		ExportedAt:  time.Now().Unix(),
		CustomRules: t.customRules,
		WisdomText:  t.wisdom,
		Stats: knowledgeStats{
			Total:        t.stats.Total,
			Correct:      t.stats.Correct,
			HoldsTotal:   t.stats.HoldsTotal,
			HoldsCorrect: t.stats.HoldsCorrect,
			BuysTotal:    t.stats.BuysTotal,
			BuysCorrect:  t.stats.BuysCorrect,
			Categories:   []categoryExport{},
		},
		Models: []modelExport{},
	}

	for i := 0; i < t.count; i++ {
		ri := (t.head + i) % maxDecisions
		d := t.ring[ri]
		exp.Decisions = append(exp.Decisions, knowledgeDecision{
			Epoch:              d.Epoch,
			MarketID:           d.MarketID,
			Question:           d.Question,
			Category:           d.Category,
			DecisionType:       d.DecisionType,
			Signal:             d.Signal,
			ModelName:          d.ModelName,
			YesPriceAtDecision: d.YesPriceAtDecision,
			Confidence:         d.Confidence,
			EdgeBps:            d.EdgeBps,
			Resolved:           d.Resolved,
			OutcomeYes:         d.OutcomeYes,
			FinalYesPrice:      d.FinalYesPrice,
			Checked:            d.Checked,
			LastCheckEpoch:     d.LastCheckEpoch,
		})
	}

	return json.MarshalIndent(exp, "", "  ")
}

// ImportKnowledge imports wisdom state from JSON.
func (t *Tracker) ImportKnowledge(data []byte) error {
	var exp knowledgeExport
	if err := json.Unmarshal(data, &exp); err != nil {
		return fmt.Errorf("invalid JSON: %w", err)
	}

	if len(exp.Format) < 19 || exp.Format[:19] != "survaiv-knowledge-v" {
		return fmt.Errorf("unknown format: %s", exp.Format)
	}

	t.mu.Lock()
	defer t.mu.Unlock()

	// Import custom rules (v2).
	if exp.CustomRules != "" {
		t.customRules = exp.CustomRules
		if len(t.customRules) > maxWisdomBytes {
			t.customRules = t.customRules[:maxWisdomBytes]
			if idx := lastIndex(t.customRules, '\n'); idx > 0 {
				t.customRules = t.customRules[:idx+1]
			}
		}
		t.saveCustomRulesToDB()
	}

	// Import wisdom text.
	t.wisdom = exp.WisdomText
	if len(t.wisdom) > maxWisdomBytes {
		t.wisdom = t.wisdom[:maxWisdomBytes]
	}

	// Import stats.
	t.stats = Stats{
		Total:        exp.Stats.Total,
		Correct:      exp.Stats.Correct,
		HoldsTotal:   exp.Stats.HoldsTotal,
		HoldsCorrect: exp.Stats.HoldsCorrect,
		BuysTotal:    exp.Stats.BuysTotal,
		BuysCorrect:  exp.Stats.BuysCorrect,
	}

	// Import decisions ring.
	t.head = 0
	t.count = 0
	for _, kd := range exp.Decisions {
		if t.count >= maxDecisions {
			break
		}
		t.ring[t.count] = TrackedDecision{
			Epoch:              kd.Epoch,
			MarketID:           kd.MarketID,
			Question:           kd.Question,
			Category:           kd.Category,
			DecisionType:       kd.DecisionType,
			Signal:             kd.Signal,
			ModelName:          kd.ModelName,
			YesPriceAtDecision: kd.YesPriceAtDecision,
			Confidence:         kd.Confidence,
			EdgeBps:            kd.EdgeBps,
			Resolved:           kd.Resolved,
			OutcomeYes:         kd.OutcomeYes,
			FinalYesPrice:      kd.FinalYesPrice,
			Checked:            kd.Checked,
			LastCheckEpoch:     kd.LastCheckEpoch,
		}
		t.count++
	}

	// Persist and regenerate.
	t.saveStatsToDB()
	if !t.frozen {
		t.regenerateWisdom()
	}
	t.saveWisdomToDB()

	slog.Info("wisdom imported",
		"stats_total", t.stats.Total, "decisions", t.count,
		"wisdom_len", len(t.wisdom), "custom_rules_len", len(t.customRules))
	return nil
}

// StatsJSON returns the wisdom stats as a JSON string for the API.
func (t *Tracker) StatsJSON() string {
	t.mu.RLock()
	defer t.mu.RUnlock()

	data, _ := json.Marshal(map[string]interface{}{
		"total_tracked":  t.count,
		"total_resolved": t.stats.Total,
		"total_correct":  t.stats.Correct,
		"hold_resolved":  t.stats.HoldsTotal,
		"hold_correct":   t.stats.HoldsCorrect,
		"buy_resolved":   t.stats.BuysTotal,
		"buy_correct":    t.stats.BuysCorrect,
		"frozen":         t.frozen,
		"wisdom_text":    t.wisdom,
		"custom_rules":   t.customRules,
		"wisdom_budget":  maxWisdomBytes,
		"categories":     []interface{}{},
		"models":         []interface{}{},
	})
	return string(data)
}
