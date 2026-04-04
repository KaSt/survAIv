package polymarket

import (
	"context"
	"crypto/ecdsa"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"log/slog"
	"math/big"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"

	"survaiv/internal/crypto"
	"survaiv/internal/httpclient"

	"github.com/ethereum/go-ethereum/common"
)

// ClobClient handles Polymarket CLOB authentication and order management.
type ClobClient struct {
	mu         sync.Mutex
	baseURL    string
	client     *httpclient.Client
	key        *ecdsa.PrivateKey
	address    common.Address
	apiKey     string
	secret     string
	passphrase string
	ready      bool
}

// NewClobClient creates a new CLOB client.
func NewClobClient(baseURL string, client *httpclient.Client, key *ecdsa.PrivateKey) *ClobClient {
	return &ClobClient{
		baseURL: strings.TrimRight(baseURL, "/"),
		client:  client,
		key:     key,
		address: crypto.AddressFromKey(key),
	}
}

// Init performs L1 EIP-712 authentication to derive API credentials.
func (c *ClobClient) Init(ctx context.Context) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	address := strings.ToLower(c.address.Hex())
	ts := strconv.FormatInt(time.Now().Unix(), 10)

	// Random nonce.
	nonceBuf := make([]byte, 4)
	rand.Read(nonceBuf)
	nonce := strconv.FormatUint(uint64(nonceBuf[0])<<24|uint64(nonceBuf[1])<<16|uint64(nonceBuf[2])<<8|uint64(nonceBuf[3]), 10)

	message := "This message attests that I control the given wallet"

	domainSep := crypto.ClobDomainSeparator()
	structHash := crypto.HashClobAuth(address, ts, nonce, message)

	sig, err := crypto.Eip712Sign(c.key, domainSep, structHash)
	if err != nil {
		return fmt.Errorf("clob L1 sign: %w", err)
	}
	sigHex := "0x" + crypto.HexEncode(sig)

	// POST to /auth/derive-api-key.
	body := fmt.Sprintf(`{"message":"%s"}`, message)
	req, _ := http.NewRequestWithContext(ctx, http.MethodPost, c.baseURL+"/auth/derive-api-key", strings.NewReader(body))
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("POLY-ADDRESS", address)
	req.Header.Set("POLY-SIGNATURE", sigHex)
	req.Header.Set("POLY-TIMESTAMP", ts)
	req.Header.Set("POLY-NONCE", nonce)

	resp, err := c.client.DoRequest(req)
	if err != nil {
		return fmt.Errorf("clob L1 auth: %w", err)
	}
	if resp.StatusCode != 200 {
		return fmt.Errorf("clob L1 auth HTTP %d: %s", resp.StatusCode, string(resp.Body[:clobMin(len(resp.Body), 200)]))
	}

	var creds struct {
		ApiKey     string `json:"apiKey"`
		Secret     string `json:"secret"`
		Passphrase string `json:"passphrase"`
	}
	if err := json.Unmarshal(resp.Body, &creds); err != nil {
		return fmt.Errorf("clob parse creds: %w", err)
	}
	if creds.ApiKey == "" || creds.Secret == "" {
		return fmt.Errorf("clob L1 returned empty credentials")
	}

	c.apiKey = creds.ApiKey
	c.secret = creds.Secret
	c.passphrase = creds.Passphrase
	c.ready = true

	slog.Info("CLOB authenticated", "apiKey", c.apiKey[:8]+"...")
	return nil
}

// IsReady returns true if CLOB credentials are valid.
func (c *ClobClient) IsReady() bool {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.ready
}

