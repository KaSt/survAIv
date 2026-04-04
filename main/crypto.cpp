#include "crypto.h"

#include <algorithm>
#include <cstring>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/md.h"

namespace survaiv {
namespace crypto {

// ---------------------------------------------------------------------------
// Keccak-256 (Ethereum-flavour).
//
// This is the "original" Keccak with 0x01 padding, NOT the NIST SHA3 variant
// which uses 0x06.  Ethereum and all EIP-712 hashing depend on this.
// Adapted from the Keccak reference implementation (public domain).
// ---------------------------------------------------------------------------

namespace {

constexpr int kKeccakRounds = 24;

constexpr uint64_t kKeccakRC[kKeccakRounds] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808AULL,
    0x8000000080008000ULL, 0x000000000000808BULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008AULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000AULL,
    0x000000008000808BULL, 0x800000000000008BULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800AULL, 0x800000008000000AULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL,
};

constexpr int kKeccakRotations[25] = {
    0,  1,  62, 28, 27, 36, 44, 6,  55, 20, 3,  10, 43,
    25, 39, 41, 45, 15, 21, 8,  18, 2,  61, 56, 14,
};

constexpr int kKeccakPi[25] = {
    0, 10, 20, 5, 15, 16, 1, 11, 21, 6, 7, 17, 2, 12, 22, 23, 8, 18, 3, 13, 14, 24, 9, 19, 4,
};

inline uint64_t Rotl64(uint64_t x, int n) {
  return (x << n) | (x >> (64 - n));
}

void KeccakF1600(uint64_t state[25]) {
  for (int round = 0; round < kKeccakRounds; ++round) {
    // Theta.
    uint64_t c[5];
    for (int x = 0; x < 5; ++x) {
      c[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
    }
    uint64_t d[5];
    for (int x = 0; x < 5; ++x) {
      d[x] = c[(x + 4) % 5] ^ Rotl64(c[(x + 1) % 5], 1);
    }
    for (int i = 0; i < 25; ++i) {
      state[i] ^= d[i % 5];
    }

    // Rho + Pi.
    uint64_t tmp[25];
    for (int i = 0; i < 25; ++i) {
      tmp[kKeccakPi[i]] = Rotl64(state[i], kKeccakRotations[i]);
    }

    // Chi.
    for (int y = 0; y < 25; y += 5) {
      for (int x = 0; x < 5; ++x) {
        state[y + x] = tmp[y + x] ^ (~tmp[y + (x + 1) % 5] & tmp[y + (x + 2) % 5]);
      }
    }

    // Iota.
    state[0] ^= kKeccakRC[round];
  }
}

}  // namespace

void Keccak256(const uint8_t *data, size_t len, uint8_t out[32]) {
  constexpr size_t kRate = 136;  // (1600 - 2*256) / 8
  uint64_t state[25] = {};

  // Absorb.
  size_t offset = 0;
  while (offset + kRate <= len) {
    for (size_t i = 0; i < kRate / 8; ++i) {
      uint64_t lane;
      std::memcpy(&lane, data + offset + i * 8, 8);
      state[i] ^= lane;
    }
    KeccakF1600(state);
    offset += kRate;
  }

  // Pad last block (Keccak padding: 0x01 ... 0x80).
  uint8_t block[kRate] = {};
  size_t remaining = len - offset;
  std::memcpy(block, data + offset, remaining);
  block[remaining] = 0x01;
  block[kRate - 1] |= 0x80;

  for (size_t i = 0; i < kRate / 8; ++i) {
    uint64_t lane;
    std::memcpy(&lane, block + i * 8, 8);
    state[i] ^= lane;
  }
  KeccakF1600(state);

  // Squeeze 32 bytes.
  std::memcpy(out, state, 32);
}

// ---------------------------------------------------------------------------
// secp256k1 ECDSA.
// ---------------------------------------------------------------------------

bool Secp256k1PubKey(const uint8_t private_key[32], uint8_t pubkey[64]) {
  mbedtls_ecp_group grp;
  mbedtls_mpi d;
  mbedtls_ecp_point Q;
  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_ecp_point_init(&Q);

  bool ok = false;
  int ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1);
  if (ret != 0) goto done;

