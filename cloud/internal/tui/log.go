package tui

import (
	"fmt"

	"survaiv/internal/dashboard"

	"github.com/charmbracelet/lipgloss"
)

func renderLog(snap dashboard.StateSnapshot, width, scroll int) string {
	title := lipgloss.NewStyle().
		Bold(true).
		Foreground(currentTheme.accent).
		Render("📋 Decision Log")

	if len(snap.Decisions) == 0 {
		return lipgloss.NewStyle().Width(width).Padding(0, 1).
			Render(title + "\n" + lipgloss.NewStyle().Foreground(currentTheme.dim).Render("  No decisions yet"))
	}

	rows := title + "\n"
	maxShow := 6
	// Show newest first, with scroll offset.
	start := len(snap.Decisions) - 1 - scroll
	if start < 0 {
		start = 0
	}
	shown := 0
	for i := start; i >= 0 && shown < maxShow; i-- {
		d := snap.Decisions[i]
		typeStyle := lipgloss.NewStyle().Foreground(decisionColor(d.Type)).Bold(true)
		q := d.MarketQuestion
		if len(q) > 40 {
			q = q[:37] + "..."
		}
		rows += fmt.Sprintf("  %s %-40s conf:%.2f edge:%4.0f  %s\n",
			typeStyle.Render(fmt.Sprintf("%-12s", d.Type)), q, d.Confidence, d.EdgeBps,
			lipgloss.NewStyle().Foreground(currentTheme.dim).Render(d.Rationale))
		if len(rows) > 500 {
			break
		}
		shown++
	}

	return lipgloss.NewStyle().Width(width).Padding(0, 1).Render(rows)
}

func decisionColor(t string) lipgloss.Color {
	switch {
	case t == "hold":
		return currentTheme.yellow
	case len(t) > 4 && t[len(t)-3:] == "yes":
		return currentTheme.green
	case len(t) > 3 && t[len(t)-2:] == "no":
		return currentTheme.red
	default:
		return currentTheme.fg
	}
}
