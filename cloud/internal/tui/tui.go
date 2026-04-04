package tui

import (
	"time"

	tea "github.com/charmbracelet/bubbletea"

	"survaiv/internal/config"
	"survaiv/internal/dashboard"
)

// tickMsg triggers a TUI refresh.
type tickMsg time.Time

// Model is the main Bubbletea model.
type Model struct {
	dash      *dashboard.State
	cfg       *config.Config
	theme     Theme
	darkTheme bool
	width     int
	height    int
	logScroll int
}

// New creates a new TUI model.
func New(dash *dashboard.State, cfg *config.Config) Model {
	return Model{
		dash:      dash,
		cfg:       cfg,
		theme:     DarkTheme,
		darkTheme: true,
		width:     120,
		height:    40,
	}
}

// Init implements tea.Model.
func (m Model) Init() tea.Cmd {
	return tea.Batch(tickCmd(), tea.WindowSize())
}

// Update implements tea.Model.
func (m Model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "q", "ctrl+c":
			return m, tea.Quit
		case "t":
			m.darkTheme = !m.darkTheme
			if m.darkTheme {
				m.theme = DarkTheme
			} else {
				m.theme = LightTheme
			}
		case "j", "down":
			m.logScroll++
		case "k", "up":
			if m.logScroll > 0 {
				m.logScroll--
			}
		case "r":
			// Force refresh — just re-render.
		}

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height

	case tickMsg:
		return m, tickCmd()
	}

	return m, nil
}

// View implements tea.Model.
func (m Model) View() string {
	snap := m.dash.GetSnapshot()

	var s string
	s += renderHeader(m.theme, snap, m.cfg)
	s += "\n"
	s += renderBudget(m.theme, snap, m.width)
	s += "\n"
	s += renderPositions(m.theme, snap)
	s += "\n"
	s += renderMarkets(m.theme, snap)
	s += "\n"
	s += renderWisdom(m.theme, snap)
	s += "\n"
	s += renderLog(m.theme, snap, m.logScroll, m.height)
	s += "\n"
	s += m.theme.Dim.Render("  q=quit  t=theme  j/k=scroll  r=refresh")

	return s
}

func tickCmd() tea.Cmd {
	return tea.Tick(2*time.Second, func(t time.Time) tea.Msg {
		return tickMsg(t)
	})
}
