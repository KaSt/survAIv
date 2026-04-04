package tui

import (
	"fmt"
	"strings"

	"survaiv/internal/dashboard"
)

func renderPositions(t Theme, snap dashboard.Snapshot) string {
	title := t.SectionTitle.Render("  ── OPEN POSITIONS ──")

	if len(snap.Positions) == 0 {
		return title + "\n" + t.Dim.Render("  No open positions")
	}

	header := fmt.Sprintf("  %-40s %-5s %-7s %-7s %-8s %-7s %-6s",
		"MARKET", "SIDE", "ENTRY", "CURR", "P&L", "STAKE", "TYPE")
	lines := []string{title, t.TableHeader.Render(header)}

	for _, p := range snap.Positions {
		q := truncate(p.Question, 38)
		side := strings.ToUpper(p.Side)
		entry := fmt.Sprintf("%.2f", p.EntryPrice)
		current := fmt.Sprintf("%.2f", p.EntryPrice) // Will be enriched with market data
		pnl := "$0.00"
		stake := fmtUsd(p.StakeUsdc)
		ptype := "PAPER"
		if p.IsLive {
			ptype = "LIVE"
		}

		// Enrich from market snapshots.
		for _, m := range snap.Positions {
			if m.MarketID == p.MarketID {
				break
			}
		}

		row := fmt.Sprintf("  %-40s %-5s %-7s %-7s %-8s %-7s %-6s",
			q, side, entry, current, pnl, stake, ptype)
		lines = append(lines, t.TableCell.Render(row))
	}

	return strings.Join(lines, "\n")
}

func truncate(s string, maxLen int) string {
	if len(s) <= maxLen {
		return s
	}
	if maxLen < 4 {
		return s[:maxLen]
	}
	return s[:maxLen-1] + "…"
}
