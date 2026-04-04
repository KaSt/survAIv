package wisdom

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"log/slog"
	"sync"
	"time"

	"survaiv/internal/httpclient"
	"survaiv/internal/types"
)

const (
	maxDecisions      = 30
	maxChecksPerCall  = 3
	checkCooldownSec  = 3600
	maxQuestionLen    = 60
	maxWisdomBytes    = 800
	gammaMarketURL    = "https://gamma-api.polymarket.com/markets/"
)

// TrackedDecision is an internal record for outcome tracking.
type TrackedDecision struct {
	Epoch              int64
	MarketID           string
	Question           string
	Category           string
	DecisionType       string
	Side               string
	Signal             string
	ModelName          string
	YesPriceAtDecision float64
	Confidence         float64
	EdgeBps            float64
	Resolved           bool
	OutcomeYes         bool
	FinalYesPrice      float64
	Checked            bool
	LastCheckEpoch     int64
}

// Stats holds aggregated outcome statistics.
type Stats struct {
	Total        int
	Correct      int
	HoldsTotal   int
	HoldsCorrect int
	BuysTotal    int
	BuysCorrect  int
}

// Tracker tracks agent decisions and evaluates outcomes.
type Tracker struct {
	db     *sql.DB
	client *httpclient.Client
	mu     sync.RWMutex
	ring   [maxDecisions]TrackedDecision
	head   int
	count  int
	stats  Stats
	wisdom string
	frozen bool
}

// NewTracker creates a new wisdom Tracker backed by SQLite.
func NewTracker(db *sql.DB, client *httpclient.Client) *Tracker {
	t := &Tracker{db: db, client: client}
	t.loadFromDB()
	return t
}

// RecordDecision records a new agent decision for tracking.
func (t *Tracker) RecordDecision(d types.DecisionRecord) {
	t.mu.Lock()
	defer t.mu.Unlock()

	idx := (t.head + t.count) % maxDecisions
	if t.count == maxDecisions {
		t.head = (t.head + 1) % maxDecisions
	} else {
		t.count++
	}

	q := d.MarketQuestion
	if len(q) > maxQuestionLen {
		q = q[:maxQuestionLen]
	}

	t.ring[idx] = TrackedDecision{
		Epoch:              d.Epoch,
		MarketID:           d.MarketID,
		Question:           q,
		DecisionType:       d.Type,
		Side:               d.Side,
		YesPriceAtDecision: 0,
		Confidence:         d.Confidence,
		EdgeBps:            d.EdgeBps,
	}

	slog.Info("wisdom: track", "type", d.Type, "market", d.MarketID,
		"confidence", d.Confidence, "edge_bps", d.EdgeBps)
}

// CheckOutcomes checks resolved markets to evaluate past decisions.
func (t *Tracker) CheckOutcomes(ctx context.Context) {
	if t.client == nil {
		return
	}

	t.mu.Lock()
	now := time.Now().Unix()

	type target struct {
		idx      int
		marketID string
	}
	var targets []target

	for i := 0; i < t.count && len(targets) < maxChecksPerCall; i++ {
		ri := (t.head + i) % maxDecisions
		d := &t.ring[ri]
		if d.Resolved || d.MarketID == "" {
			continue
		}
		if now-d.LastCheckEpoch < checkCooldownSec {
			continue
		}
		targets = append(targets, target{idx: ri, marketID: d.MarketID})
	}
	t.mu.Unlock()

	for _, tgt := range targets {
		url := gammaMarketURL + tgt.marketID
		resp, err := t.client.Get(ctx, url)
		if err != nil || resp.StatusCode != 200 {
			slog.Warn("wisdom: check failed", "market", tgt.marketID, "err", err)
			t.mu.Lock()
			t.ring[tgt.idx].LastCheckEpoch = now
			t.mu.Unlock()
			continue
		}

		var raw struct {
			Closed        bool   `json:"closed"`
			OutcomePrices string `json:"outcomePrices"`
		}
		if err := json.Unmarshal(resp.Body, &raw); err != nil {
			continue
		}

		var prices []string
		json.Unmarshal([]byte(raw.OutcomePrices), &prices)
		var yesPrice, noPrice float64
		if len(prices) >= 2 {
			fmt.Sscanf(prices[0], "%f", &yesPrice)
			fmt.Sscanf(prices[1], "%f", &noPrice)
		}

		t.mu.Lock()
		d := &t.ring[tgt.idx]
		if d.MarketID != tgt.marketID {
			t.mu.Unlock()
			continue
		}
		d.LastCheckEpoch = now

		if raw.Closed && len(prices) >= 2 {
			d.Resolved = true
			d.Checked = false
			d.FinalYesPrice = yesPrice
			d.OutcomeYes = yesPrice > 0.9 && noPrice < 0.1
			slog.Info("wisdom: resolved", "market", d.MarketID,
				"outcome", func() string {
					if d.OutcomeYes {
						return "YES"
					}
					return "NO"
				}())
		} else if yesPrice > 0 {
			delta := yesPrice - d.YesPriceAtDecision
			if delta < 0 {
				delta = -delta
			}
			if delta > 0.15 {
				d.Resolved = true
				d.Checked = false
				d.FinalYesPrice = yesPrice
				d.OutcomeYes = (yesPrice - d.YesPriceAtDecision) > 0
				slog.Info("wisdom: price-resolved", "market", d.MarketID, "delta", delta)
			}
		}
		t.mu.Unlock()
	}
}