// PlaceOrder places a limit order on the CLOB.
func (c *ClobClient) PlaceOrder(ctx context.Context, tokenID string, side int, price, size float64) (string, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if !c.ready {
		return "", fmt.Errorf("CLOB not authenticated")
	}

	// Build EIP-712 Order.
	order := crypto.OrderFields{}

	// Random salt.
	rand.Read(order.Salt[:])

	// Maker and signer = our address.
	copy(order.Maker[:], c.address.Bytes())
	copy(order.Signer[:], c.address.Bytes())
	// Taker = 0x0 (public).

	// Token ID — decimal string to big-endian uint256.
	tid := new(big.Int)
	tid.SetString(tokenID, 10)
	tidBytes := tid.Bytes()
	copy(order.TokenID[32-len(tidBytes):], tidBytes)

	// Amounts.
	var makerAmt, takerAmt uint64
	if side == 0 { // BUY
		makerAmt = uint64(size * price * 1e6)
		takerAmt = uint64(size * 1e6)
	} else { // SELL
		makerAmt = uint64(size * 1e6)
		takerAmt = uint64(size * price * 1e6)
	}
	order.MakerAmount = crypto.Uint64ToUint256(makerAmt)
	order.TakerAmount = crypto.Uint64ToUint256(takerAmt)

	// Random nonce.
	nonceBuf := make([]byte, 4)
	rand.Read(nonceBuf)
	copy(order.Nonce[28:], nonceBuf)

	order.Side = uint8(side)
	order.SignatureType = 0

	// Sign.
	domainSep := crypto.ClobDomainSeparator()
	orderHash := crypto.HashOrder(&order)
	sig, err := crypto.Eip712Sign(c.key, domainSep, orderHash)
	if err != nil {
		return "", fmt.Errorf("order sign: %w", err)
	}
	sigHex := "0x" + crypto.HexEncode(sig)

	// Build JSON payload.
	payload := map[string]interface{}{
		"order": map[string]interface{}{
			"salt":          uint256ToDecimal(order.Salt[:]),
			"maker":         c.address.Hex(),
			"signer":        c.address.Hex(),
			"taker":         "0x0000000000000000000000000000000000000000",
			"tokenId":       tokenID,
			"makerAmount":   strconv.FormatUint(makerAmt, 10),
			"takerAmount":   strconv.FormatUint(takerAmt, 10),
			"expiration":    "0",
			"nonce":         uint256ToDecimal(order.Nonce[:]),
			"feeRateBps":    "0",
			"side":          side,
			"signatureType": 0,
			"signature":     sigHex,
		},
		"owner":     c.address.Hex(),
		"orderType": "GTC",
	}

	resp, err := c.clobRequest(ctx, "POST", "/order", payload)
	if err != nil {
		return "", err
	}

	if resp.StatusCode == 401 {
		// Retry with fresh creds.
		c.ready = false
		c.mu.Unlock()
		c.Init(ctx)
		c.mu.Lock()
		resp, err = c.clobRequest(ctx, "POST", "/order", payload)
		if err != nil {
			return "", err
		}
	}

	if resp.StatusCode != 200 && resp.StatusCode != 201 {
		return "", fmt.Errorf("order failed HTTP %d: %s", resp.StatusCode, string(resp.Body[:clobMin(len(resp.Body), 300)]))
	}

	var result struct {
		OrderID string `json:"orderID"`
		ID      string `json:"id"`
	}
	json.Unmarshal(resp.Body, &result)
	orderID := result.OrderID
	if orderID == "" {
		orderID = result.ID
	}

	slog.Info("order placed", "id", orderID, "side", side, "price", price, "size", size)
	return orderID, nil
}

// CancelOrder cancels an order by ID.
func (c *ClobClient) CancelOrder(ctx context.Context, orderID string) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if !c.ready {
		return fmt.Errorf("CLOB not authenticated")
	}

	resp, err := c.clobRequest(ctx, "DELETE", "/order/"+orderID, nil)
	if err != nil {
		return err
	}
	if resp.StatusCode == 200 || resp.StatusCode == 204 {
		return nil
	}
	return fmt.Errorf("cancel failed HTTP %d", resp.StatusCode)
}

func (c *ClobClient) clobRequest(ctx context.Context, method, path string, body interface{}) (*httpclient.Response, error) {
	ts := strconv.FormatInt(time.Now().Unix(), 10)

	var bodyBytes []byte
	if body != nil {
		var err error
		bodyBytes, err = json.Marshal(body)
		if err != nil {
			return nil, err
		}
	}

	sig := c.l2Sign(ts, method, path, string(bodyBytes))

	req, _ := http.NewRequestWithContext(ctx, method, c.baseURL+path, strings.NewReader(string(bodyBytes)))
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("POLY-ADDRESS", strings.ToLower(c.address.Hex()))
	req.Header.Set("POLY-SIGNATURE", sig)
	req.Header.Set("POLY-TIMESTAMP", ts)
	req.Header.Set("POLY-PASSPHRASE", c.passphrase)
	req.Header.Set("POLY-API-KEY", c.apiKey)

	return c.client.DoRequest(req)
}

func (c *ClobClient) l2Sign(timestamp, method, path, body string) string {
	message := timestamp + method + path + body
	decodedSecret, _ := base64.StdEncoding.DecodeString(c.secret)
	mac := hmac.New(sha256.New, decodedSecret)
	mac.Write([]byte(message))
	return base64.StdEncoding.EncodeToString(mac.Sum(nil))
}

func uint256ToDecimal(b []byte) string {
	n := new(big.Int).SetBytes(b)
	return n.String()
}

func clobMin(a, b int) int {
	if a < b {
		return a
	}
	return b
}
