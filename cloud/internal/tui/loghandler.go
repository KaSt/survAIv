package tui

import (
	"context"
	"fmt"
	"log/slog"
	"sync"
	"time"
)

// LogEntry is a single captured log line for TUI display.
type LogEntry struct {
	Time    time.Time
	Level   slog.Level
	Message string
}

// RingHandler is a slog.Handler that captures log records into a ring buffer
// for display in the TUI, preventing raw log output from corrupting the
// alt-screen rendering.
type RingHandler struct {
	mu      sync.Mutex
	entries []LogEntry
	cap     int
}

// NewRingHandler creates a handler that keeps the last `cap` log entries.
func NewRingHandler(cap int) *RingHandler {
	return &RingHandler{cap: cap, entries: make([]LogEntry, 0, cap)}
}

func (h *RingHandler) Enabled(_ context.Context, _ slog.Level) bool { return true }

func (h *RingHandler) Handle(_ context.Context, r slog.Record) error {
	// Format attrs inline.
	attrs := ""
	r.Attrs(func(a slog.Attr) bool {
		attrs += fmt.Sprintf(" %s=%v", a.Key, a.Value)
		return true
	})

	h.mu.Lock()
	defer h.mu.Unlock()
	h.entries = append(h.entries, LogEntry{
		Time:    r.Time,
		Level:   r.Level,
		Message: r.Message + attrs,
	})
	if len(h.entries) > h.cap {
		h.entries = h.entries[len(h.entries)-h.cap:]
	}
	return nil
}

func (h *RingHandler) WithAttrs(_ []slog.Attr) slog.Handler  { return h }
func (h *RingHandler) WithGroup(_ string) slog.Handler        { return h }

// Entries returns a copy of the current log entries.
func (h *RingHandler) Entries() []LogEntry {
	h.mu.Lock()
	defer h.mu.Unlock()
	out := make([]LogEntry, len(h.entries))
	copy(out, h.entries)
	return out
}
