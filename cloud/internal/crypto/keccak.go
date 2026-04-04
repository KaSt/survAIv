package crypto

import (
	"github.com/ethereum/go-ethereum/crypto"
)

// Keccak256 computes the Ethereum-flavour Keccak-256 hash.
func Keccak256(data []byte) []byte {
	return crypto.Keccak256(data)
}

// Keccak256Hash computes the Keccak-256 hash and returns it as a fixed-size array.
func Keccak256Hash(data []byte) [32]byte {
	var out [32]byte
	copy(out[:], crypto.Keccak256(data))
	return out
}
