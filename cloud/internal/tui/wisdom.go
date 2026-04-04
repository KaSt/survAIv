package tui

import (
	"survaiv/internal/wisdom"

	"github.com/charmbracelet/lipgloss"
)

func renderWisdom(width int) string {
	title := lipgloss.NewStyle().
		Bold(true).
		Foreground(currentTheme.accent).
		Render("🧠 Wisdom")

	w := wisdom.GetWisdom()
	if w == "" {
		w = "Collecting data..."
	}

	content := lipgloss.NewStyle().
		Foreground(currentTheme.dim).
		Width(width - 4).
		Render(w)

	return lipgloss.NewStyle().Width(width).Padding(0, 1).Render(title + "\n" + content)
}
