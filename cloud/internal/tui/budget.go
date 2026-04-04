package tui

import (
	"fmt"

	"survaiv/internal/dashboard"

	"github.com/charmbracelet/lipgloss"
)

func renderBudget(snap dashboard.StateSnapshot, width int) string {
	items := []struct {
		label string
		value string
		color lipgloss.Color
	}{
		{"Cash", fmt.Sprintf("$%.2f", snap.Budget.Cash), currentTheme.fg},
		{"Equity", fmt.Sprintf("$%.2f", snap.Budget.Equity), currentTheme.accent},
		{"P&L", fmt.Sprintf("$%.4f", snap.Budget.RealizedPnl), pnlColor(snap.Budget.RealizedPnl)},
		{"LLM Spend", fmt.Sprintf("$%.4f", snap.Budget.LlmSpend), currentTheme.yellow},
		{"Daily Loss", fmt.Sprintf("$%.4f", snap.Budget.DailyLoss), currentTheme.red},
	}

	cellWidth := (width - 2) / len(items)
	cells := ""
	for _, item := range items {
		cell := lipgloss.NewStyle().
			Width(cellWidth).
			Align(lipgloss.Center).
			Render(
				lipgloss.NewStyle().Foreground(currentTheme.dim).Render(item.label) + "\n" +
					lipgloss.NewStyle().Bold(true).Foreground(item.color).Render(item.value))
		cells += cell
	}

	return lipgloss.NewStyle().
		Width(width).
		Padding(0, 1).
		Render(cells)
}

func pnlColor(v float64) lipgloss.Color {
	if v > 0 {
		return currentTheme.green
	}
	if v < 0 {
		return currentTheme.red
	}
	return currentTheme.fg
}
