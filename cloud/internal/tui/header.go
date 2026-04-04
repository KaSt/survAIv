package tui

import (
	"fmt"

	"survaiv/internal/config"
	"survaiv/internal/dashboard"
)

func renderHeader(t Theme, snap dashboard.Snapshot, cfg *config.Config) string {
	mode := t.Badge.Render("[PAPER]")
	if snap.LiveMode {
		mode = t.StatusOk.Render("[LIVE]")
	}

	statusDot := t.StatusOk.Render("●")
	statusText := snap.Status
	if snap.Status == "error" || snap.Status == "llm_error" {
		statusDot = t.StatusErr.Render("●")
	}

	h := int(snap.UptimeSeconds / 3600)
	m := int(snap.UptimeSeconds%3600) / 60

	header := fmt.Sprintf("  %s  %s  %s %s  Uptime: %dh %dm",
		t.Header.Render("⟁ SURVAIV"),
		mode,
		statusDot,
		statusText,
		h, m)

	if snap.LastError != "" {
		header += "\n  " + t.StatusErr.Render("⚠ "+snap.LastError)
	}

	return header
}
