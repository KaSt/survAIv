package tui

import (
	"fmt"
	"strings"

	"survaiv/internal/dashboard"
)

func renderMarkets(t Theme, snap dashboard.Snapshot) string {
	title := t.SectionTitle.Render("  ── MARKET SCANNER ──")

	if len(snap.ScoutedMarkets) == 0 {
		return title + "\n" + t.Dim.Render("  Waiting for first scan…")
	}

	var lines []string
	lines = append(lines, title)

	for _, m := range snap.ScoutedMarkets {
		var dot string
		switch m.Signal {
		case "bullish":
			dot = t.Bullish.Render("●")
		case "bearish":
			dot = t.Bearish.Render("●")
		case "neutral":
			dot = t.Neutral.Render("●")
		default:
			dot = t.Dim.Render("●")
		}

		q := truncate(m.Question, 50)
		edge := fmt.Sprintf("%.0fbp", m.EdgeBps)
		conf := fmt.Sprintf("%.0f%%", m.Confidence*100)
		price := "—"
		if m.YesPrice > 0 {
			price = fmt.Sprintf("%.0f¢", m.YesPrice*100)
		}

		vol := formatK(m.Volume)
		note := truncate(m.Note, 40)

		line := fmt.Sprintf("  %s %-50s %5s %4s  Vol:$%s  %s",
			dot, q, price, conf, vol, note)
		lines = append(lines, line)
		_ = edge // Available for extended view
	}

	return strings.Join(lines, "\n")
}

func formatK(n float64) string {
	if n >= 1000 {
		return fmt.Sprintf("%.0fk", n/1000)
	}
	return fmt.Sprintf("%.0f", n)
}
