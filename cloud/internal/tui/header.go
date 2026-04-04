package tui

import (
	"fmt"

	"survaiv/internal/dashboard"

	"github.com/charmbracelet/lipgloss"
)

func renderHeader(snap dashboard.StateSnapshot, width int) string {
	logo := lipgloss.NewStyle().
		Bold(true).
		Foreground(currentTheme.accent).
		Render("⟁ SURVAIV")

	status := lipgloss.NewStyle().
		Foreground(currentTheme.green).
		Render(fmt.Sprintf(" [%s]", snap.Status))

	model := lipgloss.NewStyle().
		Foreground(currentTheme.dim).
		Render(fmt.Sprintf(" model: %s", snap.Model))

	uptime := lipgloss.NewStyle().
		Foreground(currentTheme.dim).
		Render(fmt.Sprintf(" uptime: %s  cycles: %d", formatDuration(snap.Uptime), snap.Cycles))

	content := logo + status + model + uptime

	return lipgloss.NewStyle().
		Width(width).
		BorderBottom(true).
		BorderStyle(lipgloss.NormalBorder()).
		BorderForeground(currentTheme.border).
		Padding(0, 1).
		Render(content)
}

func formatDuration(seconds int64) string {
	h := seconds / 3600
	m := (seconds % 3600) / 60
	return fmt.Sprintf("%dh%02dm", h, m)
}
