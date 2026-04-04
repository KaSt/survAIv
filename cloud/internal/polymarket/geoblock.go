package polymarket

import (
	"context"
	"encoding/json"
	"log/slog"

	"survaiv/internal/httpclient"
	"survaiv/internal/types"
)

const geoblockURL = "https://polymarket.com/api/geoblock"

// FetchGeoblockStatus checks Polymarket geo-restriction status.
func FetchGeoblockStatus(ctx context.Context, client *httpclient.Client) types.GeoblockStatus {
	status := types.GeoblockStatus{Blocked: true}

	resp, err := client.Get(ctx, geoblockURL)
	if err != nil || resp.StatusCode != 200 {
		slog.Warn("geoblock check failed, defaulting to blocked", "err", err)
		return status
	}

	var raw struct {
		Blocked bool   `json:"blocked"`
		Country string `json:"country"`
		Region  string `json:"region"`
		IP      string `json:"ip"`
	}
	if err := json.Unmarshal(resp.Body, &raw); err != nil {
		return status
	}

	status.Blocked = raw.Blocked
	status.Country = raw.Country
	status.Region = raw.Region
	status.IP = raw.IP
	return status
}
