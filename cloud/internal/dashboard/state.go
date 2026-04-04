package dashboard

import (
	"encoding/json"
	"sync"
	"time"

	"survaiv/internal/types"
)

const maxLogEntries = 50
const maxEquitySnapshots = 200

// State is the thread-safe shared state between the agent and dashboard.
type State struct {
	mu sync.RWMutex

	budget         types.BudgetInfo
	positions      []types.Position
	marketSnaps    []types.MarketSnapshot
	scoutedMarkets []types.ScoutedMarket
	decisions      []types.DecisionRecord
	equityHistory  []types.EquitySnapshot
	status         string
	liveMode       bool
	cycleCount     int
	uptimeSeconds  int64
	activeModel    string
	modelPrice     float64
	firmware       string
	wallet         string
	usdcBalance    float64
	lastError      string
	nextRetrySec   int
	oaiURL         string
	oaiModel       string
	inferenceSpent float64
	geoblocked     bool
	geoCountry     string

	// SSE subscribers.
	sseClients []chan string
}

// NewState creates a new dashboard State.
func NewState() *State {
	return &State{
		status:   "starting",
		firmware: "cloud",
	}
}

// UpdateBudget updates the budget snapshot.
func (s *State) UpdateBudget(b types.BudgetInfo) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.budget = b
}

// UpdatePositions updates the positions list.
func (s *State) UpdatePositions(positions []types.Position) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.positions = make([]types.Position, len(positions))
	copy(s.positions, positions)
}

// UpdateMarketSnapshots stores the latest market data.
func (s *State) UpdateMarketSnapshots(markets []types.MarketSnapshot) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.marketSnaps = make([]types.MarketSnapshot, len(markets))
	copy(s.marketSnaps, markets)
}

// SetScoutedMarkets updates the scouted markets from agent analysis.
func (s *State) SetScoutedMarkets(scouted []types.ScoutedMarket) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.scoutedMarkets = make([]types.ScoutedMarket, len(scouted))
	copy(s.scoutedMarkets, scouted)
}

// SetAgentStatus sets the current agent status string.
func (s *State) SetAgentStatus(status string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.status = status
}

// SetGeoblock sets geoblock state.
func (s *State) SetGeoblock(blocked bool, country string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.geoblocked = blocked
	s.geoCountry = country
}

// SetLiveMode sets whether the agent is in live mode.
func (s *State) SetLiveMode(live bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.liveMode = live
}

// SetCycleCount sets the cycle counter.
func (s *State) SetCycleCount(count int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.cycleCount = count
}

// SetUptimeSeconds sets the uptime.
func (s *State) SetUptimeSeconds(sec int64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.uptimeSeconds = sec
}

// SetActiveModel sets the active LLM model and price.
func (s *State) SetActiveModel(name string, price float64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.activeModel = name
	s.modelPrice = price
}

// SetWallet sets the wallet address and balance.
func (s *State) SetWallet(address string, balance float64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.wallet = address
	s.usdcBalance = balance
}

// SetLastError sets the error message.
func (s *State) SetLastError(msg string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.lastError = msg
}

// ClearError clears the error state.
func (s *State) ClearError() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.lastError = ""
	s.nextRetrySec = 0
}

// SetNextRetrySec sets the retry countdown.
func (s *State) SetNextRetrySec(sec int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.nextRetrySec = sec
}

// SetOaiConfig stores the current LLM config for the settings panel.
func (s *State) SetOaiConfig(url, model string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.oaiURL = url
	s.oaiModel = model
}

// SetInferenceSpend sets the x402 inference spend amount.
func (s *State) SetInferenceSpend(usdc float64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.inferenceSpent = usdc
}

// PushDecision appends a decision to the log.
func (s *State) PushDecision(d types.DecisionRecord) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.decisions = append([]types.DecisionRecord{d}, s.decisions...)
	if len(s.decisions) > maxLogEntries {
		s.decisions = s.decisions[:maxLogEntries]
	}
}

