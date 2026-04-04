package tui

import (
	"fmt"
	"strings"
	"time"

	"survaiv/internal/dashboard"
)

func renderLog(t Theme, snap dashboard.Snapshot, scroll, maxHeight int) string {
	title := t.SectionTitle.Render("  ── DECISION LOG ──")

	if len(snap.Decisions) == 0 {
		return title + "\n" + t.Dim.Render("  Waiting for first cycle…")
	}

	maxVisible := maxHeight / 3
	if maxVisible < 5 {
		maxVisible = 5
	}
	if maxVisible > 15 {
		maxVisible = 15
	}

	start := scroll
	if start >= len(snap.Decisions) {
		start = len(snap.Decisions) - 1
	}
	if start < 0 {
		start = 0
	}

	end := start + maxVisible
	if end > len(snap.Decisions) {
		end = len(snap.Decisions)
	}

	var lines []string
	lines = append(lines, title)

	for _, d := range snap.Decisions[start:end] {
		ts := time.Unix(d.Epoch, 0).Format("15:04:05")
		timeStr := t.LogTime.Render(ts)

		var typeStr string
		switch {
		case strings.Contains(d.Type, "buy"):
			typeStr = t.LogBuy.Render(fmt.Sprintf("%-15s", d.Type))
		case strings.Contains(d.Type, "close"):
			typeStr = t.LogClose.Render(fmt.Sprintf("%-15s", d.Type))
		case d.Type == "tool_call":
			typeStr = t.LogTool.Render(fmt.Sprintf("%-15s", d.Type))
		default:
			typeStr = t.LogHold.Render(fmt.Sprintf("%-15s", d.Type))
		}

		q := ""
		if d.MarketQuestion != "" {
			q = truncate(d.MarketQuestion, 35)
		}

		conf := ""
		if d.Confidence > 0 {
			conf = fmt.Sprintf(" %.0f%%", d.Confidence*100)
		}

		line := fmt.Sprintf("  %s %s %s%s", timeStr, typeStr, q, conf)
		lines = append(lines, line)

		if d.Rationale != "" {
			rationale := truncate(d.Rationale, 80)
			lines = append(lines, "    "+t.Dim.Render(rationale))
		}
	}

	if end < len(snap.Decisions) {
		lines = append(lines, t.Dim.Render(fmt.Sprintf("  … %d more (j/k to scroll)", len(snap.Decisions)-end)))
	}

	return strings.Join(lines, "\n")
}
