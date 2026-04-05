package dashboard

import (
	"encoding/json"
	"runtime"
	"sync"
	"time"

	"survaiv/internal/dynconfig"
	"survaiv/internal/types"
)

const (
	maxDecisionHistory = 50
	maxEquityHistory   = 200
	maxScoutedMarkets  = 20
)

// State holds all dashboard state, shared between agent and HTTP server.
type State struct {
	mu sync.RWMutex

	budget         types.BudgetInfo
	positions      []types.Position
	markets        []types.MarketSnapshot
	decisions      []types.DecisionRecord
	equityHistory  []types.EquitySnapshot
	scoutedMarkets []types.ScoutedMarket

	agentStatus string
	cycleCount  int
	bootEpoch   int64

	geoblocked bool
	geoCountry string

	walletAddress string
	usdcBalance   float64
	liveMode      bool
	inferenceSpent float64
	activeModel   string
	modelPrice    float64
	lastError     string
	nextRetrySec  int
	nextCycleEpoch int64
	paperOnly     bool
	toolUsage     int
	agentName     string

	efficiency *dynconfig.RuntimeConfig

	// CPU usage tracking.
	prevCPUUser int64
	prevCPUSys  int64
	prevCPUTime time.Time
	cpuPercent  float64

	sseClients []chan string

	onResetPaper func() // callback to reset ledger; set by agent
}

func NewState() *State {
	s := &State{
		agentStatus: "initializing",
		bootEpoch:   time.Now().Unix(),
		prevCPUTime: time.Now(),
	}
	// Initialize CPU baseline.
	if u, sys, ok := getCPUUsage(); ok {
		s.prevCPUUser = u
		s.prevCPUSys = sys
	}
	return s
}

// SetResetPaperFunc sets the callback invoked when the user resets paper trading.
func (s *State) SetResetPaperFunc(fn func()) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.onResetPaper = fn
}

// ResetPaperTrading clears all dashboard financial state and invokes the ledger reset callback.
func (s *State) ResetPaperTrading() bool {
	s.mu.Lock()
	if !s.paperOnly {
		s.mu.Unlock()
		return false
	}
	fn := s.onResetPaper
	s.decisions = nil
	s.equityHistory = nil
	s.positions = nil
	s.budget = types.BudgetInfo{}
	s.scoutedMarkets = nil
	s.cycleCount = 0
	s.inferenceSpent = 0
	s.lastError = ""
	s.mu.Unlock()
	if fn != nil {
		fn()
	}
	return true
}

func (s *State) UpdateBudget(b types.BudgetInfo) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.budget = b
	snap := types.EquitySnapshot{
		Epoch:  time.Now().Unix(),
		Equity: b.Equity,
		Cash:   b.Cash,
	}
	if len(s.equityHistory) >= maxEquityHistory {
		s.equityHistory = s.equityHistory[1:]
	}
	s.equityHistory = append(s.equityHistory, snap)
}

func (s *State) UpdatePositions(p []types.Position) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.positions = make([]types.Position, len(p))
	copy(s.positions, p)
}

func (s *State) UpdateMarkets(m []types.MarketSnapshot) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.markets = make([]types.MarketSnapshot, len(m))
	copy(s.markets, m)
}

// UpdateMarketSnapshots is an alias for UpdateMarkets (backward compat).
func (s *State) UpdateMarketSnapshots(m []types.MarketSnapshot) {
	s.UpdateMarkets(m)
}

func (s *State) PushDecision(r types.DecisionRecord) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if len(s.decisions) >= maxDecisionHistory {
		s.decisions = s.decisions[1:]
	}
	s.decisions = append(s.decisions, r)
}

func (s *State) SetAgentStatus(status string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.agentStatus = status
}

func (s *State) SetGeoblock(blocked bool, country string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.geoblocked = blocked
	s.geoCountry = country
}

func (s *State) SetWalletInfo(address string, balance float64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.walletAddress = address
	s.usdcBalance = balance
}

// SetWallet is an alias for SetWalletInfo (backward compat).
func (s *State) SetWallet(address string, balance float64) {
	s.SetWalletInfo(address, balance)
}

func (s *State) SetLiveMode(enabled bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.liveMode = enabled
}

func (s *State) SetActiveModel(name string, price float64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.activeModel = name
	s.modelPrice = price
}

func (s *State) SetScoutedMarkets(scouted []types.ScoutedMarket) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.scoutedMarkets = scouted
	if len(s.scoutedMarkets) > maxScoutedMarkets {
		s.scoutedMarkets = s.scoutedMarkets[:maxScoutedMarkets]
	}
}

func (s *State) IncrementCycleCount() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.cycleCount++
}

// SetCycleCount sets the cycle counter directly (backward compat).
func (s *State) SetCycleCount(count int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.cycleCount = count
}

func (s *State) SetLastError(err string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.lastError = err
}

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

// SetNextCycleEpoch records when the next agent cycle will start.
func (s *State) SetNextCycleEpoch(epoch int64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.nextCycleEpoch = epoch
}

