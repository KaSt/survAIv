package wallet

import (
	"context"
	"encoding/json"
	"fmt"
	"log/slog"
	"math/big"
	"strings"

	"survaiv/internal/httpclient"
)

// USDC.e contract on Polygon.
const usdceAddress = "0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174"

// QueryUSDCBalance queries the on-chain USDC.e balance via JSON-RPC.
func (w *Wallet) QueryUSDCBalance(ctx context.Context, client *httpclient.Client, rpcURL string) (float64, error) {
	if !w.IsReady() {
		return -1, fmt.Errorf("wallet not ready")
	}

	addrHex := strings.TrimPrefix(w.AddressHex(), "0x")
	// balanceOf(address) selector = 0x70a08231
	data := "0x70a08231000000000000000000000000" + addrHex

	result, err := ethCall(ctx, client, rpcURL, usdceAddress, data)
	if err != nil {
		return -1, err
	}

	raw := parseHexBigInt(result)
	if raw == nil {
		return -1, fmt.Errorf("failed to parse balance result")
	}

	// USDC has 6 decimals.
	balance := new(big.Float).SetInt(raw)
	balance.Quo(balance, big.NewFloat(1e6))
	f, _ := balance.Float64()

	slog.Debug("USDC balance queried", "balance", f)
	return f, nil
}

func ethCall(ctx context.Context, client *httpclient.Client, rpcURL, to, data string) (string, error) {
	body := map[string]interface{}{
		"jsonrpc": "2.0",
		"method":  "eth_call",
		"params": []interface{}{
			map[string]string{"to": to, "data": data},
			"latest",
		},
		"id": 1,
	}

	resp, err := client.PostJSON(ctx, rpcURL, body, nil)
	if err != nil {
		return "", err
	}
	if resp.StatusCode != 200 {
		return "", fmt.Errorf("RPC call failed: HTTP %d", resp.StatusCode)
	}

	var rpcResp struct {
		Result string `json:"result"`
	}
	if err := json.Unmarshal(resp.Body, &rpcResp); err != nil {
		return "", err
	}
	return rpcResp.Result, nil
}

func parseHexBigInt(hex string) *big.Int {
	hex = strings.TrimPrefix(hex, "0x")
	n := new(big.Int)
	n.SetString(hex, 16)
	return n
}