  ret = mbedtls_mpi_read_binary(&d, private_key, 32);
  if (ret != 0) goto done;

  ret = mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, nullptr, nullptr);
  if (ret != 0) goto done;

  {
    uint8_t uncompressed[65];
    size_t olen = 0;
    ret = mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                         &olen, uncompressed, sizeof(uncompressed));
    if (ret != 0 || olen != 65) goto done;
    std::memcpy(pubkey, uncompressed + 1, 64);
  }
  ok = true;

done:
  mbedtls_ecp_point_free(&Q);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_group_free(&grp);
  return ok;
}

bool EthAddress(const uint8_t private_key[32], uint8_t address[20]) {
  uint8_t pubkey[64];
  if (!Secp256k1PubKey(private_key, pubkey)) {
    return false;
  }
  uint8_t hash[32];
  Keccak256(pubkey, 64, hash);
  std::memcpy(address, hash + 12, 20);
  return true;
}

// Recover public key from ECDSA signature: Q = r^{-1} * (s*R - hash*G).
// R is the curve point with x=r and y chosen by recovery_id parity.
static bool EcRecover(mbedtls_ecp_group *grp, const mbedtls_mpi *r,
                      const mbedtls_mpi *s, const uint8_t hash[32],
                      int recovery_id, uint8_t pubkey_out[64]) {
  bool ok = false;

  mbedtls_mpi y2, tmp, seven, exp, y_sqrt, r_inv, hash_mpi, neg_hash;
  mbedtls_ecp_point R_pt, result;

  mbedtls_mpi_init(&y2);
  mbedtls_mpi_init(&tmp);
  mbedtls_mpi_init(&seven);
  mbedtls_mpi_init(&exp);
  mbedtls_mpi_init(&y_sqrt);
  mbedtls_mpi_init(&r_inv);
  mbedtls_mpi_init(&hash_mpi);
  mbedtls_mpi_init(&neg_hash);
  mbedtls_ecp_point_init(&R_pt);
  mbedtls_ecp_point_init(&result);

  // Compute y from x=r on secp256k1: y^2 = x^3 + 7 (mod p).
  mbedtls_mpi_mul_mpi(&tmp, r, r);
  mbedtls_mpi_mod_mpi(&tmp, &tmp, &grp->P);
  mbedtls_mpi_mul_mpi(&y2, &tmp, r);
  mbedtls_mpi_mod_mpi(&y2, &y2, &grp->P);
  mbedtls_mpi_lset(&seven, 7);
  mbedtls_mpi_add_mpi(&y2, &y2, &seven);
  mbedtls_mpi_mod_mpi(&y2, &y2, &grp->P);

  // sqrt via y = y2^((p+1)/4) mod p  (works because p ≡ 3 mod 4).
  mbedtls_mpi_copy(&exp, &grp->P);
  mbedtls_mpi_add_int(&exp, &exp, 1);
  mbedtls_mpi_shift_r(&exp, 2);
  if (mbedtls_mpi_exp_mod(&y_sqrt, &y2, &exp, &grp->P, nullptr) != 0) goto done;

  // Choose y parity based on recovery_id.
  if ((mbedtls_mpi_get_bit(&y_sqrt, 0) != 0) != (recovery_id != 0)) {
    mbedtls_mpi_sub_mpi(&y_sqrt, &grp->P, &y_sqrt);
  }

  // R = (r, y_sqrt) — construct as uncompressed point and parse.
  {
    uint8_t r_pt_buf[65];
    r_pt_buf[0] = 0x04;
    mbedtls_mpi_write_binary(r, r_pt_buf + 1, 32);
    mbedtls_mpi_write_binary(&y_sqrt, r_pt_buf + 33, 32);
    if (mbedtls_ecp_point_read_binary(grp, &R_pt, r_pt_buf, 65) != 0) goto done;
  }

  // r_inv = r^(-1) mod N.
  if (mbedtls_mpi_inv_mod(&r_inv, r, &grp->N) != 0) goto done;

  // neg_hash = N - hash (so that -hash*G = neg_hash*G).
  mbedtls_mpi_read_binary(&hash_mpi, hash, 32);
  mbedtls_mpi_sub_mpi(&neg_hash, &grp->N, &hash_mpi);

  // result = r_inv * (s*R + neg_hash*G) via Shamir's trick (muladd).
  {
    mbedtls_ecp_point sum;
    mbedtls_ecp_point_init(&sum);
    if (mbedtls_ecp_muladd(grp, &sum, s, &R_pt, &neg_hash, &grp->G) != 0) {
      mbedtls_ecp_point_free(&sum);
      goto done;
    }
    if (mbedtls_ecp_mul(grp, &result, &r_inv, &sum, nullptr, nullptr) != 0) {
      mbedtls_ecp_point_free(&sum);
      goto done;
    }
    mbedtls_ecp_point_free(&sum);
  }

  // Write recovered public key (uncompressed, strip 0x04 prefix).
  {
    uint8_t buf[65];
    size_t olen = 0;
    if (mbedtls_ecp_point_write_binary(grp, &result, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                       &olen, buf, sizeof(buf)) != 0 || olen != 65) {
      goto done;
    }
    std::memcpy(pubkey_out, buf + 1, 64);
  }
  ok = true;

done:
  mbedtls_mpi_free(&y2);
  mbedtls_mpi_free(&tmp);
  mbedtls_mpi_free(&seven);
  mbedtls_mpi_free(&exp);
  mbedtls_mpi_free(&y_sqrt);
  mbedtls_mpi_free(&r_inv);
  mbedtls_mpi_free(&hash_mpi);
  mbedtls_mpi_free(&neg_hash);
  mbedtls_ecp_point_free(&R_pt);
  mbedtls_ecp_point_free(&result);
  return ok;
}

