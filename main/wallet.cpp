#include "wallet.h"

#include <cstdio>
#include <cstring>

#include "crypto.h"
#include "http.h"
#include "json_util.h"
#include "types.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace survaiv {
namespace wallet {

namespace {
constexpr const char *kTag = "survaiv_wallet";
constexpr const char *kNvsNamespace = "survaiv";
constexpr const char *kNvsKeyName = "wallet_key";

// Polygon Mainnet contract addresses.
constexpr const char *kUsdceAddress = "0x2791Bca1f2de4661ED88A30C99A7a9449Aa84174";
constexpr const char *kCtfExchangeAddress = "0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E";
constexpr const char *kConditionalTokensAddress = "0x4D97DCd97eC945f40cF65F87097ACe5EA0476045";

bool g_ready = false;
uint8_t g_private_key[32] = {};
uint8_t g_address[20] = {};

// Perform an eth_call JSON-RPC request. Returns the hex result string.
std::string EthCall(const std::string &to, const std::string &data) {
  std::string rpc_url = CONFIG_SURVAIV_POLYGON_RPC_URL;

  char body[512];
  snprintf(body, sizeof(body),
           "{\"jsonrpc\":\"2.0\",\"method\":\"eth_call\","
           "\"params\":[{\"to\":\"%s\",\"data\":\"%s\"},\"latest\"],\"id\":1}",
           to.c_str(), data.c_str());

  std::vector<std::pair<std::string, std::string>> headers = {
      {"Content-Type", "application/json"},
  };

  auto resp = HttpRequest(rpc_url, HTTP_METHOD_POST, headers, std::string(body));
  if (resp.status_code != 200) {
    ESP_LOGE(kTag, "RPC call failed: HTTP %d", resp.status_code);
    return "";
  }

  cJSON *root = cJSON_Parse(resp.body.c_str());
  if (!root) return "";

  std::string result = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "result"));
  cJSON_Delete(root);
  return result;
}

// Parse a hex uint256 RPC result to uint64_t (sufficient for USDC amounts).
uint64_t ParseHexUint64(const std::string &hex) {
  if (hex.size() < 2 || hex[0] != '0' || hex[1] != 'x') return 0;
  uint64_t val = 0;
  for (size_t i = 2; i < hex.size() && i < 66; ++i) {
    char c = hex[i];
    int nibble = 0;
    if (c >= '0' && c <= '9') nibble = c - '0';
    else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
    val = (val << 4) | nibble;
  }
  return val;
}

}  // namespace

bool Init() {
  g_ready = false;

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "NVS open failed: %s", esp_err_to_name(err));
    return false;
  }

  size_t key_len = 32;
  err = nvs_get_blob(handle, kNvsKeyName, g_private_key, &key_len);

  if (err == ESP_ERR_NVS_NOT_FOUND || key_len != 32) {
    const char *hex_key = CONFIG_SURVAIV_WALLET_PRIVATE_KEY;
    if (std::strlen(hex_key) == 64) {
      if (!crypto::HexDecode(std::string(hex_key), g_private_key, 32)) {
        ESP_LOGE(kTag, "Invalid hex in WALLET_PRIVATE_KEY");
        nvs_close(handle);
        return false;
      }
      err = nvs_set_blob(handle, kNvsKeyName, g_private_key, 32);
      if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGI(kTag, "Private key saved to NVS");
      }
    } else if (std::strlen(hex_key) > 0) {
      ESP_LOGE(kTag, "WALLET_PRIVATE_KEY must be exactly 64 hex chars");
      nvs_close(handle);
      return false;
    } else {
      ESP_LOGW(kTag, "No wallet key configured — live trading disabled");
      nvs_close(handle);
      return false;
    }
  } else if (err != ESP_OK) {
    ESP_LOGE(kTag, "NVS get_blob failed: %s", esp_err_to_name(err));
    nvs_close(handle);
    return false;
  }

  nvs_close(handle);

  if (!crypto::EthAddress(g_private_key, g_address)) {
    ESP_LOGE(kTag, "Failed to derive Ethereum address from key");
    std::memset(g_private_key, 0, 32);
    return false;
  }

  g_ready = true;
  ESP_LOGI(kTag, "Wallet ready: 0x%s", crypto::HexEncode(g_address, 20).c_str());
  return true;
}