// SetPaperOnly sets the paper trading flag.
func (s *State) SetPaperOnly(v bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.paperOnly = v
}

// SetToolUsage sets the tool usage level (0=frugal, 1=balanced, 2=generous).
func (s *State) SetToolUsage(level int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.toolUsage = level
}

// SetAgentName sets the agent display name.
func (s *State) SetAgentName(name string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.agentName = name
}

// SetEfficiency stores the current runtime config for dashboard display.
func (s *State) SetEfficiency(rc *dynconfig.RuntimeConfig) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.efficiency = rc
}

// SetInferenceSpend sets the inference spend amount.
func (s *State) SetInferenceSpend(usdc float64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.inferenceSpent = usdc
}

// SetOaiConfig is a no-op retained for backward compat.
func (s *State) SetOaiConfig(_, _ string) {}

// SetUptimeSeconds is a no-op; uptime is computed from bootEpoch.
func (s *State) SetUptimeSeconds(_ int64) {}

// RecordEquity appends an equity snapshot (backward compat; UpdateBudget also records).
func (s *State) RecordEquity(snap types.EquitySnapshot) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if len(s.equityHistory) >= maxEquityHistory {
		s.equityHistory = s.equityHistory[1:]
	}
	s.equityHistory = append(s.equityHistory, snap)
}

// ── JSON serialization (called by handlers) ─────────────────────

func (s *State) ToJSON() []byte {
	s.mu.RLock()
	defer s.mu.RUnlock()

	uptime := time.Now().Unix() - s.bootEpoch
	data := map[string]interface{}{
		"status":               s.agentStatus,
		"cycle_count":          s.cycleCount,
		"uptime_seconds":       uptime,
		"live_mode":            s.liveMode,
		"geoblocked":           s.geoblocked,
		"geo_country":          s.geoCountry,
		"wallet":               s.walletAddress,
		"usdc_balance":         s.usdcBalance,
		"budget":               s.budget,
		"inference_spent_usdc": s.inferenceSpent,
		"active_model":         s.activeModel,
		"model_price":          s.modelPrice,
		"open_positions":       len(s.positions),
		"paper_only":           s.paperOnly,
		"tool_usage":           s.toolUsage,
		"agent_name":           s.agentName,
	}
	if s.lastError != "" {
		data["last_error"] = s.lastError
		data["next_retry_sec"] = s.nextRetrySec
	}
	if s.nextCycleEpoch > 0 {
		data["next_cycle_epoch"] = s.nextCycleEpoch
	}
	if s.efficiency != nil {
		rc := s.efficiency
		bd := rc.Breakdown()
		platforms := map[string]int{
			"esp32_c3_ota": 12,
			"esp32_c3":     22,
			"esp32_s3":     38,
			"cloud":        rc.EfficiencyScore,
		}
		data["efficiency"] = map[string]interface{}{
			"score":            rc.EfficiencyScore,
			"platform":         rc.Platform,
			"cpu_cores":        rc.CPUCores,
			"prompt_budget":    rc.PromptBudget,
			"max_completion":   rc.MaxCompletion,
			"market_limit":     rc.MarketLimit,
			"wisdom_budget":    rc.WisdomBudget,
			"parallel_workers": rc.ParallelWorkers,
			"model_context_k":  rc.ModelContextK,
			"breakdown": map[string]int{
				"context":     bd.Context,
				"parallelism": bd.Parallelism,
				"memory":      bd.Memory,
				"coverage":    bd.Coverage,
				"wisdom":      bd.Wisdom,
			},
			"platforms": platforms,
		}
	}

	// System stats + CPU sampling.
	var memStats runtime.MemStats
	runtime.ReadMemStats(&memStats)

	// Sample process CPU usage.
	numCPU := runtime.NumCPU()
	var cpuPcts []int
	if userUs, sysUs, ok := getCPUUsage(); ok {
		elapsed := time.Since(s.prevCPUTime).Microseconds()
		if elapsed > 0 {
			cpuUs := (userUs - s.prevCPUUser) + (sysUs - s.prevCPUSys)
			totalPct := float64(cpuUs) * 100.0 / float64(elapsed)
			perCore := int(totalPct / float64(numCPU))
			if perCore > 100 {
				perCore = 100
			}
			if perCore < 0 {
				perCore = 0
			}
			cpuPcts = make([]int, numCPU)
			for i := range cpuPcts {
				cpuPcts[i] = perCore
			}
			s.cpuPercent = totalPct
		}
		s.prevCPUUser = userUs
		s.prevCPUSys = sysUs
		s.prevCPUTime = time.Now()
	}
	if cpuPcts == nil {
		cpuPcts = make([]int, numCPU) // zeros
	}

	data["sys"] = map[string]interface{}{
		"cores":       numCPU,
		"goroutines":  runtime.NumGoroutine(),
		"alloc":       memStats.Alloc,
		"total_alloc": memStats.TotalAlloc,
		"sys":         memStats.Sys,
		"gc_cycles":   memStats.NumGC,
		"cpu":         cpuPcts,
	}

	b, _ := json.Marshal(data)
	return b
}

