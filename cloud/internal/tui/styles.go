package tui

import "github.com/charmbracelet/lipgloss"

// Theme holds all the lipgloss styles for the TUI.
type Theme struct {
	Header      lipgloss.Style
	CardLabel   lipgloss.Style
	CardValue   lipgloss.Style
	CardPos     lipgloss.Style
	CardNeg     lipgloss.Style
	TableHeader lipgloss.Style
	TableCell   lipgloss.Style
	LogTime     lipgloss.Style
	LogHold     lipgloss.Style
	LogBuy      lipgloss.Style
	LogClose    lipgloss.Style
	LogTool     lipgloss.Style
	SectionTitle lipgloss.Style
	Dim         lipgloss.Style
	Bullish     lipgloss.Style
	Bearish     lipgloss.Style
	Neutral     lipgloss.Style
	StatusOk    lipgloss.Style
	StatusErr   lipgloss.Style
	Badge       lipgloss.Style
	Border      lipgloss.Style
}

// DarkTheme is the dark color scheme.
var DarkTheme = Theme{
	Header:      lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#4caf50")),
	CardLabel:   lipgloss.NewStyle().Foreground(lipgloss.Color("#777")),
	CardValue:   lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#e0e0e0")),
	CardPos:     lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#4caf50")),
	CardNeg:     lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#ef5350")),
	TableHeader: lipgloss.NewStyle().Foreground(lipgloss.Color("#777")),
	TableCell:   lipgloss.NewStyle().Foreground(lipgloss.Color("#e0e0e0")),
	LogTime:     lipgloss.NewStyle().Foreground(lipgloss.Color("#777")),
	LogHold:     lipgloss.NewStyle().Foreground(lipgloss.Color("#888")).Bold(true),
	LogBuy:      lipgloss.NewStyle().Foreground(lipgloss.Color("#4caf50")).Bold(true),
	LogClose:    lipgloss.NewStyle().Foreground(lipgloss.Color("#ef5350")).Bold(true),
	LogTool:     lipgloss.NewStyle().Foreground(lipgloss.Color("#42a5f5")).Bold(true),
	SectionTitle: lipgloss.NewStyle().Foreground(lipgloss.Color("#777")).Bold(true),
	Dim:         lipgloss.NewStyle().Foreground(lipgloss.Color("#555")),
	Bullish:     lipgloss.NewStyle().Foreground(lipgloss.Color("#4caf50")),
	Bearish:     lipgloss.NewStyle().Foreground(lipgloss.Color("#ef5350")),
	Neutral:     lipgloss.NewStyle().Foreground(lipgloss.Color("#ffca28")),
	StatusOk:    lipgloss.NewStyle().Foreground(lipgloss.Color("#4caf50")),
	StatusErr:   lipgloss.NewStyle().Foreground(lipgloss.Color("#ef5350")),
	Badge:       lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#ffca28")),
	Border:      lipgloss.NewStyle().BorderForeground(lipgloss.Color("#333")),
}

// LightTheme is the light color scheme.
var LightTheme = Theme{
	Header:      lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#0d9e50")),
	CardLabel:   lipgloss.NewStyle().Foreground(lipgloss.Color("#999")),
	CardValue:   lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#1a1a1a")),
	CardPos:     lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#0d9e50")),
	CardNeg:     lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#d32f2f")),
	TableHeader: lipgloss.NewStyle().Foreground(lipgloss.Color("#999")),
	TableCell:   lipgloss.NewStyle().Foreground(lipgloss.Color("#1a1a1a")),
	LogTime:     lipgloss.NewStyle().Foreground(lipgloss.Color("#999")),
	LogHold:     lipgloss.NewStyle().Foreground(lipgloss.Color("#888")).Bold(true),
	LogBuy:      lipgloss.NewStyle().Foreground(lipgloss.Color("#0d9e50")).Bold(true),
	LogClose:    lipgloss.NewStyle().Foreground(lipgloss.Color("#d32f2f")).Bold(true),
	LogTool:     lipgloss.NewStyle().Foreground(lipgloss.Color("#1565c0")).Bold(true),
	SectionTitle: lipgloss.NewStyle().Foreground(lipgloss.Color("#999")).Bold(true),
	Dim:         lipgloss.NewStyle().Foreground(lipgloss.Color("#aaa")),
	Bullish:     lipgloss.NewStyle().Foreground(lipgloss.Color("#0d9e50")),
	Bearish:     lipgloss.NewStyle().Foreground(lipgloss.Color("#d32f2f")),
	Neutral:     lipgloss.NewStyle().Foreground(lipgloss.Color("#e6a700")),
	StatusOk:    lipgloss.NewStyle().Foreground(lipgloss.Color("#0d9e50")),
	StatusErr:   lipgloss.NewStyle().Foreground(lipgloss.Color("#d32f2f")),
	Badge:       lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("#e6a700")),
	Border:      lipgloss.NewStyle().BorderForeground(lipgloss.Color("#e0e0e0")),
}
