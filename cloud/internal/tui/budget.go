package tui

import (
	"fmt"
	"strings"

	"survaiv/internal/dashboard"
)

func renderBudget(t Theme, snap dashboard.Snapshot, width int) string {
	b := snap.Budget

	type card struct {
		label string
		value string
		sub   string
		pos   bool
		neg   bool
	}

	cards := []card{
		{label: "CASH", value: fmtUsd(b.Cash), sub: "USDC available"},
		{label: "EQUITY", value: fmtUsd(b.Equity), sub: "Cash + positions"},
		{
			label: "P&L",
			value: signedUsd(b.RealizedPnl),
			sub:   "Realized",
			pos:   b.RealizedPnl > 0,
			neg:   b.RealizedPnl < 0,
		},
		{label: "LLM SPEND", value: fmtUsd(b.LlmSpend), sub: "Inference cost"},
		{
			label: "DAILY LOSS",
			value: fmtUsd(b.DailyLoss),
			sub:   "Today's drawdown",
			neg:   b.DailyLoss > 0,
		},
		{
			label: "CYCLES",
			value: fmt.Sprintf("%d", snap.CycleCount),
		},
	}

	var parts []string
	for _, c := range cards {
		label := t.CardLabel.Render(c.label)
		var val string
		switch {
		case c.pos:
			val = t.CardPos.Render(c.value)
		case c.neg:
			val = t.CardNeg.Render(c.value)
		default:
			val = t.CardValue.Render(c.value)
		}
		sub := ""
		if c.sub != "" {
			sub = t.Dim.Render(c.sub)
		}
		parts = append(parts, fmt.Sprintf("  %s\n  %s\n  %s", label, val, sub))
	}

	// Lay out in a row.
	colWidth := width / len(parts)
	if colWidth < 16 {
		colWidth = 16
	}

	var lines [3]string
	for _, p := range parts {
		rows := strings.SplitN(p, "\n", 3)
		for i := 0; i < 3; i++ {
			row := ""
			if i < len(rows) {
				row = rows[i]
			}
			lines[i] += padRight(row, colWidth)
		}
	}

	return lines[0] + "\n" + lines[1] + "\n" + lines[2]
}

func fmtUsd(n float64) string {
	if n < 0 {
		return fmt.Sprintf("-$%.2f", -n)
	}
	return fmt.Sprintf("$%.2f", n)
}

func signedUsd(n float64) string {
	if n >= 0 {
		return fmt.Sprintf("+$%.2f", n)
	}
	return fmt.Sprintf("-$%.2f", -n)
}

func padRight(s string, w int) string {
	// Use visible length (rough — doesn't strip ANSI, but close enough for TUI).
	if len(s) >= w {
		return s
	}
	return s + strings.Repeat(" ", w-len(s))
}
