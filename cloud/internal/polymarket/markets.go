package polymarket

import (
	"context"
	"encoding/json"
	"fmt"
	"log/slog"
	"strconv"
	"strings"

	"survaiv/internal/httpclient"
	"survaiv/internal/types"
)

const marketsBaseURL = "https://gamma-api.polymarket.com/markets"

// FetchMarkets fetches active markets from the Gamma API.
func FetchMarkets(ctx context.Context, client *httpclient.Client, limit, offset int, order string) []types.MarketSnapshot {
	if order == "" {
		order = "volume24hr"
	}

	url := fmt.Sprintf("%s?active=true&closed=false&ascending=false&limit=%d&offset=%d&order=%s",
		marketsBaseURL, limit, offset, order)

	resp, err := client.Get(ctx, url)
	if err != nil || resp.StatusCode != 200 {
		slog.Warn("market fetch failed", "err", err, "status", func() int {
			if resp != nil {
				return resp.StatusCode
			}
			return 0
		}())
		return nil
	}

	var raw []struct {
		ID            string          `json:"id"`
		Question      string          `json:"question"`
		Description   string          `json:"description"`
		Slug          string          `json:"slug"`
		Category      string          `json:"category"`
		EndDate       string          `json:"endDate"`
		Liquidity     json.Number     `json:"liquidity"`
		Volume        json.Number     `json:"volume"`
		ClobTokenIds  json.RawMessage `json:"clobTokenIds"`
		OutcomePrices json.RawMessage `json:"outcomePrices"`
	}
	if err := json.Unmarshal(resp.Body, &raw); err != nil {
		slog.Warn("market parse failed", "err", err)
		return nil
	}

	var markets []types.MarketSnapshot
	for _, r := range raw {
		desc := r.Description
		const maxDescLen = 500
		if len(desc) > maxDescLen {
			desc = desc[:maxDescLen]
		}
		m := types.MarketSnapshot{
			ID:          r.ID,
			Question:    r.Question,
			Description: desc,
			Slug:        r.Slug,
			Category:    r.Category,
			EndDate:     r.EndDate,
		}
		m.Liquidity, _ = r.Liquidity.Float64()
		m.Volume, _ = r.Volume.Float64()

		// Parse clobTokenIds — can be array or stringified array.
		m.ClobTokenYes, m.ClobTokenNo = parseClobTokens(r.ClobTokenIds)

		// Parse outcomePrices — stringified array like "[\"0.55\",\"0.45\"]".
		prices := parseStringifiedPrices(r.OutcomePrices)
		if len(prices) >= 2 {
			m.YesPrice = prices[0]
			m.NoPrice = prices[1]
		}

		if m.ID == "" || m.Question == "" {
			continue
		}
		// Skip near-resolved markets.
		if m.YesPrice > 0.95 || m.YesPrice < 0.05 {
			continue
		}
		markets = append(markets, m)
		if len(markets) >= limit {
			break
		}
	}

	slog.Info("fetched markets", "count", len(markets), "limit", limit)
	return markets
}

func parseClobTokens(raw json.RawMessage) (yes, no string) {
	if len(raw) == 0 {
		return
	}
	// Try as array of strings.
	var arr []string
	if err := json.Unmarshal(raw, &arr); err == nil && len(arr) >= 2 {
		return arr[0], arr[1]
	}
	// Try as stringified array.
	var s string
	if err := json.Unmarshal(raw, &s); err == nil {
		if err2 := json.Unmarshal([]byte(s), &arr); err2 == nil && len(arr) >= 2 {
			return arr[0], arr[1]
		}
	}
	return
}

func parseStringifiedPrices(raw json.RawMessage) []float64 {
	if len(raw) == 0 {
		return nil
	}
	// Try as a string like "[\"0.55\",\"0.45\"]".
	var s string
	if err := json.Unmarshal(raw, &s); err == nil {
		s = strings.Trim(s, "[]")
		parts := strings.Split(s, ",")
		var prices []float64
		for _, p := range parts {
			p = strings.Trim(p, "\" ")
			v, err := strconv.ParseFloat(p, 64)
			if err == nil {
				prices = append(prices, v)
			}
		}
		return prices
	}
	// Try as array of strings.
	var arr []string
	if err := json.Unmarshal(raw, &arr); err == nil {
		var prices []float64
		for _, p := range arr {
			v, err := strconv.ParseFloat(p, 64)
			if err == nil {
				prices = append(prices, v)
			}
		}
		return prices
	}
	return nil
}