bool Generate() {
  g_ready = false;

  // Generate 32 random bytes using hardware RNG.
  for (int i = 0; i < 32; i += 4) {
    uint32_t r = esp_random();
    std::memcpy(g_private_key + i, &r, 4);
  }

  // Verify the key produces a valid address.
  if (!crypto::EthAddress(g_private_key, g_address)) {
    ESP_LOGE(kTag, "Generated key failed address derivation");
    std::memset(g_private_key, 0, 32);
    return false;
  }

  // Store in NVS.
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "NVS open failed for wallet generation");
    std::memset(g_private_key, 0, 32);
    return false;
  }

  err = nvs_set_blob(handle, kNvsKeyName, g_private_key, 32);
  if (err == ESP_OK) nvs_commit(handle);
  nvs_close(handle);

  if (err != ESP_OK) {
    ESP_LOGE(kTag, "NVS write failed for generated key");
    std::memset(g_private_key, 0, 32);
    return false;
  }

  g_ready = true;
  ESP_LOGI(kTag, "New wallet generated: 0x%s", crypto::HexEncode(g_address, 20).c_str());
  return true;
}

bool HasStoredKey() {
  nvs_handle_t handle;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  size_t len = 0;
  esp_err_t err = nvs_get_blob(handle, kNvsKeyName, nullptr, &len);
  nvs_close(handle);
  return err == ESP_OK && len == 32;
}

const uint8_t *Address() { return g_address; }

std::string AddressHex() {
  return "0x" + crypto::HexEncode(g_address, 20);
}

const uint8_t *PrivateKey() { return g_private_key; }

bool IsReady() { return g_ready; }

double QueryUsdcBalance() {
  if (!g_ready) return -1.0;

  // balanceOf(address) selector = 0x70a08231
  // Followed by address left-padded to 32 bytes.
  std::string addr_hex = crypto::HexEncode(g_address, 20);
  std::string data = "0x70a08231000000000000000000000000" + addr_hex;

  std::string result = EthCall(kUsdceAddress, data);
  if (result.empty()) return -1.0;

  uint64_t raw = ParseHexUint64(result);
  return static_cast<double>(raw) / 1e6;  // USDC has 6 decimals.
}

bool CheckUsdcApproval() {
  if (!g_ready) return false;

  // allowance(owner, spender) selector = 0xdd62ed3e
  std::string addr_hex = crypto::HexEncode(g_address, 20);
  // CTF Exchange address (strip 0x prefix, pad to 32 bytes).
  std::string spender = std::string(kCtfExchangeAddress + 2);
  std::string data = "0xdd62ed3e000000000000000000000000" + addr_hex +
                     "000000000000000000000000" + spender;

  std::string result = EthCall(kUsdceAddress, data);
  if (result.empty()) return false;

  uint64_t allowance = ParseHexUint64(result);
  // Consider approved if allowance > 1000 USDC (arbitrary threshold).
  return allowance > 1000000000ULL;
}

bool CheckCtApproval() {
  if (!g_ready) return false;

  // isApprovedForAll(owner, operator) selector = 0xe985e9c5
  std::string addr_hex = crypto::HexEncode(g_address, 20);
  std::string operator_addr = std::string(kCtfExchangeAddress + 2);
  std::string data = "0xe985e9c5000000000000000000000000" + addr_hex +
                     "000000000000000000000000" + operator_addr;

  std::string result = EthCall(kConditionalTokensAddress, data);
  if (result.empty()) return false;

  uint64_t val = ParseHexUint64(result);
  return val != 0;
}

// Approval transactions require sending signed Polygon transactions,
// which needs MATIC for gas.  This is a simplified implementation
// that builds and submits a raw transaction via eth_sendRawTransaction.
// For the MVP, we log what's needed and skip if no MATIC is available.

bool ApproveUsdc() {
  ESP_LOGW(kTag, "USDC.e approval needed for CTF Exchange.");
  ESP_LOGW(kTag, "Run this from a wallet with MATIC gas:");
  ESP_LOGW(kTag, "  approve(%s, MAX_UINT256) on %s", kCtfExchangeAddress, kUsdceAddress);
  // TODO: Implement raw transaction signing and submission.
  // For now, check if already approved.
  return CheckUsdcApproval();
}

bool ApproveConditionalTokens() {
  ESP_LOGW(kTag, "Conditional Tokens approval needed for CTF Exchange.");
  ESP_LOGW(kTag, "Run this from a wallet with MATIC gas:");
  ESP_LOGW(kTag, "  setApprovalForAll(%s, true) on %s",
           kCtfExchangeAddress, kConditionalTokensAddress);
  return CheckCtApproval();
}

bool EnsureApprovals() {
  bool usdc_ok = CheckUsdcApproval();
  bool ct_ok = CheckCtApproval();

  if (usdc_ok) {
    ESP_LOGI(kTag, "USDC.e approval: OK");
  } else {
    ESP_LOGW(kTag, "USDC.e approval: MISSING");
    usdc_ok = ApproveUsdc();
  }

  if (ct_ok) {
    ESP_LOGI(kTag, "Conditional Tokens approval: OK");
  } else {
    ESP_LOGW(kTag, "Conditional Tokens approval: MISSING");
    ct_ok = ApproveConditionalTokens();
  }

  return usdc_ok && ct_ok;
}

}  // namespace wallet
}  // namespace survaiv