bool Secp256k1Sign(const uint8_t private_key[32], const uint8_t hash[32],
                   uint8_t signature[65]) {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_ecp_group grp;
  mbedtls_mpi d, r, s;

  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&d);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);

  bool ok = false;
  int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, nullptr, 0);
  if (ret != 0) goto cleanup;

  ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256K1);
  if (ret != 0) goto cleanup;

  ret = mbedtls_mpi_read_binary(&d, private_key, 32);
  if (ret != 0) goto cleanup;

  ret = mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, 32,
                           mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret != 0) goto cleanup;

  // Enforce low-S (EIP-2): if s > N/2, replace with N - s.
  {
    mbedtls_mpi half_n;
    mbedtls_mpi_init(&half_n);
    mbedtls_mpi_copy(&half_n, &grp.N);
    mbedtls_mpi_shift_r(&half_n, 1);
    if (mbedtls_mpi_cmp_mpi(&s, &half_n) > 0) {
      mbedtls_mpi_sub_mpi(&s, &grp.N, &s);
    }
    mbedtls_mpi_free(&half_n);
  }

  // Write r || s (32 bytes each, big-endian).
  std::memset(signature, 0, 65);
  mbedtls_mpi_write_binary(&r, signature, 32);
  mbedtls_mpi_write_binary(&s, signature + 32, 32);

  // Derive recovery id (v = 27 or 28) by trying both and comparing
  // the recovered public key with our known public key.
  {
    uint8_t our_pubkey[64];
    Secp256k1PubKey(private_key, our_pubkey);

    uint8_t recovered[64];
    if (EcRecover(&grp, &r, &s, hash, 0, recovered) &&
        std::memcmp(recovered, our_pubkey, 64) == 0) {
      signature[64] = 27;
    } else {
      signature[64] = 28;
    }
  }

  ok = true;

