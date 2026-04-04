package tui

import (
	"fmt"

	"survaiv/internal/dashboard"

	"github.com/charmbracelet/lipgloss"
)

func renderMarkets(snap dashboard.StateSnapshot, width int) string {
	title := lipgloss.NewStyle().
		Bold(true).
		Foreground(currentTheme.accent).
		Render("🔍 Scouted Markets")

	if len(snap.Scouted) == 0 {
		return lipgloss.NewStyle().Width(width).Padding(0, 1).
			Render(title + "\n" + lipgloss.NewStyle().Foreground(currentTheme.dim).Render("  Waiting for first scan..."))
	}

	rows := title + "\n"
	maxShow := 8
	for i, s := range snap.Scouted {
		if i >= maxShow {
			rows += fmt.Sprintf("  ... and %d more\n", len(snap.Scouted)-maxShow)
			break
		}
		sig := signalStyle(s.Signal)
		q := s.Question
		if len(q) > 45 {
			q = q[:42] + "..."
		}
		rows += fmt.Sprintf("  %s %-45s  edge:%4.0f  conf:%.2f\n", sig, q, s.EdgeBps, s.Confidence)
	}

	return lipgloss.NewStyle().Width(width).Padding(0, 1).Render(rows)
}

func signalStyle(signal string) string {
	switch signal {
	case "bullish":
		return lipgloss.NewStyle().Foreground(currentTheme.green).Render("▲")
	case "bearish":
		return lipgloss.NewStyle().Foreground(currentTheme.red).Render("▼")
	case "skip":
		return lipgloss.NewStyle().Foreground(currentTheme.dim).Render("⊘")
	default:
		return lipgloss.NewStyle().Foreground(currentTheme.yellow).Render("◆")
	}
}
