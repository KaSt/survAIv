package tui

import (
	"fmt"
	"log/slog"

	"github.com/charmbracelet/lipgloss"
)

func renderSysLog(ring *RingHandler, width int) string {
	title := lipgloss.NewStyle().
		Bold(true).
		Foreground(currentTheme.accent).
		Render("🔧 System Log")

	if ring == nil {
		return ""
	}

	entries := ring.Entries()
	if len(entries) == 0 {
		return lipgloss.NewStyle().Width(width).Padding(0, 1).
			Render(title + "\n" + lipgloss.NewStyle().Foreground(currentTheme.dim).Render("  (empty)"))
	}

	rows := title + "\n"
	// Show last 8 entries.
	start := len(entries) - 8
	if start < 0 {
		start = 0
	}
	for _, e := range entries[start:] {
		ts := e.Time.Format("15:04:05")
		lvl := levelTag(e.Level)
		msg := e.Message
		if len(msg) > 100 {
			msg = msg[:97] + "..."
		}
		rows += fmt.Sprintf("  %s %s %s\n", ts, lvl, msg)
	}

	return lipgloss.NewStyle().Width(width).Padding(0, 1).Render(rows)
}

func levelTag(l slog.Level) string {
	switch {
	case l >= slog.LevelError:
		return lipgloss.NewStyle().Foreground(currentTheme.red).Bold(true).Render("ERR")
	case l >= slog.LevelWarn:
		return lipgloss.NewStyle().Foreground(currentTheme.yellow).Bold(true).Render("WRN")
	default:
		return lipgloss.NewStyle().Foreground(currentTheme.dim).Render("INF")
	}
}