cleanup:
  mbedtls_mpi_free(&d);
  mbedtls_mpi_free(&r);
  mbedtls_mpi_free(&s);
  mbedtls_ecp_group_free(&grp);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
  return ok;
}

// ---------------------------------------------------------------------------
// EIP-712 hashing helpers.
// ---------------------------------------------------------------------------

// Keccak-256 of a string.
static void KeccakString(const std::string &str, uint8_t out[32]) {
  Keccak256(reinterpret_cast<const uint8_t *>(str.data()), str.size(), out);
}

// ABI-encode an Ethereum address (left-padded to 32 bytes).
static void AbiEncodeAddress(const uint8_t addr[20], uint8_t out[32]) {
  std::memset(out, 0, 32);
  std::memcpy(out + 12, addr, 20);
}

// ABI-encode a uint8 as uint256.
static void AbiEncodeUint8(uint8_t val, uint8_t out[32]) {
  std::memset(out, 0, 32);
  out[31] = val;
}

void ClobDomainSeparator(uint8_t out[32]) {
  // EIP-712 domain: { name: "ClobAuthDomain", version: "1", chainId: 137 }
  // typeHash = keccak256("EIP712Domain(string name,string version,uint256 chainId)")
  uint8_t type_hash[32];
  KeccakString("EIP712Domain(string name,string version,uint256 chainId)", type_hash);

  uint8_t name_hash[32];
  KeccakString("ClobAuthDomain", name_hash);

  uint8_t version_hash[32];
  KeccakString("1", version_hash);

  uint8_t chain_id[32] = {};
  chain_id[31] = 137;

  // domainSeparator = keccak256(abi.encode(typeHash, nameHash, versionHash, chainId))
  uint8_t encoded[128];
  std::memcpy(encoded, type_hash, 32);
  std::memcpy(encoded + 32, name_hash, 32);
  std::memcpy(encoded + 64, version_hash, 32);
  std::memcpy(encoded + 96, chain_id, 32);
  Keccak256(encoded, 128, out);
}

void HashClobAuth(const std::string &address, const std::string &timestamp,
                  const std::string &nonce, const std::string &message,
                  uint8_t out[32]) {
  // typeHash = keccak256("ClobAuth(address address,string timestamp,uint256 nonce,string message)")
  uint8_t type_hash[32];
  KeccakString(
      "ClobAuth(address address,string timestamp,uint256 nonce,string message)",
      type_hash);

  // Hash dynamic types.
  uint8_t timestamp_hash[32];
  KeccakString(timestamp, timestamp_hash);

  uint8_t message_hash[32];
  KeccakString(message, message_hash);

  // Encode address.
  uint8_t addr_encoded[32] = {};
  if (address.size() >= 2 && address[0] == '0' && address[1] == 'x') {
    uint8_t addr_bytes[20];
    HexDecode(address.substr(2), addr_bytes, 20);
    AbiEncodeAddress(addr_bytes, addr_encoded);
  }

  // Encode nonce as uint256.
  uint8_t nonce_encoded[32] = {};
  // nonce is a decimal string — convert to big-endian uint256.
  uint64_t nonce_val = std::strtoull(nonce.c_str(), nullptr, 10);
  for (int i = 7; i >= 0; --i) {
    nonce_encoded[24 + (7 - i)] = static_cast<uint8_t>((nonce_val >> (i * 8)) & 0xFF);
  }

  // structHash = keccak256(abi.encode(typeHash, address, timestamp, nonce, message))
  uint8_t encoded[160];
  std::memcpy(encoded, type_hash, 32);
  std::memcpy(encoded + 32, addr_encoded, 32);
  std::memcpy(encoded + 64, timestamp_hash, 32);
  std::memcpy(encoded + 96, nonce_encoded, 32);
  std::memcpy(encoded + 128, message_hash, 32);
  Keccak256(encoded, 160, out);
}

