package x402

import (
	"crypto/ecdsa"
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"log/slog"
	"strconv"
	"strings"
	"sync"
	"time"

	"survaiv/internal/crypto"

	"github.com/ethereum/go-ethereum/common"
)

// Payment handles x402 micropayment signatures on Base chain.
type Payment struct {
	mu         sync.Mutex
	key        *ecdsa.PrivateKey
	address    common.Address
	totalSpent float64
}

// New creates a new x402 payment handler.
func New(key *ecdsa.PrivateKey) *Payment {
	return &Payment{
		key:     key,
		address: crypto.AddressFromKey(key),
	}
}

// TotalSpentUsdc returns the total USDC spent on x402 payments.
func (p *Payment) TotalSpentUsdc() float64 {
	p.mu.Lock()
	defer p.mu.Unlock()
	return p.totalSpent
}

// MakePayment constructs an X-PAYMENT header value from a 402 response.
// Returns a base64-encoded JSON payment string.
func (p *Payment) MakePayment(statusCode int, body []byte, headers map[string]string) (string, error) {
	p.mu.Lock()
	defer p.mu.Unlock()

	// Parse 402 payment info.
	var root map[string]json.RawMessage
	json.Unmarshal(body, &root)

	var accepts []json.RawMessage
	if raw, ok := root["accepts"]; ok {
		json.Unmarshal(raw, &accepts)
	}

	version := 1
	if raw, ok := root["x402Version"]; ok {
		json.Unmarshal(raw, &version)
	}

	// Fallback: PAYMENT-REQUIRED header.
	if len(accepts) == 0 {
		headerVal := headers["payment-required"]
		if headerVal == "" {
			headerVal = headers["Payment-Required"]
		}
		if headerVal == "" {
			return "", fmt.Errorf("no payment info in 402 body or headers")
		}
		decoded, err := base64.StdEncoding.DecodeString(headerVal)
		if err != nil {
			return "", fmt.Errorf("decode payment header: %w", err)
		}
		json.Unmarshal(decoded, &root)
		if raw, ok := root["accepts"]; ok {
			json.Unmarshal(raw, &accepts)
		}
		if raw, ok := root["x402Version"]; ok {
			json.Unmarshal(raw, &version)
		}
	}

	if len(accepts) == 0 {
		return "", fmt.Errorf("no accepts in payment info")
	}

	// Find Base chain option (eip155:8453).
	var accept map[string]json.RawMessage
	for _, a := range accepts {
		var item map[string]json.RawMessage
		json.Unmarshal(a, &item)
		var network string
		json.Unmarshal(item["network"], &network)
		if network == "eip155:8453" {
			accept = item
			break
		}
	}
	if accept == nil {
		json.Unmarshal(accepts[0], &accept)
	}

	var amountStr, payToRaw string
	json.Unmarshal(accept["amount"], &amountStr)
	json.Unmarshal(accept["payTo"], &payToRaw)

	if amountStr == "" || payToRaw == "" {
		return "", fmt.Errorf("missing amount/payTo in 402")
	}

	timeoutSec := 300
	if raw, ok := accept["maxTimeoutSeconds"]; ok {
		json.Unmarshal(raw, &timeoutSec)
	}

	// USDC domain params.
	usdcName, usdcVer := "USD Coin", "2"
	if raw, ok := accept["extra"]; ok {
		var extra map[string]string
		json.Unmarshal(raw, &extra)
		if n, ok := extra["name"]; ok {
			usdcName = n
		}
		if v, ok := extra["version"]; ok {
			usdcVer = v
		}
	}

	// Parse payTo address.
	payToHex := strings.TrimPrefix(payToRaw, "0x")
	payToBytes, err := crypto.HexDecode(payToHex)
	if err != nil || len(payToBytes) != 20 {
		return "", fmt.Errorf("invalid payTo address")
	}
	var payTo [20]byte
	copy(payTo[:], payToBytes)

	// Build TransferWithAuthorization fields.
	amountAtomic, _ := strconv.ParseUint(amountStr, 10, 64)
	value := crypto.Uint64ToUint256(amountAtomic)
	validAfter := [32]byte{}

	vbVal := uint64(time.Now().Unix()) + uint64(timeoutSec)
	validBefore := crypto.Uint64ToUint256(vbVal)

	var nonce [32]byte
	rand.Read(nonce[:])

	// EIP-712 sign.
	domainSep := crypto.UsdcDomainSeparator(usdcName, usdcVer)
	var from [20]byte
	copy(from[:], p.address.Bytes())
	structHash := crypto.HashTransferAuth(from, payTo, value, validAfter, validBefore, nonce)

	sig, err := crypto.Eip712Sign(p.key, domainSep, structHash)
	if err != nil {
		return "", fmt.Errorf("x402 sign: %w", err)
	}

	// Track spending.
	spent := float64(amountAtomic) / 1e6
	p.totalSpent += spent
	slog.Info("x402 payment", "amount_usdc", spent, "total", p.totalSpent)

	// Build JSON payload.
	sigHex := "0x" + crypto.HexEncode(sig)
	fromHex := strings.ToLower(p.address.Hex())
	nonceHex := "0x" + crypto.HexEncode(nonce[:])
	vbStr := strconv.FormatUint(vbVal, 10)

	payment := map[string]interface{}{
		"x402Version": version,
		"payload": map[string]interface{}{
			"signature": sigHex,
			"authorization": map[string]interface{}{
				"from":        fromHex,
				"to":          payToRaw,
				"value":       amountStr,
				"validAfter":  "0",
				"validBefore": vbStr,
				"nonce":       nonceHex,
			},
		},
	}

	if version >= 2 {
		if resource, ok := root["resource"]; ok {
			payment["resource"] = json.RawMessage(resource)
		}
		payment["accepted"] = json.RawMessage(accepts[0]) // re-echo
	} else {
		var scheme, network string
		json.Unmarshal(accept["scheme"], &scheme)
		json.Unmarshal(accept["network"], &network)
		if scheme == "" {
			scheme = "exact"
		}
		if network == "" {
			network = "eip155:8453"
		}
		payment["scheme"] = scheme
		payment["network"] = network
	}

	payloadJSON, _ := json.Marshal(payment)
	return base64.StdEncoding.EncodeToString(payloadJSON), nil
}

// IsConfigured returns true if x402 is available (key is set).
func (p *Payment) IsConfigured() bool {
	return p != nil && p.key != nil
}