// EvaluateAndUpdateRules evaluates resolved decisions and regenerates wisdom.
func (t *Tracker) EvaluateAndUpdateRules() string {
	t.mu.Lock()
	defer t.mu.Unlock()

	any := false

	for i := 0; i < t.count; i++ {
		ri := (t.head + i) % maxDecisions
		d := &t.ring[ri]
		if !d.Resolved || d.Checked {
			continue
		}
		d.Checked = true
		any = true

		isHold := d.DecisionType == "hold"
		isBuy := contains(d.DecisionType, "buy")
		correct := false

		if isHold {
			move := d.FinalYesPrice - d.YesPriceAtDecision
			if move < 0 {
				move = -move
			}
			correct = move <= 0.20
			t.stats.HoldsTotal++
			if correct {
				t.stats.HoldsCorrect++
			}
		} else if isBuy {
			yesSide := contains(d.DecisionType, "yes")
			if yesSide {
				correct = d.OutcomeYes || (d.FinalYesPrice-d.YesPriceAtDecision > 0.10)
			} else {
				correct = !d.OutcomeYes || (d.YesPriceAtDecision-d.FinalYesPrice > 0.10)
			}
			t.stats.BuysTotal++
			if correct {
				t.stats.BuysCorrect++
			}
		}

		t.stats.Total++
		if correct {
			t.stats.Correct++
		}

		slog.Info("wisdom: eval", "type", d.DecisionType, "market", d.MarketID,
			"correct", correct, "score", fmt.Sprintf("%d/%d", t.stats.Correct, t.stats.Total))
	}

	if any {
		t.saveStatsToDB()
		if !t.frozen {
			t.regenerateWisdom()
			t.saveWisdomToDB()
			slog.Info("wisdom updated", "len", len(t.wisdom))
		}
	}

	return t.wisdom
}

// Stats returns the current wisdom statistics.
func (t *Tracker) Stats() types.WisdomStats {
	t.mu.RLock()
	defer t.mu.RUnlock()

	ws := types.WisdomStats{
		TotalDecisions:    t.count,
		ResolvedDecisions: t.stats.Total,
		CorrectDecisions:  t.stats.Correct,
		LearnedRules:      t.wisdom,
		Frozen:            t.frozen,
	}

	if t.stats.Total > 0 {
		ws.AccuracyPct = float64(t.stats.Correct) * 100.0 / float64(t.stats.Total)
	}
	if t.stats.BuysTotal > 0 {
		ws.BuyAccuracy = float64(t.stats.BuysCorrect) * 100.0 / float64(t.stats.BuysTotal)
	}
	if t.stats.HoldsTotal > 0 {
		ws.HoldAccuracy = float64(t.stats.HoldsCorrect) * 100.0 / float64(t.stats.HoldsTotal)
	}

	return ws
}

// SetFrozen enables or disables learning.
func (t *Tracker) SetFrozen(frozen bool) {
	t.mu.Lock()
	t.frozen = frozen
	t.mu.Unlock()

	if t.db != nil {
		v := 0
		if frozen {
			v = 1
		}
		t.db.Exec("UPDATE wisdom_stats SET frozen = ? WHERE id = 1", v)
	}
	slog.Info("wisdom learning", "frozen", frozen)
}

