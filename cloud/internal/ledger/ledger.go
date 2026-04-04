package ledger

import (
	"log/slog"
	"sync"

	"survaiv/internal/types"
)

// Ledger tracks paper trading state: cash, positions, P&L, and LLM spend.
type Ledger struct {
	mu           sync.RWMutex
	cash         float64
	reserve      float64
	llmSpend     float64
	realizedPnl  float64
	dailyLoss    float64
	positions    []types.Position
	maxPositions int
}

// New creates a ledger with starting USDC, reserve amount, and max open positions.
func New(startingUsdc, reserveUsdc float64, maxPositions int) *Ledger {
	if maxPositions <= 0 {
		maxPositions = 5
	}
	return &Ledger{
		cash:         startingUsdc,
		reserve:      reserveUsdc,
		maxPositions: maxPositions,
	}
}

// Cash returns the available cash balance.
func (l *Ledger) Cash() float64 {
	l.mu.RLock()
	defer l.mu.RUnlock()
	return l.cash
}

// Reserve returns the reserve amount.
func (l *Ledger) Reserve() float64 {
	l.mu.RLock()
	defer l.mu.RUnlock()
	return l.reserve
}

// LlmSpend returns cumulative LLM inference spend.
func (l *Ledger) LlmSpend() float64 {
	l.mu.RLock()
	defer l.mu.RUnlock()
	return l.llmSpend
}

// RealizedPnl returns cumulative realized P&L from closed positions.
func (l *Ledger) RealizedPnl() float64 {
	l.mu.RLock()
	defer l.mu.RUnlock()
	return l.realizedPnl
}

// DailyLoss returns the daily loss accumulator.
func (l *Ledger) DailyLoss() float64 {
	l.mu.RLock()
	defer l.mu.RUnlock()
	return l.dailyLoss
}

// Positions returns a copy of all open positions.
func (l *Ledger) Positions() []types.Position {
	l.mu.RLock()
	defer l.mu.RUnlock()
	out := make([]types.Position, len(l.positions))
	copy(out, l.positions)
	return out
}

// OpenPositionCount returns the number of open positions.
func (l *Ledger) OpenPositionCount() int {
	l.mu.RLock()
	defer l.mu.RUnlock()
	return len(l.positions)
}

// Equity computes total equity = cash + unrealized value of all positions.
func (l *Ledger) Equity(markets []types.MarketSnapshot) float64 {
	l.mu.RLock()
	defer l.mu.RUnlock()
	return l.equityLocked(markets)
}

func (l *Ledger) equityLocked(markets []types.MarketSnapshot) float64 {
	eq := l.cash
	for _, pos := range l.positions {
		price := currentPrice(markets, pos.MarketID, pos.Side)
		eq += pos.Shares * price
	}
	return eq
}

// CanSpendOnInference returns true if the agent can afford usdc for inference
// without dipping into the reserve.
func (l *Ledger) CanSpendOnInference(usdc float64, markets []types.MarketSnapshot) bool {
	l.mu.RLock()
	defer l.mu.RUnlock()
	return l.equityLocked(markets)-l.llmSpend-l.reserve > usdc
}

// DebitInference records an LLM inference cost.
func (l *Ledger) DebitInference(usdc float64) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.llmSpend += usdc
	l.cash -= usdc
	if l.cash < 0 {
		l.cash = 0
	}
}

// OpenPaperPosition opens a new paper position on a market.
// Returns false if max positions reached or insufficient cash.
func (l *Ledger) OpenPaperPosition(market types.MarketSnapshot, side string, sizeUsdc float64) bool {
	l.mu.Lock()
	defer l.mu.Unlock()

	if len(l.positions) >= l.maxPositions {
		slog.Warn("max positions reached", "max", l.maxPositions)
		return false
	}
	if sizeUsdc > l.cash {
		slog.Warn("insufficient cash for position", "need", sizeUsdc, "have", l.cash)
		return false
	}

	price := marketPrice(market, side)
	if price <= 0 {
		slog.Warn("invalid price for position", "market", market.ID, "side", side, "price", price)
		return false
	}

	shares := sizeUsdc / price

	l.cash -= sizeUsdc
	l.positions = append(l.positions, types.Position{
		MarketID:   market.ID,
		Question:   market.Question,
		Side:       side,
		EntryPrice: price,
		Shares:     shares,
		StakeUsdc:  sizeUsdc,
		IsLive:     false,
	})

	slog.Info("paper position opened",
		"market", market.ID, "side", side,
		"price", price, "shares", shares, "stake", sizeUsdc)
	return true
}

// ClosePaperPosition closes a paper position by market ID.
// Returns the realized P&L and true if a position was found and closed.
func (l *Ledger) ClosePaperPosition(marketID string, markets []types.MarketSnapshot) (float64, bool) {
	l.mu.Lock()
	defer l.mu.Unlock()

	for i, pos := range l.positions {
		if pos.MarketID == marketID {
			exitPrice := currentPrice(markets, pos.MarketID, pos.Side)
			pnl := pos.Shares*exitPrice - pos.StakeUsdc

			l.cash += pos.StakeUsdc + pnl
			l.realizedPnl += pnl
			if pnl < 0 {
				l.dailyLoss += -pnl
			}

			// Remove position.
			l.positions = append(l.positions[:i], l.positions[i+1:]...)

			slog.Info("paper position closed",
				"market", marketID, "pnl", pnl,
				"entry", pos.EntryPrice, "exit", exitPrice)
			return pnl, true
		}
	}
	return 0, false
}

// ResetDailyLoss resets the daily loss accumulator (called at day boundary).
func (l *Ledger) ResetDailyLoss() {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.dailyLoss = 0
}

// BudgetInfo returns a snapshot of the ledger state for the dashboard.
func (l *Ledger) BudgetInfo(markets []types.MarketSnapshot) types.BudgetInfo {
	l.mu.RLock()
	defer l.mu.RUnlock()
	return types.BudgetInfo{
		Cash:        l.cash,
		Reserve:     l.reserve,
		Equity:      l.equityLocked(markets),
		LlmSpend:    l.llmSpend,
		RealizedPnl: l.realizedPnl,
		DailyLoss:   l.dailyLoss,
	}
}

// currentPrice looks up the current price for a position's market and side.
func currentPrice(markets []types.MarketSnapshot, marketID, side string) float64 {
	m := types.FindMarket(markets, marketID)
	if m == nil {
		return 0
	}
	return marketPrice(*m, side)
}

// marketPrice returns the price for a given side of a market.
func marketPrice(m types.MarketSnapshot, side string) float64 {
	switch side {
	case "YES", "yes":
		return m.YesPrice
	case "NO", "no":
		return m.NoPrice
	default:
		return m.YesPrice
	}
}
