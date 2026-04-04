package tui

import "github.com/charmbracelet/lipgloss"

// Theme colors
type theme struct {
	bg, fg, accent, green, red, yellow, dim, border lipgloss.Color
}

var darkTheme = theme{
	bg:     lipgloss.Color("#0d1117"),
	fg:     lipgloss.Color("#c9d1d9"),
	accent: lipgloss.Color("#58a6ff"),
	green:  lipgloss.Color("#3fb950"),
	red:    lipgloss.Color("#f85149"),
	yellow: lipgloss.Color("#d29922"),
	dim:    lipgloss.Color("#484f58"),
	border: lipgloss.Color("#30363d"),
}

var lightTheme = theme{
	bg:     lipgloss.Color("#ffffff"),
	fg:     lipgloss.Color("#24292f"),
	accent: lipgloss.Color("#0969da"),
	green:  lipgloss.Color("#1a7f37"),
	red:    lipgloss.Color("#cf222e"),
	yellow: lipgloss.Color("#9a6700"),
	dim:    lipgloss.Color("#6e7781"),
	border: lipgloss.Color("#d0d7de"),
}

var currentTheme = darkTheme

func toggleTheme() {
	if currentTheme.bg == darkTheme.bg {
		currentTheme = lightTheme
	} else {
		currentTheme = darkTheme
	}
}
