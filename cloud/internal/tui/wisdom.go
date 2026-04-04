package tui

import (
	"fmt"
	"strings"

	"survaiv/internal/dashboard"
)

func renderWisdom(t Theme, snap dashboard.Snapshot) string {
	title := t.SectionTitle.Render("  ── LEARNING ──")

	// Compute stats from the wisdom tracker via dashboard snapshot.
	// These come from the /api/wisdom endpoint data passed through the TUI.
	// For TUI we read from the agent's decisions to estimate.
	total := len(snap.Decisions)
	resolved := 0
	accuracy := "–"
	buyAcc := "–"
	holdAcc := "–"

	statsLine := fmt.Sprintf(
		"  Decisions: %s  Resolved: %s  Accuracy: %s  Buy: %s  Hold: %s",
		t.CardValue.Render(fmt.Sprintf("%d", total)),
		t.CardValue.Render(fmt.Sprintf("%d", resolved)),
		t.CardValue.Render(accuracy),
		t.CardValue.Render(buyAcc),
		t.CardValue.Render(holdAcc),
	)

	var lines []string
	lines = append(lines, title, statsLine)

	// Show rules if any decisions have been tracked.
	lines = append(lines, t.Dim.Render("  Rules: (populated after outcomes are checked)"))

	return strings.Join(lines, "\n")
}