// RecordEquity appends an equity snapshot.
func (s *State) RecordEquity(snap types.EquitySnapshot) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.equityHistory = append(s.equityHistory, snap)
	if len(s.equityHistory) > maxEquitySnapshots {
		s.equityHistory = s.equityHistory[len(s.equityHistory)-maxEquitySnapshots:]
	}
}

// Budget returns the current budget info.
func (s *State) Budget() types.BudgetInfo {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.budget
}

// Positions returns a copy of current positions.
func (s *State) Positions() []types.Position {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]types.Position, len(s.positions))
	copy(out, s.positions)
	return out
}

// Markets returns the latest market snapshots.
func (s *State) Markets() []types.MarketSnapshot {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]types.MarketSnapshot, len(s.marketSnaps))
	copy(out, s.marketSnaps)
	return out
}

// ScoutedMarkets returns the scouted markets list.
func (s *State) ScoutedMarkets() []types.ScoutedMarket {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]types.ScoutedMarket, len(s.scoutedMarkets))
	copy(out, s.scoutedMarkets)
	return out
}

// Decisions returns the decision log.
func (s *State) Decisions() []types.DecisionRecord {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]types.DecisionRecord, len(s.decisions))
	copy(out, s.decisions)
	return out
}

// EquityHistory returns the equity snapshots.
func (s *State) EquityHistory() []types.EquitySnapshot {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]types.EquitySnapshot, len(s.equityHistory))
	copy(out, s.equityHistory)
	return out
}

// Status returns the current status string.
func (s *State) Status() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.status
}

// CycleCount returns the cycle count.
func (s *State) CycleCount() int {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.cycleCount
}

// UptimeSeconds returns the uptime.
func (s *State) UptimeSeconds() int64 {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.uptimeSeconds
}

// ActiveModel returns the current model name and price.
func (s *State) ActiveModel() (string, float64) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.activeModel, s.modelPrice
}

// ToJSON returns the full state as a JSON string matching the C3 /api/state format.
func (s *State) ToJSON() string {
	s.mu.RLock()
	defer s.mu.RUnlock()

	data, _ := json.Marshal(s.statePayload())
	return string(data)
}

func (s *State) statePayload() map[string]interface{} {
	return map[string]interface{}{
		"budget": map[string]interface{}{
			"cash":         s.budget.Cash,
			"reserve":      s.budget.Reserve,
			"equity":       s.budget.Equity,
			"llm_spend":    s.budget.LlmSpend,
			"realized_pnl": s.budget.RealizedPnl,
			"daily_loss":   s.budget.DailyLoss,
		},
		"status":              s.status,
		"live_mode":           s.liveMode,
		"cycle_count":         s.cycleCount,
		"uptime_seconds":      s.uptimeSeconds,
		"active_model":        s.activeModel,
		"model_price":         s.modelPrice,
		"firmware":            s.firmware,
		"wallet":              s.wallet,
		"usdc_balance":        s.usdcBalance,
		"last_error":          s.lastError,
		"next_retry_sec":      s.nextRetrySec,
		"oai_url":             s.oaiURL,
		"oai_model":           s.oaiModel,
		"inference_spent_usdc": s.inferenceSpent,
	}
}

// PositionsJSON returns positions with unrealized P&L as JSON.
func (s *State) PositionsJSON() string {
	s.mu.RLock()
	defer s.mu.RUnlock()

	type posJSON struct {
		MarketID      string  `json:"market_id"`
		Question      string  `json:"question"`
		Side          string  `json:"side"`
		EntryPrice    float64 `json:"entry_price"`
		CurrentPrice  float64 `json:"current_price"`
		UnrealizedPnl float64 `json:"unrealized_pnl"`
		StakeUsdc     float64 `json:"stake_usdc"`
		IsLive        bool    `json:"is_live"`
	}

	out := make([]posJSON, 0, len(s.positions))
	for _, p := range s.positions {
		cp := p.EntryPrice
		var upnl float64
		for _, m := range s.marketSnaps {
			if m.ID == p.MarketID {
				if p.Side == "yes" {
					cp = m.YesPrice
				} else {
					cp = m.NoPrice
				}
				upnl = (cp - p.EntryPrice) * p.Shares
				break
			}
		}
		out = append(out, posJSON{
			MarketID:      p.MarketID,
			Question:      p.Question,
			Side:          p.Side,
			EntryPrice:    p.EntryPrice,
			CurrentPrice:  cp,
			UnrealizedPnl: upnl,
			StakeUsdc:     p.StakeUsdc,
			IsLive:        p.IsLive,
		})
	}

	data, _ := json.Marshal(out)
	return string(data)
}

