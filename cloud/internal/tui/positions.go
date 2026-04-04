package tui

import (
	"fmt"

	"survaiv/internal/dashboard"

	"github.com/charmbracelet/lipgloss"
)

func renderPositions(snap dashboard.StateSnapshot, width int) string {
	title := lipgloss.NewStyle().
		Bold(true).
		Foreground(currentTheme.accent).
		Render("📊 Positions")

	if len(snap.Positions) == 0 {
		return lipgloss.NewStyle().Width(width).Padding(0, 1).
			Render(title + "\n" + lipgloss.NewStyle().Foreground(currentTheme.dim).Render("  No open positions"))
	}

	rows := title + "\n"
	for _, p := range snap.Positions {
		side := lipgloss.NewStyle().Foreground(currentTheme.green).Render("YES")
		if p.Side == "no" {
			side = lipgloss.NewStyle().Foreground(currentTheme.red).Render("NO")
		}
		q := p.Question
		if len(q) > 50 {
			q = q[:47] + "..."
		}
		live := ""
		if p.IsLive {
			live = " 🔴"
		}
		rows += fmt.Sprintf("  %s %s @ $%.2f ($%.2f)%s\n", side, q, p.EntryPrice, p.StakeUsdc, live)
	}

	return lipgloss.NewStyle().Width(width).Padding(0, 1).Render(rows)
}
