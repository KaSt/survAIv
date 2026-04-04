package wallet

import (
	"crypto/ecdsa"
	"fmt"
	"log/slog"
	"strings"
	"sync"

	"survaiv/internal/crypto"

	"github.com/ethereum/go-ethereum/common"
)

// Wallet manages the Ethereum private key and derived address.
type Wallet struct {
	mu      sync.RWMutex
	key     *ecdsa.PrivateKey
	address common.Address
	ready   bool
}

// New creates a wallet from a hex private key (64 chars, no 0x prefix).
func New(hexKey string) (*Wallet, error) {
	hexKey = strings.TrimPrefix(hexKey, "0x")
	if len(hexKey) != 64 {
		return nil, fmt.Errorf("wallet key must be 64 hex chars, got %d", len(hexKey))
	}

	key, err := crypto.PrivateKeyFromHex(hexKey)
	if err != nil {
		return nil, fmt.Errorf("invalid wallet key: %w", err)
	}

	addr := crypto.AddressFromKey(key)
	slog.Info("wallet initialized", "address", addr.Hex())

	return &Wallet{
		key:     key,
		address: addr,
		ready:   true,
	}, nil
}

// PrivateKey returns the ECDSA private key.
func (w *Wallet) PrivateKey() *ecdsa.PrivateKey {
	w.mu.RLock()
	defer w.mu.RUnlock()
	return w.key
}

// Address returns the 20-byte Ethereum address.
func (w *Wallet) Address() common.Address {
	w.mu.RLock()
	defer w.mu.RUnlock()
	return w.address
}

// AddressHex returns the 0x-prefixed hex address.
func (w *Wallet) AddressHex() string {
	return strings.ToLower(w.Address().Hex())
}

// IsReady returns true if the wallet is initialized.
func (w *Wallet) IsReady() bool {
	w.mu.RLock()
	defer w.mu.RUnlock()
	return w.ready
}