// ToJSONString returns the full state as a JSON string (convenience).
func (s *State) ToJSONString() string {
	return string(s.ToJSON())
}

func (s *State) PositionsJSON() []byte {
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
		for _, m := range s.markets {
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

	b, _ := json.Marshal(out)
	return b
}

func (s *State) DecisionsJSON() []byte {
	s.mu.RLock()
	defer s.mu.RUnlock()
	reversed := make([]types.DecisionRecord, len(s.decisions))
	for i, d := range s.decisions {
		reversed[len(s.decisions)-1-i] = d
	}
	b, _ := json.Marshal(reversed)
	return b
}

func (s *State) EquityHistoryJSON() []byte {
	s.mu.RLock()
	defer s.mu.RUnlock()
	b, _ := json.Marshal(s.equityHistory)
	return b
}

func (s *State) ScoutedMarketsJSON() []byte {
	s.mu.RLock()
	defer s.mu.RUnlock()
	b, _ := json.Marshal(s.scoutedMarkets)
	return b
}

// ── Snapshot for TUI ────────────────────────────────────────────

// Snapshot returns a read-only snapshot for the TUI.
func (s *State) Snapshot() StateSnapshot {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return StateSnapshot{
		Budget:    s.budget,
		Positions: append([]types.Position(nil), s.positions...),
		Scouted:   append([]types.ScoutedMarket(nil), s.scoutedMarkets...),
		Decisions: append([]types.DecisionRecord(nil), s.decisions...),
		Status:    s.agentStatus,
		Model:     s.activeModel,
		Uptime:    time.Now().Unix() - s.bootEpoch,
		Cycles:    s.cycleCount,
		AgentName: s.agentName,
	}
}

// GetSnapshot returns the legacy Snapshot (backward compat for TUI).
func (s *State) GetSnapshot() Snapshot {
	s.mu.RLock()
	defer s.mu.RUnlock()

	snap := Snapshot{
		Budget:         s.budget,
		Status:         s.agentStatus,
		LiveMode:       s.liveMode,
		CycleCount:     s.cycleCount,
		UptimeSeconds:  time.Now().Unix() - s.bootEpoch,
		ActiveModel:    s.activeModel,
		ModelPrice:     s.modelPrice,
		Firmware:       "cloud",
		Wallet:         s.walletAddress,
		UsdcBalance:    s.usdcBalance,
		LastError:      s.lastError,
		InferenceSpent: s.inferenceSpent,
	}

	snap.Positions = make([]types.Position, len(s.positions))
	copy(snap.Positions, s.positions)

	snap.ScoutedMarkets = make([]types.ScoutedMarket, len(s.scoutedMarkets))
	copy(snap.ScoutedMarkets, s.scoutedMarkets)

	snap.Decisions = make([]types.DecisionRecord, len(s.decisions))
	copy(snap.Decisions, s.decisions)

	return snap
}

type StateSnapshot struct {
	Budget    types.BudgetInfo
	Positions []types.Position
	Scouted   []types.ScoutedMarket
	Decisions []types.DecisionRecord
	Status    string
	Model     string
	Uptime    int64
	Cycles    int
	AgentName string
}

// Snapshot is the legacy TUI snapshot type (backward compat).
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

// ── SSE support ─────────────────────────────────────────────────

func (s *State) RegisterSSEClient() chan string {
	s.mu.Lock()
	defer s.mu.Unlock()
	ch := make(chan string, 16)
	s.sseClients = append(s.sseClients, ch)
	return ch
}

func (s *State) UnregisterSSEClient(ch chan string) {
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

func (s *State) PushSSE(event, data string) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	msg := "event: " + event + "\ndata: " + data + "\n\n"
	for _, ch := range s.sseClients {
		select {
		case ch <- msg:
		default: // drop if full
		}
	}
}

// Subscribe is an alias for RegisterSSEClient (backward compat).
func (s *State) Subscribe() chan string {
	return s.RegisterSSEClient()
}

// Unsubscribe is an alias for UnregisterSSEClient (backward compat).
func (s *State) Unsubscribe(ch chan string) {
	s.UnregisterSSEClient(ch)
}

// PushEvent is an alias for PushSSE (backward compat).
func (s *State) PushEvent(event, data string) {
	s.PushSSE(event, data)
}

// TelemetryReport returns the full dashboard state as a map for telemetry.
func (s *State) TelemetryReport() map[string]any {
	s.mu.RLock()
	defer s.mu.RUnlock()

	report := map[string]any{
		"status":       s.agentStatus,
		"cycle_count":  s.cycleCount,
		"uptime":       time.Now().Unix() - s.bootEpoch,
		"agent_name":   s.agentName,
		"live_mode":    s.liveMode,
		"paper_only":   s.paperOnly,
		"active_model": s.activeModel,
		"budget":       s.budget,
		"positions":    s.positions,
		"decisions":    s.decisions,
		"equity":       s.equityHistory,
	}
	return report
}