void HashOrder(const OrderFields &order, uint8_t out[32]) {
  // typeHash = keccak256("Order(uint256 salt,address maker,address signer,address taker,"
  //   "uint256 tokenId,uint256 makerAmount,uint256 takerAmount,uint256 expiration,"
  //   "uint256 nonce,uint256 feeRateBps,uint8 side,uint8 signatureType)")
  uint8_t type_hash[32];
  KeccakString(
      "Order(uint256 salt,address maker,address signer,address taker,"
      "uint256 tokenId,uint256 makerAmount,uint256 takerAmount,uint256 expiration,"
      "uint256 nonce,uint256 feeRateBps,uint8 side,uint8 signatureType)",
      type_hash);

  // 12 fields: typeHash + 11 fields (addresses are left-padded to 32,
  // uint8s are encoded as uint256).
  uint8_t encoded[12 * 32];
  std::memcpy(encoded, type_hash, 32);

  std::memcpy(encoded + 1 * 32, order.salt, 32);

  uint8_t tmp[32];
  AbiEncodeAddress(order.maker, tmp);
  std::memcpy(encoded + 2 * 32, tmp, 32);

  AbiEncodeAddress(order.signer, tmp);
  std::memcpy(encoded + 3 * 32, tmp, 32);

  AbiEncodeAddress(order.taker, tmp);
  std::memcpy(encoded + 4 * 32, tmp, 32);

  std::memcpy(encoded + 5 * 32, order.token_id, 32);
  std::memcpy(encoded + 6 * 32, order.maker_amount, 32);
  std::memcpy(encoded + 7 * 32, order.taker_amount, 32);
  std::memcpy(encoded + 8 * 32, order.expiration, 32);
  std::memcpy(encoded + 9 * 32, order.nonce, 32);
  std::memcpy(encoded + 10 * 32, order.fee_rate_bps, 32);

  AbiEncodeUint8(order.side, tmp);
  std::memcpy(encoded + 11 * 32, tmp, 32);

  // Note: signatureType is the 13th slot (index 12).
  uint8_t encoded_full[13 * 32];
  std::memcpy(encoded_full, encoded, 12 * 32);
  AbiEncodeUint8(order.signature_type, tmp);
  std::memcpy(encoded_full + 12 * 32, tmp, 32);

  Keccak256(encoded_full, 13 * 32, out);
}

bool Eip712Sign(const uint8_t private_key[32], const uint8_t domain_sep[32],
                const uint8_t struct_hash[32], uint8_t signature[65]) {
  // digest = keccak256("\x19\x01" || domainSep || structHash)
  uint8_t payload[66];
  payload[0] = 0x19;
  payload[1] = 0x01;
  std::memcpy(payload + 2, domain_sep, 32);
  std::memcpy(payload + 34, struct_hash, 32);

  uint8_t digest[32];
  Keccak256(payload, 66, digest);

  return Secp256k1Sign(private_key, digest, signature);
}

// ---------------------------------------------------------------------------
// HMAC-SHA256.
// ---------------------------------------------------------------------------

void HmacSha256(const uint8_t *key, size_t key_len,
                const uint8_t *data, size_t data_len,
                uint8_t out[32]) {
  const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(md_info, key, key_len, data, data_len, out);
}

// ---------------------------------------------------------------------------
// Hex utilities.
// ---------------------------------------------------------------------------

std::string HexEncode(const uint8_t *data, size_t len) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    result += kHex[data[i] >> 4];
    result += kHex[data[i] & 0x0F];
  }
  return result;
}

bool HexDecode(const std::string &hex, uint8_t *out, size_t out_len) {
  if (hex.size() != out_len * 2) {
    return false;
  }
  for (size_t i = 0; i < out_len; ++i) {
    auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int hi = nibble(hex[i * 2]);
    int lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

}  // namespace crypto
}  // namespace survaiv
