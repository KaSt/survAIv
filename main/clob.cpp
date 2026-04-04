#include "clob.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "crypto.h"
#include "http.h"
#include "json_util.h"
#include "wallet.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "sdkconfig.h"

namespace survaiv {
namespace clob {

namespace {
constexpr const char *kTag = "survaiv_clob";

struct ApiCredentials {
  std::string api_key;
  std::string secret;
  std::string passphrase;
};

ApiCredentials g_creds;
bool g_ready = false;

// Build L2 HMAC-SHA256 signature for a CLOB request.
std::string L2Sign(const std::string &timestamp, const std::string &method,
                   const std::string &path, const std::string &body) {
  std::string message = timestamp + method + path + body;

  // Decode base64 secret.
  // Polymarket returns the secret as a base64-encoded string.
  // We need to decode it for HMAC.
  // Simple base64 decode inline (secret is short, ~44 chars).
  auto Base64Decode = [](const std::string &in) -> std::string {
    auto B64Val = [](char c) -> int {
      if (c >= 'A' && c <= 'Z') return c - 'A';
      if (c >= 'a' && c <= 'z') return c - 'a' + 26;
      if (c >= '0' && c <= '9') return c - '0' + 52;
      if (c == '+') return 62;
      if (c == '/') return 63;
      return -1;
    };
    std::string out;
    int val = 0, bits = -8;
    for (char c : in) {
      int v = B64Val(c);
      if (v < 0) break;
      val = (val << 6) + v;
      bits += 6;
      if (bits >= 0) {
        out += static_cast<char>((val >> bits) & 0xFF);
        bits -= 8;
      }
    }
    return out;
  };

  std::string decoded_secret = Base64Decode(g_creds.secret);

  uint8_t hmac[32];
  crypto::HmacSha256(reinterpret_cast<const uint8_t *>(decoded_secret.data()),
                     decoded_secret.size(),
                     reinterpret_cast<const uint8_t *>(message.data()),
                     message.size(), hmac);

  // Base64-encode the HMAC result.
  auto Base64Encode = [](const uint8_t *data, size_t len) -> std::string {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -6;
    for (size_t i = 0; i < len; ++i) {
      val = (val << 8) + data[i];
      bits += 8;
      while (bits >= 0) {
        out += kTable[(val >> bits) & 0x3F];
        bits -= 6;
      }
    }
    if (bits > -6) {
      out += kTable[((val << 8) >> (bits + 8)) & 0x3F];
    }
    while (out.size() % 4) out += '=';
    return out;
  };

  return Base64Encode(hmac, 32);
}

std::string GetTimestamp() {
  time_t now;
  time(&now);
  return std::to_string(now);
}

// Perform an L2-authenticated HTTP request to the CLOB.
HttpResponse ClobRequest(const std::string &method, const std::string &path,
                         const std::string &body = "") {
  std::string timestamp = GetTimestamp();
  std::string sig = L2Sign(timestamp, method, path, body);
  std::string base_url = CONFIG_SURVAIV_CLOB_BASE_URL;
  std::string url = base_url + path;

  std::vector<std::pair<std::string, std::string>> headers = {
      {"POLY-ADDRESS", wallet::AddressHex()},
      {"POLY-SIGNATURE", sig},
      {"POLY-TIMESTAMP", timestamp},
      {"POLY-PASSPHRASE", g_creds.passphrase},
      {"POLY-API-KEY", g_creds.api_key},
      {"Content-Type", "application/json"},
  };

  esp_http_client_method_t http_method = HTTP_METHOD_GET;
  if (method == "POST") http_method = HTTP_METHOD_POST;
  else if (method == "DELETE") http_method = HTTP_METHOD_DELETE;

  return HttpRequest(url, http_method, headers, body);
}

}  // namespace

bool Init() {
  if (!wallet::IsReady()) {
    ESP_LOGE(kTag, "Wallet not initialised");
    return false;
  }

  // L1 Auth: EIP-712 sign ClobAuth → derive API credentials.
  std::string address = wallet::AddressHex();
  std::string timestamp = GetTimestamp();

  // Generate a random nonce.
  uint32_t nonce_raw = esp_random();
  char nonce_buf[16];
  snprintf(nonce_buf, sizeof(nonce_buf), "%" PRIu32, nonce_raw);
  std::string nonce(nonce_buf);

  std::string message = "This message attests that I control the given wallet";

  // Compute EIP-712 struct hash and domain separator.
  uint8_t domain_sep[32];
  crypto::ClobDomainSeparator(domain_sep);

  uint8_t struct_hash[32];
  crypto::HashClobAuth(address, timestamp, nonce, message, struct_hash);

  // Sign.
  uint8_t sig[65];
  if (!crypto::Eip712Sign(wallet::PrivateKey(), domain_sep, struct_hash, sig)) {
    ESP_LOGE(kTag, "EIP-712 signing failed");
    return false;
  }

  // Encode signature as 0x-prefixed hex.
  std::string sig_hex = "0x" + crypto::HexEncode(sig, 65);

  // POST to /auth/derive-api-key.
  std::string base_url = CONFIG_SURVAIV_CLOB_BASE_URL;
  std::string url = base_url + "/auth/derive-api-key";

  std::vector<std::pair<std::string, std::string>> headers = {
      {"POLY-ADDRESS", address},
      {"POLY-SIGNATURE", sig_hex},
      {"POLY-TIMESTAMP", timestamp},
      {"POLY-NONCE", nonce},
      {"Content-Type", "application/json"},
  };

  std::string body = "{\"message\":\"" + message + "\"}";

  auto resp = HttpRequest(url, HTTP_METHOD_POST, headers, body);
  if (resp.status_code != 200) {
    ESP_LOGE(kTag, "L1 auth failed: HTTP %d", resp.status_code);
    if (!resp.body.empty()) {
      ESP_LOGE(kTag, "Response: %.200s", resp.body.c_str());
    }
    return false;
  }

  // Parse API credentials from response.
  cJSON *root = cJSON_Parse(resp.body.c_str());
  if (!root) {
    ESP_LOGE(kTag, "Failed to parse L1 auth response");
    return false;
  }

  g_creds.api_key = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "apiKey"));
  g_creds.secret = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "secret"));
  g_creds.passphrase = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "passphrase"));
  cJSON_Delete(root);

  if (g_creds.api_key.empty() || g_creds.secret.empty()) {
    ESP_LOGE(kTag, "L1 auth returned empty credentials");
    return false;
  }

  g_ready = true;
  ESP_LOGI(kTag, "CLOB authenticated (apiKey: %.8s...)", g_creds.api_key.c_str());
  return true;
}

