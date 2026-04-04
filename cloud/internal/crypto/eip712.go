package crypto

import (
	"crypto/ecdsa"
	"encoding/binary"
	"math/big"
)

// ── EIP-712 Helpers ─────────────────────────────────────────────

// Eip712Digest computes keccak256("\x19\x01" || domainSep || structHash).
func Eip712Digest(domainSep, structHash [32]byte) []byte {
	payload := make([]byte, 66)
	payload[0] = 0x19
	payload[1] = 0x01
	copy(payload[2:34], domainSep[:])
	copy(payload[34:66], structHash[:])
	return Keccak256(payload)
}

// Eip712Sign signs an EIP-712 typed data hash and returns a 65-byte signature.
func Eip712Sign(key *ecdsa.PrivateKey, domainSep, structHash [32]byte) ([]byte, error) {
	digest := Eip712Digest(domainSep, structHash)
	return Sign(digest, key)
}

// ── CLOB Domain Separator ───────────────────────────────────────

// ClobDomainSeparator computes the EIP-712 domain separator for Polymarket CLOB.
// Domain: { name: "ClobAuthDomain", version: "1", chainId: 137 }
func ClobDomainSeparator() [32]byte {
	typeHash := Keccak256Hash([]byte("EIP712Domain(string name,string version,uint256 chainId)"))
	nameHash := Keccak256Hash([]byte("ClobAuthDomain"))
	versionHash := Keccak256Hash([]byte("1"))

	chainID := make([]byte, 32)
	chainID[31] = 137

	encoded := make([]byte, 128)
	copy(encoded[0:32], typeHash[:])
	copy(encoded[32:64], nameHash[:])
	copy(encoded[64:96], versionHash[:])
	copy(encoded[96:128], chainID)

	return Keccak256Hash(encoded)
}

// HashClobAuth computes the EIP-712 struct hash for ClobAuth.
func HashClobAuth(address, timestamp, nonce, message string) [32]byte {
	typeHash := Keccak256Hash([]byte(
		"ClobAuth(address address,string timestamp,uint256 nonce,string message)"))

	timestampHash := Keccak256Hash([]byte(timestamp))
	messageHash := Keccak256Hash([]byte(message))

	// Encode address (left-padded to 32 bytes).
	addrEncoded := make([]byte, 32)
	if len(address) >= 2 && address[:2] == "0x" {
		addrBytes, _ := HexDecode(address[2:])
		if len(addrBytes) == 20 {
			copy(addrEncoded[12:], addrBytes)
		}
	}

	// Encode nonce as uint256.
	nonceEncoded := make([]byte, 32)
	n := new(big.Int)
	n.SetString(nonce, 10)
	nBytes := n.Bytes()
	copy(nonceEncoded[32-len(nBytes):], nBytes)

	encoded := make([]byte, 160)
	copy(encoded[0:32], typeHash[:])
	copy(encoded[32:64], addrEncoded)
	copy(encoded[64:96], timestampHash[:])
	copy(encoded[96:128], nonceEncoded)
	copy(encoded[128:160], messageHash[:])

	return Keccak256Hash(encoded)
}

// OrderFields represents the fields of a Polymarket CLOB order for EIP-712 signing.
type OrderFields struct {
	Salt          [32]byte
	Maker         [20]byte
	Signer        [20]byte
	Taker         [20]byte
	TokenID       [32]byte
	MakerAmount   [32]byte
	TakerAmount   [32]byte
	Expiration    [32]byte
	Nonce         [32]byte
	FeeRateBps    [32]byte
	Side          uint8 // 0 = BUY, 1 = SELL
	SignatureType uint8 // 0 = EOA
}

// HashOrder computes the EIP-712 struct hash for a Polymarket Order.
func HashOrder(order *OrderFields) [32]byte {
	typeHash := Keccak256Hash([]byte(
		"Order(uint256 salt,address maker,address signer,address taker," +
			"uint256 tokenId,uint256 makerAmount,uint256 takerAmount,uint256 expiration," +
			"uint256 nonce,uint256 feeRateBps,uint8 side,uint8 signatureType)"))

	abiAddr := func(addr [20]byte) []byte {
		out := make([]byte, 32)
		copy(out[12:], addr[:])
		return out
	}
	abiUint8 := func(v uint8) []byte {
		out := make([]byte, 32)
		out[31] = v
		return out
	}

	encoded := make([]byte, 13*32)
	copy(encoded[0*32:], typeHash[:])
	copy(encoded[1*32:], order.Salt[:])
	copy(encoded[2*32:], abiAddr(order.Maker))
	copy(encoded[3*32:], abiAddr(order.Signer))
	copy(encoded[4*32:], abiAddr(order.Taker))
	copy(encoded[5*32:], order.TokenID[:])
	copy(encoded[6*32:], order.MakerAmount[:])
	copy(encoded[7*32:], order.TakerAmount[:])
	copy(encoded[8*32:], order.Expiration[:])
	copy(encoded[9*32:], order.Nonce[:])
	copy(encoded[10*32:], order.FeeRateBps[:])
	copy(encoded[11*32:], abiUint8(order.Side))
	copy(encoded[12*32:], abiUint8(order.SignatureType))

	return Keccak256Hash(encoded)
}

// ── x402 USDC Domain Separator (Base chain) ─────────────────────

// UsdcDomainSeparator computes the EIP-712 domain separator for USDC on Base (chainId 8453).
func UsdcDomainSeparator(name, version string) [32]byte {
	typeHash := Keccak256Hash([]byte(
		"EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)"))

	nameHash := Keccak256Hash([]byte(name))
	versionHash := Keccak256Hash([]byte(version))

	chainID := make([]byte, 32)
	binary.BigEndian.PutUint64(chainID[24:], 8453)

	// USDC on Base: 0x833589fCD6eDb6E08f4c7C32D4f71b54bdA02913
	contract := make([]byte, 32)
	addr, _ := HexDecode("833589fCD6eDb6E08f4c7C32D4f71b54bdA02913")
	copy(contract[12:], addr)

	encoded := make([]byte, 160)
	copy(encoded[0:32], typeHash[:])
	copy(encoded[32:64], nameHash[:])
	copy(encoded[64:96], versionHash[:])
	copy(encoded[96:128], chainID)
	copy(encoded[128:160], contract)

	return Keccak256Hash(encoded)
}

// HashTransferAuth computes the struct hash for EIP-3009 TransferWithAuthorization.
func HashTransferAuth(from, to [20]byte, value, validAfter, validBefore, nonce [32]byte) [32]byte {
	typeHash := Keccak256Hash([]byte(
		"TransferWithAuthorization(address from,address to,uint256 value," +
			"uint256 validAfter,uint256 validBefore,bytes32 nonce)"))

	abiAddr := func(addr [20]byte) []byte {
		out := make([]byte, 32)
		copy(out[12:], addr[:])
		return out
	}

	encoded := make([]byte, 7*32)
	copy(encoded[0*32:], typeHash[:])
	copy(encoded[1*32:], abiAddr(from))
	copy(encoded[2*32:], abiAddr(to))
	copy(encoded[3*32:], value[:])
	copy(encoded[4*32:], validAfter[:])
	copy(encoded[5*32:], validBefore[:])
	copy(encoded[6*32:], nonce[:])

	return Keccak256Hash(encoded)
}

// Uint64ToUint256 encodes a uint64 as a big-endian 32-byte uint256.
func Uint64ToUint256(val uint64) [32]byte {
	var out [32]byte
	binary.BigEndian.PutUint64(out[24:], val)
	return out
}
