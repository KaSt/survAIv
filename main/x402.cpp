#include "x402.h"

#include <cinttypes>
#include <cstring>
#include <ctime>
#include <string>

#include "cJSON.h"
#include "config.h"
#include "crypto.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "provider.h"
#include "wallet.h"

namespace survaiv {
namespace x402 {

static const char *kTag = "x402";

// USDC on Base (EIP-155:8453).
static const char *kUsdcBaseAddrHex = "833589fCD6eDb6E08f4c7C32D4f71b54bdA02913";
static const uint64_t kBaseChainId = 8453;

static double g_total_spent_usdc = 0.0;

// ── Helpers ──────────────────────────────────────────────────────

static void Uint64ToUint256(uint64_t val, uint8_t out[32]) {
  std::memset(out, 0, 32);
  for (int i = 0; i < 8; ++i) {
    out[31 - i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
  }
}

static std::string Base64Encode(const std::string &input) {
  size_t required = 0;
  mbedtls_base64_encode(nullptr, 0, &required,
                        reinterpret_cast<const unsigned char *>(input.data()),
                        input.size());
  if (required == 0) return "";
  std::string buf(required, '\0');
  size_t written = 0;
  mbedtls_base64_encode(reinterpret_cast<unsigned char *>(&buf[0]), required,
                        &written,
                        reinterpret_cast<const unsigned char *>(input.data()),
                        input.size());
  // mbedtls includes null terminator in written count.
  if (written > 0 && buf[written - 1] == '\0') --written;
  buf.resize(written);
  return buf;
}

// ── EIP-712: USDC domain separator on Base ───────────────────────

static void UsdcDomainSeparator(uint8_t out[32], const char *name,
                                const char *version) {
  // typeHash = keccak256("EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)")
  static const char *kDomainType =
      "EIP712Domain(string name,string version,uint256 chainId,"
      "address verifyingContract)";
  uint8_t type_hash[32];
  crypto::Keccak256(reinterpret_cast<const uint8_t *>(kDomainType),
                    strlen(kDomainType), type_hash);

  uint8_t name_hash[32];
  crypto::Keccak256(reinterpret_cast<const uint8_t *>(name), strlen(name),
                    name_hash);

  uint8_t ver_hash[32];
  crypto::Keccak256(reinterpret_cast<const uint8_t *>(version),
                    strlen(version), ver_hash);

  uint8_t chain_id[32];
  Uint64ToUint256(kBaseChainId, chain_id);

  // verifyingContract left-padded to 32 bytes.
  uint8_t contract[32] = {};
  uint8_t addr[20];
  crypto::HexDecode(std::string(kUsdcBaseAddrHex), addr, 20);
  std::memcpy(contract + 12, addr, 20);

  // domainSep = keccak256(abi.encode(typeHash, nameHash, versionHash, chainId, contract))
  uint8_t encoded[160];
  std::memcpy(encoded, type_hash, 32);
  std::memcpy(encoded + 32, name_hash, 32);
  std::memcpy(encoded + 64, ver_hash, 32);
  std::memcpy(encoded + 96, chain_id, 32);
  std::memcpy(encoded + 128, contract, 32);
  crypto::Keccak256(encoded, 160, out);
}

// ── EIP-3009: TransferWithAuthorization struct hash ──────────────

static void HashTransferAuth(const uint8_t from[20], const uint8_t to[20],
                             const uint8_t value[32],
                             const uint8_t valid_after[32],
                             const uint8_t valid_before[32],
                             const uint8_t nonce[32], uint8_t out[32]) {
  static const char *kTypeStr =
      "TransferWithAuthorization(address from,address to,uint256 value,"
      "uint256 validAfter,uint256 validBefore,bytes32 nonce)";
  uint8_t type_hash[32];
  crypto::Keccak256(reinterpret_cast<const uint8_t *>(kTypeStr),
                    strlen(kTypeStr), type_hash);

  uint8_t encoded[224];  // 7 × 32
  std::memcpy(encoded, type_hash, 32);

  // from — address left-padded to 32 bytes.
  uint8_t tmp[32] = {};
  std::memcpy(tmp + 12, from, 20);
  std::memcpy(encoded + 32, tmp, 32);

  // to
  std::memset(tmp, 0, 32);
  std::memcpy(tmp + 12, to, 20);
  std::memcpy(encoded + 64, tmp, 32);

  std::memcpy(encoded + 96, value, 32);
  std::memcpy(encoded + 128, valid_after, 32);
  std::memcpy(encoded + 160, valid_before, 32);
  std::memcpy(encoded + 192, nonce, 32);

  crypto::Keccak256(encoded, 224, out);
}

static std::string Base64Decode(const std::string &input) {
  size_t required = 0;
  mbedtls_base64_decode(nullptr, 0, &required,
                        reinterpret_cast<const unsigned char *>(input.data()),
                        input.size());
  if (required == 0) return "";
  std::string buf(required, '\0');
  size_t written = 0;
  int ret = mbedtls_base64_decode(
      reinterpret_cast<unsigned char *>(&buf[0]), required, &written,
      reinterpret_cast<const unsigned char *>(input.data()), input.size());
  if (ret != 0) return "";
  buf.resize(written);
  return buf;
}

// ── Public API ───────────────────────────────────────────────────

void Init() {
  g_total_spent_usdc = 0.0;
  ESP_LOGI(kTag, "x402 payment module ready (Base/USDC)");
}

bool IsConfigured() {
  return providers::ActiveProviderUsesX402();
}

double TotalSpentUsdc() { return g_total_spent_usdc; }
void ResetSpending() { g_total_spent_usdc = 0.0; }

std::string MakePayment(const HttpResponse &resp_402) {
  // ── Parse 402 payment info (body or header) ───────────────────
  cJSON *root = cJSON_Parse(resp_402.body.c_str());
  cJSON *accepts = root ? cJSON_GetObjectItemCaseSensitive(root, "accepts")
                        : nullptr;

  // Fallback: PAYMENT-REQUIRED header (x402engine format).
  if (!cJSON_IsArray(accepts) || cJSON_GetArraySize(accepts) == 0) {
    if (root) cJSON_Delete(root);
    root = nullptr;
    accepts = nullptr;

    auto it = resp_402.headers.find("payment-required");
    if (it == resp_402.headers.end()) {
      ESP_LOGE(kTag, "No payment info in 402 body or headers");
      return "";
    }
    std::string decoded = Base64Decode(it->second);
    if (decoded.empty()) {
      ESP_LOGE(kTag, "Failed to base64-decode PAYMENT-REQUIRED header");
      return "";
    }
    root = cJSON_Parse(decoded.c_str());
    if (!root) {
      ESP_LOGE(kTag, "Failed to parse decoded PAYMENT-REQUIRED JSON");
      return "";
    }
    accepts = cJSON_GetObjectItemCaseSensitive(root, "accepts");
    if (!cJSON_IsArray(accepts) || cJSON_GetArraySize(accepts) == 0) {
      ESP_LOGE(kTag, "No accepts in decoded payment header");
      cJSON_Delete(root);
      return "";
    }
  }

  int version = 1;
  cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "x402Version");
  if (cJSON_IsNumber(ver)) version = ver->valueint;

  // Find Base chain payment option (eip155:8453).
  cJSON *accept = nullptr;
  int arr_size = cJSON_GetArraySize(accepts);
  for (int i = 0; i < arr_size; ++i) {
    cJSON *item = cJSON_GetArrayItem(accepts, i);
    cJSON *j_net = cJSON_GetObjectItemCaseSensitive(item, "network");
    if (cJSON_IsString(j_net) &&
        strcmp(j_net->valuestring, "eip155:8453") == 0) {
      accept = item;
      break;
    }
  }
  if (!accept) {
    accept = cJSON_GetArrayItem(accepts, 0);
  }
  cJSON *j_amount = cJSON_GetObjectItemCaseSensitive(accept, "amount");
  cJSON *j_pay_to = cJSON_GetObjectItemCaseSensitive(accept, "payTo");
  cJSON *j_timeout = cJSON_GetObjectItemCaseSensitive(accept, "maxTimeoutSeconds");
  cJSON *j_extra = cJSON_GetObjectItemCaseSensitive(accept, "extra");

  if (!cJSON_IsString(j_amount) || !cJSON_IsString(j_pay_to)) {
    ESP_LOGE(kTag, "Missing amount/payTo in 402");
    cJSON_Delete(root);
    return "";
  }

  const char *amount_str = j_amount->valuestring;
  const char *pay_to_raw = j_pay_to->valuestring;
  int timeout_sec = cJSON_IsNumber(j_timeout) ? j_timeout->valueint : 300;

  // USDC domain params (from extra, or known defaults).
  const char *usdc_name = "USD Coin";
  const char *usdc_ver = "2";
  if (cJSON_IsObject(j_extra)) {
    cJSON *jn = cJSON_GetObjectItemCaseSensitive(j_extra, "name");
    cJSON *jv = cJSON_GetObjectItemCaseSensitive(j_extra, "version");
    if (cJSON_IsString(jn)) usdc_name = jn->valuestring;
    if (cJSON_IsString(jv)) usdc_ver = jv->valuestring;
  }

  // ── Wallet check ──────────────────────────────────────────────
  if (!wallet::IsReady()) {
    ESP_LOGE(kTag, "Wallet not ready for x402 payment");
    cJSON_Delete(root);
    return "";
  }

  // ── Parse payTo address ───────────────────────────────────────
  uint8_t pay_to_addr[20];
  std::string pay_to_hex(pay_to_raw);
  if (pay_to_hex.size() >= 2 && pay_to_hex[0] == '0' && pay_to_hex[1] == 'x')
    pay_to_hex = pay_to_hex.substr(2);
  if (!crypto::HexDecode(pay_to_hex, pay_to_addr, 20)) {
    ESP_LOGE(kTag, "Invalid payTo address");
    cJSON_Delete(root);
    return "";
  }

  // ── Build TransferWithAuthorization fields ────────────────────
  uint64_t amount_atomic = strtoull(amount_str, nullptr, 10);

  uint8_t value_u256[32];
  Uint64ToUint256(amount_atomic, value_u256);

  uint8_t valid_after[32] = {};  // 0 = immediately valid

  uint64_t vb_val = 0;
  time_t now = time(nullptr);
  if (now > 1600000000) {
    vb_val = static_cast<uint64_t>(now) + timeout_sec;
  } else {
    vb_val = 0xFFFFFFFFULL;  // Time not synced — use max.
  }
  uint8_t valid_before[32];
  Uint64ToUint256(vb_val, valid_before);

  // Random nonce (32 bytes, EIP-3009 uses random nonce not account nonce).
  uint8_t nonce[32];
  for (int i = 0; i < 32; i += 4) {
    uint32_t r = esp_random();
    std::memcpy(nonce + i, &r, 4);
  }

  // ── EIP-712 sign ──────────────────────────────────────────────
  uint8_t domain_sep[32];
  UsdcDomainSeparator(domain_sep, usdc_name, usdc_ver);

  uint8_t struct_hash[32];
  HashTransferAuth(wallet::Address(), pay_to_addr, value_u256, valid_after,
                   valid_before, nonce, struct_hash);

  uint8_t signature[65];
  if (!crypto::Eip712Sign(wallet::PrivateKey(), domain_sep, struct_hash,
                          signature)) {
    ESP_LOGE(kTag, "EIP-712 signing failed");
    cJSON_Delete(root);
    return "";
  }

  // ── Track spending ────────────────────────────────────────────
  double spent = static_cast<double>(amount_atomic) / 1e6;
  g_total_spent_usdc += spent;
  ESP_LOGI(kTag, "Payment: $%.6f USDC (session total: $%.6f)", spent,
           g_total_spent_usdc);

  // ── Build JSON payload ────────────────────────────────────────
  std::string sig_hex = "0x" + crypto::HexEncode(signature, 65);
  std::string from_hex = wallet::AddressHex();
  std::string nonce_hex = "0x" + crypto::HexEncode(nonce, 32);
  char vb_str[24];
  snprintf(vb_str, sizeof(vb_str), "%" PRIu64, vb_val);

  cJSON *payment = cJSON_CreateObject();
  cJSON_AddNumberToObject(payment, "x402Version", version);

  if (version >= 2) {
    cJSON *resource = cJSON_GetObjectItemCaseSensitive(root, "resource");
    if (resource)
      cJSON_AddItemToObject(payment, "resource",
                            cJSON_Duplicate(resource, true));
    cJSON_AddItemToObject(payment, "accepted",
                          cJSON_Duplicate(accept, true));
  } else {
    cJSON *j_scheme = cJSON_GetObjectItemCaseSensitive(accept, "scheme");
    cJSON *j_network = cJSON_GetObjectItemCaseSensitive(accept, "network");
    cJSON_AddStringToObject(
        payment, "scheme",
        cJSON_IsString(j_scheme) ? j_scheme->valuestring : "exact");
    cJSON_AddStringToObject(
        payment, "network",
        cJSON_IsString(j_network) ? j_network->valuestring : "eip155:8453");
  }

  cJSON *payload = cJSON_CreateObject();
  cJSON_AddStringToObject(payload, "signature", sig_hex.c_str());

  cJSON *auth = cJSON_CreateObject();
  cJSON_AddStringToObject(auth, "from", from_hex.c_str());
  cJSON_AddStringToObject(auth, "to", pay_to_raw);
  cJSON_AddStringToObject(auth, "value", amount_str);
  cJSON_AddStringToObject(auth, "validAfter", "0");
  cJSON_AddStringToObject(auth, "validBefore", vb_str);
  cJSON_AddStringToObject(auth, "nonce", nonce_hex.c_str());

  cJSON_AddItemToObject(payload, "authorization", auth);
  cJSON_AddItemToObject(payment, "payload", payload);

  char *json_str = cJSON_PrintUnformatted(payment);
  std::string json(json_str);
  free(json_str);
  cJSON_Delete(payment);
  cJSON_Delete(root);

  return Base64Encode(json);
}

}  // namespace x402
}  // namespace survaiv