bool IsReady() { return g_ready; }

bool RefreshCredentials() {
  g_ready = false;
  return Init();
}

std::string PlaceOrder(const std::string &token_id, int side,
                       double price, double size) {
  if (!g_ready) {
    ESP_LOGE(kTag, "CLOB not authenticated");
    return "";
  }

  // Build EIP-712 Order struct.
  crypto::OrderFields order = {};

  // Random salt.
  for (int i = 0; i < 32; i += 4) {
    uint32_t rnd = esp_random();
    std::memcpy(order.salt + i, &rnd, std::min(4, 32 - i));
  }

  // Maker and signer = our address.
  std::memcpy(order.maker, wallet::Address(), 20);
  std::memcpy(order.signer, wallet::Address(), 20);
  // Taker = 0x0 (public order).
  std::memset(order.taker, 0, 20);

  // Token ID — parse from decimal string to uint256.
  {
    // Token IDs can be very large (uint256). Store as big-endian bytes.
    // For simplicity, if it fits in 64 bits, encode directly.
    // Otherwise, parse as decimal string to big-endian bytes.
    std::memset(order.token_id, 0, 32);
    // Try to parse as a large decimal. For Polymarket, token IDs
    // are typically large uint256 values.
    // We'll do a simple decimal-to-big-endian conversion.
    const char *s = token_id.c_str();
    // Use a simple schoolbook multiplication approach for arbitrary precision.
    uint8_t result[32] = {};
    for (const char *p = s; *p; ++p) {
      if (*p < '0' || *p > '9') continue;
      // result = result * 10 + digit.
      uint16_t carry = *p - '0';
      for (int i = 31; i >= 0; --i) {
        uint16_t v = static_cast<uint16_t>(result[i]) * 10 + carry;
        result[i] = static_cast<uint8_t>(v & 0xFF);
        carry = v >> 8;
      }
    }
    std::memcpy(order.token_id, result, 32);
  }

  // makerAmount = size * price * 1e6 (USDC has 6 decimals).
  // takerAmount = size * 1e6 (outcome tokens also have 6 decimals on Polymarket).
  // For a BUY order: we're paying makerAmount USDC to get takerAmount tokens.
  // makerAmount = size * price * 1e6, takerAmount = size * 1e6.
  {
    uint64_t maker_amt, taker_amt;
    if (side == 0) {
      // BUY: pay (size * price) USDC for (size) tokens.
      maker_amt = static_cast<uint64_t>(size * price * 1e6);
      taker_amt = static_cast<uint64_t>(size * 1e6);
    } else {
      // SELL: give (size) tokens for (size * price) USDC.
      maker_amt = static_cast<uint64_t>(size * 1e6);
      taker_amt = static_cast<uint64_t>(size * price * 1e6);
    }

    std::memset(order.maker_amount, 0, 32);
    std::memset(order.taker_amount, 0, 32);
    for (int i = 7; i >= 0; --i) {
      order.maker_amount[24 + (7 - i)] = static_cast<uint8_t>((maker_amt >> (i * 8)) & 0xFF);
      order.taker_amount[24 + (7 - i)] = static_cast<uint8_t>((taker_amt >> (i * 8)) & 0xFF);
    }
  }

  // Expiration = 0 (GTC).
  std::memset(order.expiration, 0, 32);

  // Nonce.
  {
    uint32_t nonce_val = esp_random();
    std::memset(order.nonce, 0, 32);
    order.nonce[28] = static_cast<uint8_t>((nonce_val >> 24) & 0xFF);
    order.nonce[29] = static_cast<uint8_t>((nonce_val >> 16) & 0xFF);
    order.nonce[30] = static_cast<uint8_t>((nonce_val >> 8) & 0xFF);
    order.nonce[31] = static_cast<uint8_t>(nonce_val & 0xFF);
  }

  // Fee rate — use 0 for maker orders (takers pay fees on Polymarket).
  std::memset(order.fee_rate_bps, 0, 32);

  order.side = static_cast<uint8_t>(side);
  order.signature_type = 0;  // EOA

  // Hash and sign the order.
  uint8_t domain_sep[32];
  crypto::ClobDomainSeparator(domain_sep);

  uint8_t order_hash[32];
  crypto::HashOrder(order, order_hash);

  uint8_t sig[65];
  if (!crypto::Eip712Sign(wallet::PrivateKey(), domain_sep, order_hash, sig)) {
    ESP_LOGE(kTag, "Order signing failed");
    return "";
  }

  std::string sig_hex = "0x" + crypto::HexEncode(sig, 65);

  // Build JSON payload for POST /order.
  auto Uint256ToDecimal = [](const uint8_t bytes[32]) -> std::string {
    // Convert big-endian uint256 to decimal string.
    // Simple approach: treat as a large number, divide by 10 repeatedly.
    uint8_t tmp[32];
    std::memcpy(tmp, bytes, 32);

    // Check if zero.
    bool all_zero = true;
    for (int i = 0; i < 32; ++i) {
      if (tmp[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) return "0";

    std::string digits;
    while (true) {
      bool all_zero_inner = true;
      for (int i = 0; i < 32; ++i) {
        if (tmp[i] != 0) { all_zero_inner = false; break; }
      }
      if (all_zero_inner) break;

      // Divide tmp by 10, get remainder.
      uint16_t rem = 0;
      for (int i = 0; i < 32; ++i) {
        uint16_t v = (rem << 8) | tmp[i];
        tmp[i] = static_cast<uint8_t>(v / 10);
        rem = v % 10;
      }
      digits += static_cast<char>('0' + rem);
    }

    std::reverse(digits.begin(), digits.end());
    return digits;
  };

  std::string maker_amount_str = Uint256ToDecimal(order.maker_amount);
  std::string taker_amount_str = Uint256ToDecimal(order.taker_amount);
  std::string salt_str = Uint256ToDecimal(order.salt);
  std::string nonce_str = Uint256ToDecimal(order.nonce);

  char price_str[32];
  snprintf(price_str, sizeof(price_str), "%.4f", price);

  // Construct order JSON.
  cJSON *json = cJSON_CreateObject();
  cJSON *order_json = cJSON_CreateObject();

  cJSON_AddStringToObject(order_json, "salt", salt_str.c_str());
  cJSON_AddStringToObject(order_json, "maker", wallet::AddressHex().c_str());
  cJSON_AddStringToObject(order_json, "signer", wallet::AddressHex().c_str());
  cJSON_AddStringToObject(order_json, "taker", "0x0000000000000000000000000000000000000000");
  cJSON_AddStringToObject(order_json, "tokenId", token_id.c_str());
  cJSON_AddStringToObject(order_json, "makerAmount", maker_amount_str.c_str());
  cJSON_AddStringToObject(order_json, "takerAmount", taker_amount_str.c_str());
  cJSON_AddStringToObject(order_json, "expiration", "0");
  cJSON_AddStringToObject(order_json, "nonce", nonce_str.c_str());
  cJSON_AddStringToObject(order_json, "feeRateBps", "0");
  cJSON_AddNumberToObject(order_json, "side", side);
  cJSON_AddNumberToObject(order_json, "signatureType", 0);
  cJSON_AddStringToObject(order_json, "signature", sig_hex.c_str());

  cJSON_AddItemToObject(json, "order", order_json);
  cJSON_AddStringToObject(json, "owner", wallet::AddressHex().c_str());
  cJSON_AddStringToObject(json, "orderType", "GTC");

  char *json_str = cJSON_PrintUnformatted(json);
  std::string payload(json_str);
  free(json_str);
  cJSON_Delete(json);

  ESP_LOGI(kTag, "Placing %s order: %.4f @ %s (token: %.16s...)",
           side == 0 ? "BUY" : "SELL", size, price_str, token_id.c_str());

  auto resp = ClobRequest("POST", "/order", payload);

  if (resp.status_code == 401) {
    ESP_LOGW(kTag, "Auth expired, refreshing credentials...");
    if (RefreshCredentials()) {
      resp = ClobRequest("POST", "/order", payload);
    }
  }

  if (resp.status_code != 200 && resp.status_code != 201) {
    ESP_LOGE(kTag, "Order placement failed: HTTP %d", resp.status_code);
    if (!resp.body.empty()) {
      ESP_LOGE(kTag, "Response: %.300s", resp.body.c_str());
    }
    return "";
  }

  // Parse order ID from response.
  cJSON *resp_json = cJSON_Parse(resp.body.c_str());
  if (!resp_json) {
    ESP_LOGE(kTag, "Failed to parse order response");
    return "";
  }

  std::string order_id = JsonToString(cJSON_GetObjectItemCaseSensitive(resp_json, "orderID"));
  if (order_id.empty()) {
    order_id = JsonToString(cJSON_GetObjectItemCaseSensitive(resp_json, "id"));
  }
  cJSON_Delete(resp_json);

  if (!order_id.empty()) {
    ESP_LOGI(kTag, "Order placed: %s", order_id.c_str());
  }
  return order_id;
}

bool CancelOrder(const std::string &order_id) {
  if (!g_ready) return false;

  std::string path = "/order/" + order_id;
  auto resp = ClobRequest("DELETE", path);

  if (resp.status_code == 401) {
    if (RefreshCredentials()) {
      resp = ClobRequest("DELETE", path);
    }
  }

  if (resp.status_code == 200 || resp.status_code == 204) {
    ESP_LOGI(kTag, "Order cancelled: %s", order_id.c_str());
    return true;
  }

  ESP_LOGE(kTag, "Cancel failed: HTTP %d", resp.status_code);
  return false;
}

std::string GetOpenOrders() {
  if (!g_ready) return "[]";

  auto resp = ClobRequest("GET", "/orders?open=true");
  if (resp.status_code == 401) {
    if (RefreshCredentials()) {
      resp = ClobRequest("GET", "/orders?open=true");
    }
  }

  if (resp.status_code == 200) {
    return resp.body;
  }
  return "[]";
}

int GetFeeRateBps() {
  // Polymarket charges 0 bps for makers currently.
  return 0;
}

}  // namespace clob
}  // namespace survaiv