// DecisionsJSON returns the decision log as JSON.
func (s *State) DecisionsJSON() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	data, _ := json.Marshal(s.decisions)
	return string(data)
}

// ScoutedMarketsJSON returns scouted markets as JSON.
func (s *State) ScoutedMarketsJSON() string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	data, _ := json.Marshal(s.scoutedMarkets)
	return string(data)
}

// EquityHistoryJSON returns equity history as JSON for the chart.
func (s *State) EquityHistoryJSON() string {
	s.mu.RLock()
	defer s.mu.RUnlock()

	// Return as array of [epoch, equity] pairs for the chart.
	pairs := make([][2]float64, len(s.equityHistory))
	for i, snap := range s.equityHistory {
		pairs[i] = [2]float64{float64(snap.Epoch), snap.Equity}
	}
	data, _ := json.Marshal(pairs)
	return string(data)
}

// Subscribe registers a channel for SSE events.
func (s *State) Subscribe() chan string {
	s.mu.Lock()
	defer s.mu.Unlock()
	ch := make(chan string, 16)
	s.sseClients = append(s.sseClients, ch)
	return ch
}

// Unsubscribe removes an SSE channel.
func (s *State) Unsubscribe(ch chan string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for i, c := range s.sseClients {
		if c == ch {
			s.sseClients = append(s.sseClients[:i], s.sseClients[i+1:]...)
			close(ch)
			return
		}
	}
}

// PushEvent broadcasts an SSE event to all subscribers.
func (s *State) PushEvent(eventType, data string) {
	msg := "event: " + eventType + "\ndata: " + data + "\n\n"

	s.mu.RLock()
	clients := make([]chan string, len(s.sseClients))
	copy(clients, s.sseClients)
	s.mu.RUnlock()

	for _, ch := range clients {
		select {
		case ch <- msg:
		default:
			// Drop if buffer full.
		}
	}
}

// Snapshot returns a snapshot for the TUI.
type Snapshot struct {
	Budget         types.BudgetInfo
	Positions      []types.Position
	ScoutedMarkets []types.ScoutedMarket
	Decisions      []types.DecisionRecord
	Status         string
	LiveMode       bool
	CycleCount     int
	UptimeSeconds  int64
	ActiveModel    string
	ModelPrice     float64
	Firmware       string
	Wallet         string
	UsdcBalance    float64
	LastError      string
	InferenceSpent float64
	OaiURL         string
	OaiModel       string
}

// GetSnapshot returns a point-in-time snapshot of dashboard state.
func (s *State) GetSnapshot() Snapshot {
	s.mu.RLock()
	defer s.mu.RUnlock()

	snap := Snapshot{
		Budget:         s.budget,
		Status:         s.status,
		LiveMode:       s.liveMode,
		CycleCount:     s.cycleCount,
		UptimeSeconds:  s.uptimeSeconds,
		ActiveModel:    s.activeModel,
		ModelPrice:     s.modelPrice,
		Firmware:       s.firmware,
		Wallet:         s.wallet,
		UsdcBalance:    s.usdcBalance,
		LastError:      s.lastError,
		InferenceSpent: s.inferenceSpent,
		OaiURL:         s.oaiURL,
		OaiModel:       s.oaiModel,
	}

	snap.Positions = make([]types.Position, len(s.positions))
	copy(snap.Positions, s.positions)

	snap.ScoutedMarkets = make([]types.ScoutedMarket, len(s.scoutedMarkets))
	copy(snap.ScoutedMarkets, s.scoutedMarkets)

	snap.Decisions = make([]types.DecisionRecord, len(s.decisions))
	copy(snap.Decisions, s.decisions)

	_ = time.Now() // ensure time package used for future features

	return snap
}
