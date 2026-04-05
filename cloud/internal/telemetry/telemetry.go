package telemetry

import (
	"bytes"
	"encoding/json"
	"log/slog"
	"net/http"
	"strings"
	"sync"
	"time"
)

// StateProvider returns the full dashboard state for telemetry reporting.
type StateProvider interface {
	TelemetryReport() map[string]any
}

// Telemetry periodically POSTs a JSON report to a configurable hub URL.
// Disabled when URL is empty.
type Telemetry struct {
	mu       sync.RWMutex
	url      string
	interval time.Duration
	provider StateProvider
	stopCh   chan struct{}
	client   *http.Client
}

// New creates and starts the telemetry loop. If intervalSec < 60 it defaults to 300.
func New(url string, intervalSec int, provider StateProvider) *Telemetry {
	if intervalSec < 60 {
		intervalSec = 300
	}
	t := &Telemetry{
		url:      url,
		interval: time.Duration(intervalSec) * time.Second,
		provider: provider,
		stopCh:   make(chan struct{}),
		client:   &http.Client{Timeout: 10 * time.Second},
	}
	go t.loop()
	return t
}

func (t *Telemetry) SetURL(url string) {
	t.mu.Lock()
	t.url = url
	t.mu.Unlock()
}

func (t *Telemetry) SetInterval(sec int) {
	if sec < 60 {
		sec = 60
	}
	t.mu.Lock()
	t.interval = time.Duration(sec) * time.Second
	t.mu.Unlock()
}

func (t *Telemetry) URL() string {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.url
}

func (t *Telemetry) IntervalSec() int {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return int(t.interval.Seconds())
}

func (t *Telemetry) loop() {
	// Wait 30s after start before first report.
	select {
	case <-time.After(30 * time.Second):
	case <-t.stopCh:
		return
	}

	for {
		t.sendReport()

		t.mu.RLock()
		wait := t.interval
		t.mu.RUnlock()

		select {
		case <-time.After(wait):
		case <-t.stopCh:
			return
		}
	}
}

func (t *Telemetry) sendReport() {
	t.mu.RLock()
	url := t.url
	t.mu.RUnlock()

	if url == "" {
		return
	}

	url = strings.TrimRight(url, "/") + "/api/report"

	report := t.provider.TelemetryReport()
	body, err := json.Marshal(report)
	if err != nil {
		slog.Warn("telemetry: marshal error", "err", err)
		return
	}

	resp, err := t.client.Post(url, "application/json", bytes.NewReader(body))
	if err != nil {
		slog.Warn("telemetry: POST failed", "err", err)
		return
	}
	resp.Body.Close()

	if resp.StatusCode >= 200 && resp.StatusCode < 300 {
		slog.Info("telemetry: report sent", "bytes", len(body))
	} else {
		slog.Warn("telemetry: POST returned", "status", resp.StatusCode)
	}
}

// Stop shuts down the telemetry loop.
func (t *Telemetry) Stop() {
	close(t.stopCh)
}
