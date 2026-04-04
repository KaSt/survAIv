package crypto

import (
	"crypto/ecdsa"
	"encoding/hex"
	"fmt"

	"github.com/ethereum/go-ethereum/common"
	ethcrypto "github.com/ethereum/go-ethereum/crypto"
)

// PrivateKeyFromHex parses a 64-char hex private key string (no 0x prefix).
func PrivateKeyFromHex(hexKey string) (*ecdsa.PrivateKey, error) {
	return ethcrypto.HexToECDSA(hexKey)
}

// AddressFromKey derives the Ethereum address from a private key.
func AddressFromKey(key *ecdsa.PrivateKey) common.Address {
	return ethcrypto.PubkeyToAddress(key.PublicKey)
}

// Sign signs a 32-byte hash with the private key and returns a 65-byte signature (r+s+v).
// The v value is 27 or 28 (EIP-155 unprotected).
func Sign(hash []byte, key *ecdsa.PrivateKey) ([]byte, error) {
	sig, err := ethcrypto.Sign(hash, key)
	if err != nil {
		return nil, fmt.Errorf("ecdsa sign: %w", err)
	}
	// go-ethereum returns v=0/1; Ethereum uses v=27/28.
	sig[64] += 27
	return sig, nil
}

// HexEncode encodes bytes as a lowercase hex string (no 0x prefix).
func HexEncode(data []byte) string {
	return hex.EncodeToString(data)
}

// HexDecode decodes a hex string (no 0x prefix) into bytes.
func HexDecode(s string) ([]byte, error) {
	return hex.DecodeString(s)
}
