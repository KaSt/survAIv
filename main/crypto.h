#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace survaiv {
namespace crypto {

// Keccak-256 hash (Ethereum-flavour, NOT NIST SHA3-256).
void Keccak256(const uint8_t *data, size_t len, uint8_t out[32]);

// secp256k1 ECDSA sign with recovery id (v = 27 or 28).
// Returns true on success; writes 65-byte signature (r[32] || s[32] || v[1]).
bool Secp256k1Sign(const uint8_t private_key[32], const uint8_t hash[32],
                   uint8_t signature[65]);

// Derive uncompressed public key (64 bytes, no 0x04 prefix) from private key.
bool Secp256k1PubKey(const uint8_t private_key[32], uint8_t pubkey[64]);

// Derive 20-byte Ethereum address from private key.
bool EthAddress(const uint8_t private_key[32], uint8_t address[20]);

// EIP-712 domain separator for Polymarket CLOB (ClobAuthDomain, v1, chainId=137).
void ClobDomainSeparator(uint8_t out[32]);

// EIP-712: hash the ClobAuth struct.
void HashClobAuth(const std::string &address, const std::string &timestamp,
                  const std::string &nonce, const std::string &message,
                  uint8_t out[32]);

// EIP-712: hash a Polymarket Order struct.
struct OrderFields {
  uint8_t salt[32];
  uint8_t maker[20];
  uint8_t signer[20];
  uint8_t taker[20];
  uint8_t token_id[32];
  uint8_t maker_amount[32];
  uint8_t taker_amount[32];
  uint8_t expiration[32];
  uint8_t nonce[32];
  uint8_t fee_rate_bps[32];
  uint8_t side;            // 0 = BUY, 1 = SELL
  uint8_t signature_type;  // 0 = EOA
};

void HashOrder(const OrderFields &order, uint8_t out[32]);

// Full EIP-712 sign: \x19\x01 || domainSep || structHash → Keccak → ECDSA.
bool Eip712Sign(const uint8_t private_key[32], const uint8_t domain_sep[32],
                const uint8_t struct_hash[32], uint8_t signature[65]);

// HMAC-SHA256 (for CLOB L2 auth).
void HmacSha256(const uint8_t *key, size_t key_len,
                const uint8_t *data, size_t data_len,
                uint8_t out[32]);

// Hex encode/decode utilities.
std::string HexEncode(const uint8_t *data, size_t len);
bool HexDecode(const std::string &hex, uint8_t *out, size_t out_len);

}  // namespace crypto
}  // namespace survaiv
