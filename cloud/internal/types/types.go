package types

// MarketSnapshot represents a Polymarket market's current state.
type MarketSnapshot struct {
	ID           string  `json:"id"`
	Question     string  `json:"question"`
	Slug         string  `json:"slug"`
	Category     string  `json:"category"`
	EndDate      string  `json:"end_date"`
	ClobTokenYes string  `json:"clob_token_yes"`
	ClobTokenNo  string  `json:"clob_token_no"`
	Liquidity    float64 `json:"liquidity"`
	Volume       float64 `json:"volume"`
	YesPrice     float64 `json:"yes_price"`
	NoPrice      float64 `json:"no_price"`
}

// GeoblockStatus represents the Polymarket geo-restriction check result.
type GeoblockStatus struct {
	Blocked bool   `json:"blocked"`
	Country string `json:"country"`
	Region  string `json:"region"`
	IP      string `json:"ip"`
}

// UsageStats represents token usage from an LLM response.
type UsageStats struct {
	PromptTokens     int `json:"prompt_tokens"`
	CompletionTokens int `json:"completion_tokens"`
}

// ToolCall represents a parsed tool_call decision from the LLM.
type ToolCall struct {
	Valid  bool
	Tool   string
	Order  string
	Limit  int
	Offset int
}

// Decision represents a parsed trading decision from the LLM.
type Decision struct {
	Type         string  `json:"type"`
	MarketID     string  `json:"market_id"`
	Side         string  `json:"side"`
	EdgeBps      float64 `json:"edge_bps"`
	Confidence   float64 `json:"confidence"`
	SizeFraction float64 `json:"size_fraction"`
	Rationale    string  `json:"rationale"`
}

// Position represents an open paper or live trading position.
type Position struct {
	MarketID   string  `json:"market_id"`
	Question   string  `json:"question"`
	Side       string  `json:"side"`
	EntryPrice float64 `json:"entry_price"`
	Shares     float64 `json:"shares"`
	StakeUsdc  float64 `json:"stake_usdc"`
	IsLive     bool    `json:"is_live"`
	OrderID    string  `json:"order_id"`
}

// ScoutedMarket represents a market the agent rated during a cycle.
type ScoutedMarket struct {
	Epoch      int64   `json:"epoch"`
	MarketID   string  `json:"market_id"`
	Question   string  `json:"question"`
	Signal     string  `json:"signal"`
	EdgeBps    float64 `json:"edge_bps"`
	Confidence float64 `json:"confidence"`
	Note       string  `json:"note"`
	YesPrice   float64 `json:"yes_price"`
	Volume     float64 `json:"volume"`
	Liquidity  float64 `json:"liquidity"`
}

// BudgetInfo is a snapshot of the ledger state for display.
type BudgetInfo struct {
	Cash        float64 `json:"cash"`
	Reserve     float64 `json:"reserve"`
	Equity      float64 `json:"equity"`
	LlmSpend    float64 `json:"llm_spend"`
	RealizedPnl float64 `json:"realized_pnl"`
	DailyLoss   float64 `json:"daily_loss"`
}

// LogEntry represents a timestamped agent log line.
type LogEntry struct {
	Time     int64  `json:"time"`
	Type     string `json:"type"`
	Message  string `json:"message"`
	MarketID string `json:"market_id,omitempty"`
}

// WisdomStats holds the agent's learning statistics.
type WisdomStats struct {
	TotalDecisions    int     `json:"total_decisions"`
	ResolvedDecisions int     `json:"resolved_decisions"`
	CorrectDecisions  int     `json:"correct_decisions"`
	AccuracyPct       float64 `json:"accuracy_pct"`
	BuyAccuracy       float64 `json:"buy_accuracy"`
	HoldAccuracy      float64 `json:"hold_accuracy"`
	LearnedRules      string  `json:"learned_rules"`
	Frozen            bool    `json:"frozen"`
}

// DecisionRecord is a dashboard-facing record of a single agent decision.
type DecisionRecord struct {
	Epoch          int64   `json:"epoch"`
	Type           string  `json:"type"`
	MarketID       string  `json:"market_id"`
	MarketQuestion string  `json:"question"`
	Side           string  `json:"side"`
	Confidence     float64 `json:"confidence"`
	EdgeBps        float64 `json:"edge_bps"`
	SizeUsdc       float64 `json:"size_usdc"`
	Rationale      string  `json:"rationale"`
}

// EquitySnapshot records equity at a point in time.
type EquitySnapshot struct {
	Epoch  int64   `json:"epoch"`
	Equity float64 `json:"equity"`
	Cash   float64 `json:"cash"`
}

// FindMarket searches for a market by ID in a slice.
func FindMarket(markets []MarketSnapshot, id string) *MarketSnapshot {
	for i := range markets {
		if markets[i].ID == id {
			return &markets[i]
		}
	}
	return nil
}
