#include "config.h"

#include <cstring>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace survaiv {
namespace config {

namespace {
constexpr const char *kTag = "survaiv_cfg";
constexpr const char *kNs = "survaiv_cfg";

std::string GetNvsString(const char *key, const char *fallback) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return fallback;

  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) {
    nvs_close(h);
    return fallback;
  }

  std::string val(len - 1, '\0');
  nvs_get_str(h, key, &val[0], &len);
  nvs_close(h);
  return val;
}

int GetNvsInt(const char *key, int fallback) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return fallback;

  int32_t val = 0;
  if (nvs_get_i32(h, key, &val) != ESP_OK) {
    nvs_close(h);
    return fallback;
  }
  nvs_close(h);
  return static_cast<int>(val);
}

}  // namespace

void Init() {
  ESP_LOGI(kTag, "Loading config (NVS → Kconfig fallback)");
}

void SetString(const char *key, const std::string &value) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, key, value.c_str());
  nvs_commit(h);
  nvs_close(h);
}

void SetInt(const char *key, int value) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_i32(h, key, static_cast<int32_t>(value));
  nvs_commit(h);
  nvs_close(h);
}

std::string WifiSsid()        { return GetNvsString("wifi_ssid", CONFIG_SURVAIV_WIFI_SSID); }
std::string WifiPassword()    { return GetNvsString("wifi_pass", CONFIG_SURVAIV_WIFI_PASSWORD); }
std::string OpenaiBaseUrl()   { return GetNvsString("oai_url", CONFIG_SURVAIV_OPENAI_BASE_URL); }
std::string OpenaiModel()     { return GetNvsString("oai_model", CONFIG_SURVAIV_OPENAI_MODEL); }
std::string ApiKey()          { return GetNvsString("api_key", CONFIG_SURVAIV_API_KEY); }
std::string WalletPrivateKey(){ return GetNvsString("wallet_pk", CONFIG_SURVAIV_WALLET_PRIVATE_KEY); }
std::string PolygonRpcUrl()   { return GetNvsString("poly_rpc", CONFIG_SURVAIV_POLYGON_RPC_URL); }
std::string ClobBaseUrl()     { return GetNvsString("clob_url", CONFIG_SURVAIV_CLOB_BASE_URL); }
std::string LlmProvider()    { return GetNvsString("llm_provider", "apikey"); }

int LoopSeconds()             { return GetNvsInt("loop_sec", CONFIG_SURVAIV_LOOP_SECONDS); }
int StartingBankrollCents()   { return GetNvsInt("bankroll", CONFIG_SURVAIV_STARTING_BANKROLL_CENTS); }
int ReserveCents()            { return GetNvsInt("reserve", CONFIG_SURVAIV_RESERVE_CENTS); }
int MaxOpenPositions()        { return GetNvsInt("max_pos", CONFIG_SURVAIV_MAX_OPEN_POSITIONS); }
int MarketLimit()             { return GetNvsInt("mkt_limit", CONFIG_SURVAIV_MARKET_LIMIT); }
int DailyLossLimitCents()     { return GetNvsInt("loss_lim", CONFIG_SURVAIV_DAILY_LOSS_LIMIT_CENTS); }
int LiveConfidenceThreshold() { return GetNvsInt("live_conf", CONFIG_SURVAIV_LIVE_CONFIDENCE_THRESHOLD); }
int LiveMinEdgeBps()          { return GetNvsInt("live_edge", CONFIG_SURVAIV_LIVE_MIN_EDGE_BPS); }
int CooldownAfterLossSeconds(){ return GetNvsInt("cooldown", CONFIG_SURVAIV_COOLDOWN_AFTER_LOSS_SECONDS); }

bool PaperTradingOnly() {
  int val = GetNvsInt("paper_only", CONFIG_SURVAIV_PAPER_TRADING_ONLY ? 1 : 0);
  return val != 0;
}

bool HasStoredConfig() {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return false;

  size_t len = 0;
  esp_err_t err = nvs_get_str(h, "wifi_ssid", nullptr, &len);
  nvs_close(h);
  return err == ESP_OK && len > 1;
}

std::string AgentName() { return GetNvsString("agent_name", ""); }
std::string OwnerPin() { return GetNvsString("owner_pin", ""); }

void Reboot() {
  ESP_LOGI(kTag, "Rebooting...");
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
}

}  // namespace config
}  // namespace survaiv
