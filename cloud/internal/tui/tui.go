package tui

import (
	"time"

	"survaiv/internal/dashboard"

	tea "github.com/charmbracelet/bubbletea"
)

type refreshMsg struct{}

type Model struct {
	state     *dashboard.State
	width     int
	height    int
	logScroll int
	ready     bool
}

func NewModel(state *dashboard.State) Model {
	return Model{state: state}
}

func (m Model) Init() tea.Cmd {
	return tea.Batch(
		tea.EnterAltScreen,
		tickRefresh(),
	)
}

func (m Model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "q", "ctrl+c":
			return m, tea.Quit
		case "t":
			toggleTheme()
		case "j", "down":
			m.logScroll++
		case "k", "up":
			if m.logScroll > 0 {
				m.logScroll--
			}
		}
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		m.ready = true
	case refreshMsg:
		return m, tickRefresh()
	}
	return m, nil
}

func (m Model) View() string {
	if !m.ready {
		return "Loading..."
	}

	snap := m.state.Snapshot()

	header := renderHeader(snap, m.width)
	budget := renderBudget(snap, m.width)
	positions := renderPositions(snap, m.width)
	markets := renderMarkets(snap, m.width)
	log := renderLog(snap, m.width, m.logScroll)
	wisdomPanel := renderWisdom(m.width)

	return header + "\n" + budget + "\n" + positions + "\n" + markets + "\n" + log + "\n" + wisdomPanel
}

func tickRefresh() tea.Cmd {
	return tea.Tick(time.Second*2, func(t time.Time) tea.Msg {
		return refreshMsg{}
	})
}
