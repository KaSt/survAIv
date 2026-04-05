package ledger

import (
	"database/sql"
	"fmt"
	"log/slog"
	"strconv"
	"sync"

	"survaiv/internal/db"
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
	database     *sql.DB
}

// New creates a ledger with starting USDC, reserve amount, and max open positions.
// If a database handle is provided, state is restored from disk on startup and
// persisted after every mutation.
func New(startingUsdc, reserveUsdc float64, maxPositions int, database *sql.DB) *Ledger {
	if maxPositions <= 0 {
		maxPositions = 5
	}
	l := &Ledger{
		cash:         startingUsdc,
		reserve:      reserveUsdc,
		maxPositions: maxPositions,
		database:     database,
	}
	if database != nil {
		l.loadFromDB()
	}
	return l
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
// without dipping into the reserve.  Cash already reflects prior llmSpend
// (DebitInference subtracts from cash), so we only subtract the reserve.
func (l *Ledger) CanSpendOnInference(usdc float64, markets []types.MarketSnapshot) bool {
	l.mu.RLock()
	defer l.mu.RUnlock()
	return l.cash-l.reserve > usdc
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
	l.saveToDB()
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
	if sizeUsdc <= 0 {
		slog.Warn("zero or negative position size", "size", sizeUsdc)
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
	l.saveToDB()
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
			if exitPrice == 0 {
				slog.Warn("cannot close position: market not in snapshot", "market", marketID)
				return 0, false
			}
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
			l.saveToDB()
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
	l.saveToDB()
}

// ResetPaper resets the ledger to initial state with given bankroll.
func (l *Ledger) ResetPaper(startingUsdc, reserveUsdc float64) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.cash = startingUsdc
	l.reserve = reserveUsdc
	l.llmSpend = 0
	l.realizedPnl = 0
	l.dailyLoss = 0
	l.positions = nil
	l.saveToDB()
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

// saveToDB persists the full ledger state to the database.
// Must be called with l.mu held (write lock).
func (l *Ledger) saveToDB() {
	if l.database == nil {
		return
	}

	tx, err := l.database.Begin()
	if err != nil {
		slog.Error("ledger: failed to begin save tx", "err", err)
		return
	}
	defer tx.Rollback()

	// Clear and rewrite positions.
	if _, err := tx.Exec("DELETE FROM positions"); err != nil {
		slog.Error("ledger: failed to clear positions", "err", err)
		return
	}
	insertQ := db.Q("INSERT INTO positions (market_id, question, side, entry_price, shares, stake_usdc, is_live, order_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?)")
	for _, p := range l.positions {
		isLive := 0
		if p.IsLive {
			isLive = 1
		}
		if _, err := tx.Exec(insertQ, p.MarketID, p.Question, p.Side, p.EntryPrice, p.Shares, p.StakeUsdc, isLive, p.OrderID); err != nil {
			slog.Error("ledger: failed to insert position", "market", p.MarketID, "err", err)
			return
		}
	}

	// Upsert scalar values into config table.
	scalars := map[string]float64{
		"ledger_cash":         l.cash,
		"ledger_reserve":      l.reserve,
		"ledger_llm_spend":    l.llmSpend,
		"ledger_realized_pnl": l.realizedPnl,
		"ledger_daily_loss":   l.dailyLoss,
	}
	var upsertQ string
	if db.ActiveDriver == db.Postgres {
		upsertQ = "INSERT INTO config (key, value) VALUES ($1, $2) ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value"
	} else {
		upsertQ = "INSERT OR REPLACE INTO config (key, value) VALUES (?, ?)"
	}
	for k, v := range scalars {
		if _, err := tx.Exec(upsertQ, k, fmt.Sprintf("%.6f", v)); err != nil {
			slog.Error("ledger: failed to upsert config", "key", k, "err", err)
			return
		}
	}

	if err := tx.Commit(); err != nil {
		slog.Error("ledger: failed to commit save tx", "err", err)
		return
	}
}

// loadFromDB restores ledger state from the database.
// Called once from New() before any concurrent access.
func (l *Ledger) loadFromDB() {
	if l.database == nil {
		return
	}

	// Try to read ledger_cash to determine if saved state exists.
	var val string
	err := l.database.QueryRow(db.Q("SELECT value FROM config WHERE key = ?"), "ledger_cash").Scan(&val)
	if err != nil {
		slog.Info("starting fresh ledger (no saved state)")
		return
	}

	// Restore scalars.
	keys := map[string]*float64{
		"ledger_cash":         &l.cash,
		"ledger_reserve":      &l.reserve,
		"ledger_llm_spend":    &l.llmSpend,
		"ledger_realized_pnl": &l.realizedPnl,
		"ledger_daily_loss":   &l.dailyLoss,
	}
	for k, ptr := range keys {
		var s string
		if err := l.database.QueryRow(db.Q("SELECT value FROM config WHERE key = ?"), k).Scan(&s); err == nil {
			if f, err := strconv.ParseFloat(s, 64); err == nil {
				*ptr = f
			}
		}
	}

	// Restore positions.
	rows, err := l.database.Query("SELECT market_id, question, side, entry_price, shares, stake_usdc, is_live, order_id FROM positions")
	if err != nil {
		slog.Error("ledger: failed to load positions", "err", err)
		return
	}
	defer rows.Close()

	l.positions = nil
	for rows.Next() {
		var p types.Position
		var isLive int
		if err := rows.Scan(&p.MarketID, &p.Question, &p.Side, &p.EntryPrice, &p.Shares, &p.StakeUsdc, &isLive, &p.OrderID); err != nil {
			slog.Error("ledger: failed to scan position row", "err", err)
			continue
		}
		p.IsLive = isLive != 0
		l.positions = append(l.positions, p)
	}

	slog.Info("ledger restored from db",
		"cash", l.cash,
		"reserve", l.reserve,
		"llm_spend", l.llmSpend,
		"realized_pnl", l.realizedPnl,
		"daily_loss", l.dailyLoss,
		"positions", len(l.positions),
	)
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