// IsFrozen returns whether learning is frozen.
func (t *Tracker) IsFrozen() bool {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.frozen
}

func (t *Tracker) regenerateWisdom() {
	var text string
	rule := 1

	if t.stats.Total > 0 {
		pct := t.stats.Correct * 100 / t.stats.Total
		text += fmt.Sprintf("%d. Overall: %d/%d correct (%d%%)", rule, t.stats.Correct, t.stats.Total, pct)
		if pct >= 60 {
			text += " — positive edge confirmed.\n"
		} else if pct >= 45 {
			text += " — near breakeven, tighten filters.\n"
		} else {
			text += " — negative edge, reduce risk.\n"
		}
		rule++
	}

	if t.stats.HoldsTotal > 0 && t.stats.BuysTotal > 0 {
		hp := t.stats.HoldsCorrect * 100 / t.stats.HoldsTotal
		bp := t.stats.BuysCorrect * 100 / t.stats.BuysTotal
		text += fmt.Sprintf("%d. Holds %d%% vs Buys %d%% correct", rule, hp, bp)
		if hp > bp+10 {
			text += " — holding saves capital.\n"
		} else if bp > hp+10 {
			text += " — buying edge is strong.\n"
		} else {
			text += " — similar performance.\n"
		}
		rule++
	} else if t.stats.HoldsTotal > 0 {
		hp := t.stats.HoldsCorrect * 100 / t.stats.HoldsTotal
		text += fmt.Sprintf("%d. Holds: %d%% correct (%d/%d).\n", rule, hp, t.stats.HoldsCorrect, t.stats.HoldsTotal)
		rule++
	} else if t.stats.BuysTotal > 0 {
		bp := t.stats.BuysCorrect * 100 / t.stats.BuysTotal
		text += fmt.Sprintf("%d. Buys: %d%% correct (%d/%d).\n", rule, bp, t.stats.BuysCorrect, t.stats.BuysTotal)
		rule++
	}

	if len(text) > maxWisdomBytes {
		text = text[:maxWisdomBytes]
		if idx := lastIndex(text, '\n'); idx > 0 {
			text = text[:idx+1]
		}
	}

	t.wisdom = text
}

func (t *Tracker) loadFromDB() {
	if t.db == nil {
		return
	}

	row := t.db.QueryRow("SELECT total, correct, holds_total, holds_correct, buys_total, buys_correct, frozen FROM wisdom_stats WHERE id = 1")
	var frozen int
	if err := row.Scan(&t.stats.Total, &t.stats.Correct, &t.stats.HoldsTotal, &t.stats.HoldsCorrect,
		&t.stats.BuysTotal, &t.stats.BuysCorrect, &frozen); err == nil {
		t.frozen = frozen != 0
	}

	// Load wisdom text from latest rule.
	row = t.db.QueryRow("SELECT rule_text FROM wisdom_rules ORDER BY id DESC LIMIT 1")
	row.Scan(&t.wisdom)

	slog.Info("wisdom loaded", "total", t.stats.Total, "correct", t.stats.Correct,
		"frozen", t.frozen, "wisdom_len", len(t.wisdom))
}

func (t *Tracker) saveStatsToDB() {
	if t.db == nil {
		return
	}
	t.db.Exec(`UPDATE wisdom_stats SET total=?, correct=?, holds_total=?, holds_correct=?,
		buys_total=?, buys_correct=? WHERE id = 1`,
		t.stats.Total, t.stats.Correct, t.stats.HoldsTotal, t.stats.HoldsCorrect,
		t.stats.BuysTotal, t.stats.BuysCorrect)
}

func (t *Tracker) saveWisdomToDB() {
	if t.db == nil || t.wisdom == "" {
		return
	}
	t.db.Exec("INSERT INTO wisdom_rules (rule_text) VALUES (?)", t.wisdom)
}

func contains(s, sub string) bool {
	for i := 0; i+len(sub) <= len(s); i++ {
		if s[i:i+len(sub)] == sub {
			return true
		}
	}
	return false
}

func lastIndex(s string, b byte) int {
	for i := len(s) - 1; i >= 0; i-- {
		if s[i] == b {
			return i
		}
	}
	return -1
}
