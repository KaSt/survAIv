package httpclient

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"math"
	"net/http"
	"time"
)

// Client wraps net/http with timeouts, retries, and JSON helpers.
type Client struct {
	http    *http.Client
	llmHTTP *http.Client
}

// New creates a Client with the default and LLM-specific timeouts.
func New() *Client {
	return &Client{
		http:    &http.Client{Timeout: 30 * time.Second},
		llmHTTP: &http.Client{Timeout: 120 * time.Second},
	}
}

// Response wraps an HTTP response for easy consumption.
type Response struct {
	StatusCode int
	Body       []byte
	Headers    http.Header
}

// Get performs a simple GET request.
func (c *Client) Get(ctx context.Context, url string) (*Response, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	return c.do(c.http, req)
}

// PostJSON performs a POST with a JSON body.
func (c *Client) PostJSON(ctx context.Context, url string, body interface{}, headers map[string]string) (*Response, error) {
	data, err := json.Marshal(body)
	if err != nil {
		return nil, fmt.Errorf("marshal: %w", err)
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, url, bytes.NewReader(data))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	return c.do(c.http, req)
}

// LLMPost performs a POST to an LLM endpoint with extended timeout and retry.
func (c *Client) LLMPost(ctx context.Context, url string, body []byte, headers map[string]string) (*Response, error) {
	const maxRetries = 3
	const baseDelay = 15 * time.Second

	var lastErr error
	for attempt := 0; attempt < maxRetries; attempt++ {
		if attempt > 0 {
			delay := baseDelay * time.Duration(math.Pow(2, float64(attempt-1)))
			slog.Warn("LLM retry", "attempt", attempt+1, "delay", delay)
			select {
			case <-ctx.Done():
				return nil, ctx.Err()
			case <-time.After(delay):
			}
		}

		req, err := http.NewRequestWithContext(ctx, http.MethodPost, url, bytes.NewReader(body))
		if err != nil {
			return nil, err
		}
		req.Header.Set("Content-Type", "application/json")
		for k, v := range headers {
			req.Header.Set(k, v)
		}

		resp, err := c.do(c.llmHTTP, req)
		if err != nil {
			lastErr = err
			continue
		}
		if resp.StatusCode >= 200 && resp.StatusCode < 300 {
			return resp, nil
		}
		// 402 is expected for x402 — return it so the caller can handle payment.
		if resp.StatusCode == 402 {
			return resp, nil
		}
		lastErr = fmt.Errorf("LLM HTTP %d: %s", resp.StatusCode, string(resp.Body[:min(len(resp.Body), 200)]))
	}
	return nil, fmt.Errorf("LLM unreachable after %d attempts: %w", maxRetries, lastErr)
}

// DoRequest performs a raw HTTP request.
func (c *Client) DoRequest(req *http.Request) (*Response, error) {
	return c.do(c.http, req)
}

func (c *Client) do(hc *http.Client, req *http.Request) (*Response, error) {
	resp, err := hc.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("read body: %w", err)
	}

	return &Response{
		StatusCode: resp.StatusCode,
		Body:       body,
		Headers:    resp.Header,
	}, nil
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
