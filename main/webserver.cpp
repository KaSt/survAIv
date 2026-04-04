#include "webserver.h"

#include <cstring>
#include <vector>

#include "config.h"
#include "dashboard_state.h"
#include "wallet.h"
#include "web_assets.h"
#include "wisdom.h"

#include "esp_http_server.h"
#include "esp_log.h"
#if CONFIG_SURVAIV_ENABLE_OTA
#include "esp_ota_ops.h"
#endif
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace survaiv {
namespace webserver {

namespace {
constexpr const char *kTag = "survaiv_web";
constexpr int kMaxSseClients = 4;

httpd_handle_t g_server = nullptr;
bool g_is_onboard = false;

// SSE client tracking.
struct SseClient {
  int fd = -1;
  bool active = false;
};
SseClient g_sse_clients[kMaxSseClients] = {};
SemaphoreHandle_t g_sse_mutex = nullptr;

// ─── Auth state ─────────────────────────────────────────────────
static std::string g_pending_pin;
static std::string g_session_token;

static std::string GenerateClaimPin() {
  static const char *adjectives[] = {
      "swift", "brave", "calm",  "dark",  "eager", "fair", "glad",
      "happy", "keen",  "loud",  "neat",  "proud", "quiet","rare",
      "safe",  "tall",  "vast",  "warm",  "wise",  "young"};
  static const char *animals[] = {
      "bear", "crow", "deer", "eagle", "fox",   "hawk", "lion",
      "lynx", "moth", "newt", "orca",  "puma",  "raven","seal",
      "tiger","viper","whale","wolf",  "wren",  "yak"};
  uint32_t r1, r2, r3;
  esp_fill_random(&r1, sizeof(r1));
  esp_fill_random(&r2, sizeof(r2));
  esp_fill_random(&r3, sizeof(r3));
  char buf[32];
  snprintf(buf, sizeof(buf), "%s-%s-%04u",
           adjectives[r1 % 20], animals[r2 % 20],
           static_cast<unsigned>(r3 % 10000));
  return buf;
}

static std::string GenerateSessionToken() {
  uint8_t bytes[16];
  esp_fill_random(bytes, sizeof(bytes));
  char hex[33];
  for (int i = 0; i < 16; ++i) {
    snprintf(hex + i * 2, 3, "%02x", bytes[i]);
  }
  return hex;
}

static bool ValidateAuthToken(httpd_req_t *req) {
  if (g_session_token.empty()) return true;  // No owner yet — allow all
  char token[64] = {};
  httpd_req_get_hdr_value_str(req, "X-Auth-Token", token, sizeof(token));
  return g_session_token == token;
}

// ─── Handlers: Dashboard ────────────────────────────────────────

static esp_err_t DashboardGetHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kDashboardHtml, strlen(kDashboardHtml));
}

static esp_err_t ApiStateHandler(httpd_req_t *req) {
  std::string json = GetDashboardState().ToJson();
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.c_str(), json.size());
}

static esp_err_t ApiPositionsHandler(httpd_req_t *req) {
  std::string json = GetDashboardState().PositionsJson();
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.c_str(), json.size());
}

static esp_err_t ApiHistoryHandler(httpd_req_t *req) {
  std::string json = GetDashboardState().DecisionHistoryJson();
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.c_str(), json.size());
}

static esp_err_t ApiEquityHandler(httpd_req_t *req) {
  std::string json = GetDashboardState().EquityHistoryJson();
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.c_str(), json.size());
}

static esp_err_t ApiScoutedHandler(httpd_req_t *req) {
  std::string json = GetDashboardState().ScoutedMarketsJson();
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.c_str(), json.size());
}

static esp_err_t ApiWisdomHandler(httpd_req_t *req) {
  std::string json = wisdom::StatsJson();
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.c_str(), json.size());
}

static esp_err_t ApiKnowledgeExportHandler(httpd_req_t *req) {
  std::string json = wisdom::ExportKnowledge();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Content-Disposition",
                     "attachment; filename=\"survaiv-knowledge.json\"");
  return httpd_resp_send(req, json.c_str(), json.size());
}

static esp_err_t ApiKnowledgeImportHandler(httpd_req_t *req) {
  if (!ValidateAuthToken(req)) {
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Unauthorized");
    return ESP_FAIL;
  }
  if (req->content_len > 16384) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large (max 16KB)");
    return ESP_FAIL;
  }

  std::string body;
  body.resize(req->content_len);
  int received = 0;
  while (received < req->content_len) {
    int ret = httpd_req_recv(req, &body[received], req->content_len - received);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Receive error");
      return ESP_FAIL;
    }
    received += ret;
  }

  if (!wisdom::ImportKnowledge(body)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "Invalid knowledge file");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  const char *ok = "{\"ok\":true,\"msg\":\"Knowledge imported successfully\"}";
  return httpd_resp_send(req, ok, strlen(ok));
}

static esp_err_t ApiWisdomFreezeHandler(httpd_req_t *req) {
  if (!ValidateAuthToken(req)) {
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Unauthorized");
    return ESP_FAIL;
  }
  char buf[64] = {};
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_FAIL;
  }
  buf[received] = '\0';

  // Expect {"frozen":true} or {"frozen":false}
  bool freeze = (strstr(buf, "true") != nullptr);
  wisdom::SetFrozen(freeze);

  httpd_resp_set_type(req, "application/json");
  std::string resp = std::string("{\"ok\":true,\"frozen\":") + (freeze ? "true" : "false") + "}";
  return httpd_resp_send(req, resp.c_str(), resp.size());
}

// SSE endpoint — keeps connection open and sends events.
static esp_err_t ApiEventsHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/event-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // Send initial state.
  std::string initial = "event: state\ndata: " + GetDashboardState().SseStateEvent() + "\n\n";
  httpd_resp_send_chunk(req, initial.c_str(), initial.size());

  // Register this socket for SSE push.
  int fd = httpd_req_to_sockfd(req);
  if (g_sse_mutex) {
    xSemaphoreTake(g_sse_mutex, portMAX_DELAY);
    for (int i = 0; i < kMaxSseClients; ++i) {
      if (!g_sse_clients[i].active) {
        g_sse_clients[i].fd = fd;
        g_sse_clients[i].active = true;
        ESP_LOGI(kTag, "SSE client connected (fd=%d, slot=%d)", fd, i);
        break;
      }
    }
    xSemaphoreGive(g_sse_mutex);
  }

  // Keep connection alive — the client will receive events via PushSseEvent().
  // We send a keepalive comment every 15 seconds.
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(15000));
    const char *keepalive = ": keepalive\n\n";
    esp_err_t err = httpd_send(req, keepalive, strlen(keepalive));
    if (err != ESP_OK) {
      // Client disconnected.
      break;
    }
  }

  // Unregister.
  if (g_sse_mutex) {
    xSemaphoreTake(g_sse_mutex, portMAX_DELAY);
    for (int i = 0; i < kMaxSseClients; ++i) {
      if (g_sse_clients[i].fd == fd) {
        g_sse_clients[i].active = false;
        g_sse_clients[i].fd = -1;
        ESP_LOGI(kTag, "SSE client disconnected (slot=%d)", i);
        break;
      }
    }
    xSemaphoreGive(g_sse_mutex);
  }

  return ESP_OK;
}

// ─── Handlers: Onboarding ───────────────────────────────────────

static esp_err_t OnboardGetHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kOnboardHtml, strlen(kOnboardHtml));
}

// Captive portal: redirect all unknown requests.
static esp_err_t CaptiveHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kCaptiveRedirectHtml, strlen(kCaptiveRedirectHtml));
}

// WiFi scan endpoint.
static esp_err_t ApiScanHandler(httpd_req_t *req) {
  wifi_scan_config_t scan_config = {};
  scan_config.show_hidden = false;
  esp_wifi_scan_start(&scan_config, true);

  uint16_t ap_count = 0;
  esp_wifi_scan_get_ap_num(&ap_count);
  if (ap_count > 20) ap_count = 20;

  std::vector<wifi_ap_record_t> records(ap_count);
  esp_wifi_scan_get_ap_records(&ap_count, records.data());

  std::string json = "[";
  for (int i = 0; i < ap_count; ++i) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"";
    json += reinterpret_cast<const char *>(records[i].ssid);
    json += "\",\"rssi\":";
    json += std::to_string(records[i].rssi);
    json += "}";
  }
  json += "]";

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.c_str(), json.size());
}

// Save config endpoint.
static esp_err_t ApiSaveHandler(httpd_req_t *req) {
  // Read body.
  char buf[1024] = {};
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_FAIL;
  }
  buf[received] = '\0';

  // Parse JSON manually (simple key extraction).
  auto extract = [&](const char *key) -> std::string {
    std::string needle = std::string("\"") + key + "\":\"";
    const char *start = strstr(buf, needle.c_str());
    if (!start) return "";
    start += needle.size();
    const char *end = strchr(start, '"');
    if (!end) return "";
    return std::string(start, end - start);
  };

  auto extractInt = [&](const char *key) -> int {
    std::string needle = std::string("\"") + key + "\":";
    const char *start = strstr(buf, needle.c_str());
    if (!start) return -1;
    start += needle.size();
    return atoi(start);
  };

  std::string ssid = extract("wifi_ssid");
  std::string pass = extract("wifi_pass");
  std::string oai_url = extract("oai_url");
  std::string oai_model = extract("oai_model");
  std::string api_key = extract("api_key");
  std::string llm_provider = extract("llm_provider");
  std::string wallet_pk = extract("wallet_pk");
  std::string agent_name = extract("agent_name");
  int bankroll = extractInt("bankroll_cents");
  int paper_only = extractInt("paper_only");

  if (ssid.empty()) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing wifi_ssid");
    return ESP_FAIL;
  }

  // Save to NVS.
  config::SetString("wifi_ssid", ssid);
  config::SetString("wifi_pass", pass);
  if (!oai_url.empty()) config::SetString("oai_url", oai_url);
  if (!oai_model.empty()) config::SetString("oai_model", oai_model);
  if (!api_key.empty()) config::SetString("api_key", api_key);
  if (!llm_provider.empty()) config::SetString("llm_provider", llm_provider);
  if (!wallet_pk.empty()) config::SetString("wallet_pk", wallet_pk);
  if (!agent_name.empty()) config::SetString("agent_name", agent_name);
  if (bankroll > 0) config::SetInt("bankroll", bankroll);
  config::SetInt("paper_only", paper_only >= 0 ? paper_only : 1);

  ESP_LOGI(kTag, "Config saved: ssid=%s mode=%s", ssid.c_str(),
           paper_only == 0 ? "live" : "paper");

  httpd_resp_set_type(req, "application/json");
  const char *ok = "{\"ok\":true}";
  httpd_resp_send(req, ok, strlen(ok));

  // Reboot after a short delay.
  xTaskCreate([](void *) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    config::Reboot();
  }, "reboot", 2048, nullptr, 5, nullptr);

  return ESP_OK;
}

// Generate wallet on-device using hardware RNG.
static esp_err_t ApiGenerateWalletHandler(httpd_req_t *req) {
  // Check if a wallet already exists.
  bool exists = wallet::HasStoredKey();

  // Check for ?force=1 to allow overwriting.
  char query[32] = {};
  bool force = false;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char val[4] = {};
    if (httpd_query_key_value(query, "force", val, sizeof(val)) == ESP_OK) {
      force = (val[0] == '1');
    }
  }

  if (exists && !force) {
    httpd_resp_set_type(req, "application/json");
    // Return existing address without regenerating.
    if (wallet::IsReady()) {
      std::string json = "{\"exists\":true,\"address\":\"" + wallet::AddressHex() + "\"}";
      return httpd_resp_send(req, json.c_str(), json.size());
    }
    // Key exists but wallet not loaded — load it.
    if (wallet::Init()) {
      std::string json = "{\"exists\":true,\"address\":\"" + wallet::AddressHex() + "\"}";
      return httpd_resp_send(req, json.c_str(), json.size());
    }
    const char *err = "{\"exists\":true,\"address\":\"\",\"error\":\"Failed to load existing key\"}";
    return httpd_resp_send(req, err, strlen(err));
  }

  if (!wallet::Generate()) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wallet generation failed");
    return ESP_FAIL;
  }

  std::string json = "{\"exists\":false,\"address\":\"" + wallet::AddressHex() + "\",\"generated\":true}";
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.c_str(), json.size());
}

// ─── Handlers: Backup / Restore / OTA ───────────────────────────

static esp_err_t ApiBackupHandler(httpd_req_t *req) {
  // Check for ?full=1 query param to include sensitive keys.
  char query[32] = {};
  bool full = false;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char val[4] = {};
    if (httpd_query_key_value(query, "full", val, sizeof(val)) == ESP_OK) {
      full = (val[0] == '1');
    }
  }

  // Build JSON with all config values.
  std::string json = "{";
  json += "\"wifi_ssid\":\"" + config::WifiSsid() + "\"";
  json += ",\"wifi_pass\":\"" + (full ? config::WifiPassword() : std::string("***")) + "\"";
  json += ",\"oai_url\":\"" + config::OpenaiBaseUrl() + "\"";
  json += ",\"oai_model\":\"" + config::OpenaiModel() + "\"";
  json += ",\"api_key\":\"" + (full ? config::ApiKey() : std::string("***")) + "\"";
  json += ",\"llm_provider\":\"" + config::LlmProvider() + "\"";
  json += ",\"wallet_pk\":\"" + (full ? config::WalletPrivateKey() : std::string("***")) + "\"";
  json += ",\"poly_rpc\":\"" + config::PolygonRpcUrl() + "\"";
  json += ",\"clob_url\":\"" + config::ClobBaseUrl() + "\"";
  json += ",\"loop_sec\":" + std::to_string(config::LoopSeconds());
  json += ",\"bankroll\":" + std::to_string(config::StartingBankrollCents());
  json += ",\"reserve\":" + std::to_string(config::ReserveCents());
  json += ",\"max_pos\":" + std::to_string(config::MaxOpenPositions());
  json += ",\"mkt_limit\":" + std::to_string(config::MarketLimit());
  json += ",\"paper_only\":" + std::to_string(config::PaperTradingOnly() ? 1 : 0);
  json += ",\"loss_lim\":" + std::to_string(config::DailyLossLimitCents());
  json += ",\"live_conf\":" + std::to_string(config::LiveConfidenceThreshold());
  json += ",\"live_edge\":" + std::to_string(config::LiveMinEdgeBps());
  json += ",\"cooldown\":" + std::to_string(config::CooldownAfterLossSeconds());

  // Firmware info.
  const esp_app_desc_t *app = esp_app_get_description();
  json += ",\"firmware\":{\"version\":\"";
  json += app->version;
  json += "\",\"date\":\"";
  json += app->date;
  json += "\"}";
  json += "}";

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"survaiv-backup.json\"");
  return httpd_resp_send(req, json.c_str(), json.size());
}

static esp_err_t ApiRestoreHandler(httpd_req_t *req) {
  if (!ValidateAuthToken(req)) {
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Unauthorized");
    return ESP_FAIL;
  }
  char buf[2048] = {};
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_FAIL;
  }
  buf[received] = '\0';

  // Simple JSON key extraction (reuse pattern).
  auto extract = [&](const char *key) -> std::string {
    std::string needle = std::string("\"") + key + "\":\"";
    const char *start = strstr(buf, needle.c_str());
    if (!start) return "";
    start += needle.size();
    const char *end = strchr(start, '"');
    if (!end) return "";
    return std::string(start, end - start);
  };
  auto extractInt = [&](const char *key) -> int {
    std::string needle = std::string("\"") + key + "\":";
    const char *start = strstr(buf, needle.c_str());
    if (!start) return -1;
    start += needle.size();
    return atoi(start);
  };

  // Restore string keys (skip "***" masked values).
  auto restoreStr = [&](const char *key) {
    std::string val = extract(key);
    if (!val.empty() && val != "***") config::SetString(key, val);
  };

  restoreStr("wifi_ssid");
  restoreStr("wifi_pass");
  restoreStr("oai_url");
  restoreStr("oai_model");
  restoreStr("api_key");
  restoreStr("llm_provider");
  restoreStr("wallet_pk");
  restoreStr("poly_rpc");
  restoreStr("clob_url");

  // Restore int keys.
  auto restoreInt = [&](const char *key) {
    int val = extractInt(key);
    if (val >= 0) config::SetInt(key, val);
  };

  restoreInt("loop_sec");
  restoreInt("bankroll");
  restoreInt("reserve");
  restoreInt("max_pos");
  restoreInt("mkt_limit");
  restoreInt("paper_only");
  restoreInt("loss_lim");
  restoreInt("live_conf");
  restoreInt("live_edge");
  restoreInt("cooldown");

  ESP_LOGI(kTag, "Config restored from backup");

  httpd_resp_set_type(req, "application/json");
  const char *ok = "{\"ok\":true,\"msg\":\"Config restored. Rebooting…\"}";
  httpd_resp_send(req, ok, strlen(ok));

  xTaskCreate([](void *) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    config::Reboot();
  }, "reboot", 2048, nullptr, 5, nullptr);

  return ESP_OK;
}

#if CONFIG_SURVAIV_ENABLE_OTA
static esp_err_t ApiOtaHandler(httpd_req_t *req) {
  if (!ValidateAuthToken(req)) {
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Unauthorized");
    return ESP_FAIL;
  }
  ESP_LOGI(kTag, "OTA update started, size=%d", req->content_len);

  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(nullptr);
  if (!update_partition) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
    return ESP_FAIL;
  }

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_begin failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
    return ESP_FAIL;
  }

  char buf[1024];
  int total = 0;
  bool failed = false;

  while (true) {
    int received = httpd_req_recv(req, buf, sizeof(buf));
    if (received < 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
      ESP_LOGE(kTag, "OTA recv error");
      failed = true;
      break;
    }
    if (received == 0) break;  // Done.

    err = esp_ota_write(ota_handle, buf, received);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "esp_ota_write failed: %s", esp_err_to_name(err));
      failed = true;
      break;
    }
    total += received;
  }

  if (failed) {
    esp_ota_abort(ota_handle);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
    return ESP_FAIL;
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_end failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Boot partition set failed");
    return ESP_FAIL;
  }

  ESP_LOGI(kTag, "OTA success: %d bytes → %s", total, update_partition->label);

  httpd_resp_set_type(req, "application/json");
  char resp[128];
  snprintf(resp, sizeof(resp), "{\"ok\":true,\"bytes\":%d,\"partition\":\"%s\"}",
           total, update_partition->label);
  httpd_resp_send(req, resp, strlen(resp));

  xTaskCreate([](void *) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    config::Reboot();
  }, "reboot", 2048, nullptr, 5, nullptr);

  return ESP_OK;
}
#endif  // CONFIG_SURVAIV_ENABLE_OTA

// ─── POST /api/llm-config ───────────────────────────────────────
// Change LLM endpoint, model, and API key at runtime (paper mode only).
// Body: {"oai_url":"…","oai_model":"…","api_key":"…"}
// All fields optional — only provided fields are updated.
static esp_err_t ApiLlmConfigHandler(httpd_req_t *req) {
  if (!ValidateAuthToken(req)) {
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Unauthorized");
    return ESP_FAIL;
  }
  if (!config::PaperTradingOnly()) {
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                        "LLM config changes disabled in live mode");
    return ESP_FAIL;
  }

  char buf[512];
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_FAIL;
  }
  buf[received] = '\0';

  auto extract = [&](const char *key) -> std::string {
    std::string needle = std::string("\"") + key + "\":\"";
    const char *start = strstr(buf, needle.c_str());
    if (!start) return "";
    start += needle.size();
    const char *end = strchr(start, '"');
    if (!end) return "";
    return std::string(start, end - start);
  };

  std::string oai_url = extract("oai_url");
  std::string oai_model = extract("oai_model");
  std::string api_key = extract("api_key");

  if (!oai_url.empty()) config::SetString("oai_url", oai_url);
  if (!oai_model.empty()) config::SetString("oai_model", oai_model);
  if (!api_key.empty()) config::SetString("api_key", api_key);

  ESP_LOGI(kTag, "LLM config updated: url=%s model=%s",
           oai_url.empty() ? "(unchanged)" : oai_url.c_str(),
           oai_model.empty() ? "(unchanged)" : oai_model.c_str());

  httpd_resp_set_type(req, "application/json");
  // Return the effective config so the UI can confirm.
  std::string resp = "{\"ok\":true,\"oai_url\":\"" + config::OpenaiBaseUrl() +
                     "\",\"oai_model\":\"" + config::OpenaiModel() + "\"}";
  httpd_resp_send(req, resp.c_str(), resp.size());
  return ESP_OK;
}

// ─── Auth endpoint ──────────────────────────────────────────────

static esp_err_t ApiAuthGetHandler(httpd_req_t *req) {
  std::string pin = config::OwnerPin();
  httpd_resp_set_type(req, "application/json");

  if (pin.empty()) {
    // Not claimed yet — generate a pending PIN.
    if (g_pending_pin.empty()) {
      g_pending_pin = GenerateClaimPin();
      ESP_LOGI(kTag, "Generated claim PIN: %s", g_pending_pin.c_str());
    }
    std::string resp = "{\"claimed\":false,\"display_pin\":\"" + g_pending_pin + "\"}";
    return httpd_resp_send(req, resp.c_str(), resp.size());
  }

  // Claimed — check if caller has a valid token.
  char token[64] = {};
  httpd_req_get_hdr_value_str(req, "X-Auth-Token", token, sizeof(token));
  bool valid = !g_session_token.empty() && g_session_token == token;
  std::string resp = "{\"claimed\":true,\"needs_pin\":" +
                     std::string(valid ? "false" : "true") + "}";
  return httpd_resp_send(req, resp.c_str(), resp.size());
}

static esp_err_t ApiAuthPostHandler(httpd_req_t *req) {
  char buf[256] = {};
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_FAIL;
  }
  buf[received] = '\0';

  // Simple JSON extraction.
  auto extract = [&](const char *key) -> std::string {
    std::string needle = std::string("\"") + key + "\":\"";
    const char *start = strstr(buf, needle.c_str());
    if (!start) return "";
    start += needle.size();
    const char *end = strchr(start, '"');
    if (!end) return "";
    return std::string(start, end - start);
  };

  std::string action = extract("action");
  std::string pin = extract("pin");

  httpd_resp_set_type(req, "application/json");

  if (action == "claim") {
    if (g_pending_pin.empty() || pin != g_pending_pin) {
      const char *err = "{\"ok\":false,\"error\":\"PIN mismatch\"}";
      return httpd_resp_send(req, err, strlen(err));
    }
    config::SetString("owner_pin", pin);
    g_pending_pin.clear();
    g_session_token = GenerateSessionToken();
    std::string resp = "{\"ok\":true,\"token\":\"" + g_session_token + "\"}";
    return httpd_resp_send(req, resp.c_str(), resp.size());
  }

  if (action == "login") {
    std::string stored = config::OwnerPin();
    if (stored.empty() || pin != stored) {
      const char *err = "{\"ok\":false,\"error\":\"Wrong PIN\"}";
      return httpd_resp_send(req, err, strlen(err));
    }
    g_session_token = GenerateSessionToken();
    std::string resp = "{\"ok\":true,\"token\":\"" + g_session_token + "\"}";
    return httpd_resp_send(req, resp.c_str(), resp.size());
  }

  const char *err = "{\"ok\":false,\"error\":\"Unknown action\"}";
  return httpd_resp_send(req, err, strlen(err));
}

// ─── POST /api/config ───────────────────────────────────────────
// Partial config update (e.g. paper_only toggle) without reboot.
static esp_err_t ApiConfigPostHandler(httpd_req_t *req) {
  if (!ValidateAuthToken(req)) {
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Unauthorized");
    return ESP_FAIL;
  }

  char buf[512] = {};
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_FAIL;
  }
  buf[received] = '\0';

  auto extractInt = [&](const char *key) -> int {
    std::string needle = std::string("\"") + key + "\":";
    const char *start = strstr(buf, needle.c_str());
    if (!start) return -1;
    start += needle.size();
    return atoi(start);
  };

  auto extract = [&](const char *key) -> std::string {
    std::string needle = std::string("\"") + key + "\":\"";
    const char *start = strstr(buf, needle.c_str());
    if (!start) return "";
    start += needle.size();
    const char *end = strchr(start, '"');
    if (!end) return "";
    return std::string(start, end - start);
  };

  int paper_only = extractInt("paper_only");
  if (paper_only >= 0) {
    config::SetInt("paper_only", paper_only);
    ESP_LOGI(kTag, "Trading mode changed: %s", paper_only ? "paper" : "real");
  }

  std::string news_prov = extract("news_prov");
  if (!news_prov.empty()) {
    config::SetString("news_prov", news_prov);
  }
  std::string news_key = extract("news_key");
  if (!news_key.empty()) {
    config::SetString("news_key", news_key);
  }

  httpd_resp_set_type(req, "application/json");
  std::string resp = "{\"ok\":true,\"paper_only\":" +
                     std::to_string(config::PaperTradingOnly() ? 1 : 0) + "}";
  return httpd_resp_send(req, resp.c_str(), resp.size());
}

// ─── URI registration helpers ───────────────────────────────────

static void RegisterUri(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
  httpd_uri_t u = {};
  u.uri = uri;
  u.method = method;
  u.handler = handler;
  httpd_register_uri_handler(g_server, &u);
}

}  // namespace

void StartDashboard(int port) {
  if (g_server) return;

  if (!g_sse_mutex) {
    g_sse_mutex = xSemaphoreCreateMutex();
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = port;
  config.max_uri_handlers = 26;
  config.lru_purge_enable = true;
  config.max_open_sockets = 7;

  if (httpd_start(&g_server, &config) != ESP_OK) {
    ESP_LOGE(kTag, "Failed to start dashboard server");
    return;
  }

  RegisterUri("/", HTTP_GET, DashboardGetHandler);
  RegisterUri("/api/state", HTTP_GET, ApiStateHandler);
  RegisterUri("/api/positions", HTTP_GET, ApiPositionsHandler);
  RegisterUri("/api/history", HTTP_GET, ApiHistoryHandler);
  RegisterUri("/api/equity", HTTP_GET, ApiEquityHandler);
  RegisterUri("/api/scouted", HTTP_GET, ApiScoutedHandler);
  RegisterUri("/api/wisdom", HTTP_GET, ApiWisdomHandler);
  RegisterUri("/api/knowledge", HTTP_GET, ApiKnowledgeExportHandler);
  RegisterUri("/api/knowledge", HTTP_POST, ApiKnowledgeImportHandler);
  RegisterUri("/api/wisdom/freeze", HTTP_POST, ApiWisdomFreezeHandler);
  RegisterUri("/api/events", HTTP_GET, ApiEventsHandler);
  RegisterUri("/api/backup", HTTP_GET, ApiBackupHandler);
  RegisterUri("/api/restore", HTTP_POST, ApiRestoreHandler);
#if CONFIG_SURVAIV_ENABLE_OTA
  RegisterUri("/api/ota", HTTP_POST, ApiOtaHandler);
#endif
  RegisterUri("/api/generate-wallet", HTTP_POST, ApiGenerateWalletHandler);
  RegisterUri("/api/llm-config", HTTP_POST, ApiLlmConfigHandler);
  RegisterUri("/api/auth", HTTP_GET, ApiAuthGetHandler);
  RegisterUri("/api/auth", HTTP_POST, ApiAuthPostHandler);
  RegisterUri("/api/config", HTTP_POST, ApiConfigPostHandler);

  g_is_onboard = false;
  ESP_LOGI(kTag, "Dashboard server started on port %d", port);
}

void StartOnboarding(int port) {
  if (g_server) return;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = port;
  config.max_uri_handlers = 8;
  config.lru_purge_enable = true;

  if (httpd_start(&g_server, &config) != ESP_OK) {
    ESP_LOGE(kTag, "Failed to start onboarding server");
    return;
  }

  RegisterUri("/", HTTP_GET, OnboardGetHandler);
  RegisterUri("/api/scan", HTTP_GET, ApiScanHandler);
  RegisterUri("/api/save", HTTP_POST, ApiSaveHandler);
  RegisterUri("/api/generate-wallet", HTTP_POST, ApiGenerateWalletHandler);
  // Captive portal: catch common probe URLs.
  RegisterUri("/generate_204", HTTP_GET, CaptiveHandler);
  RegisterUri("/hotspot-detect.html", HTTP_GET, CaptiveHandler);
  RegisterUri("/connecttest.txt", HTTP_GET, CaptiveHandler);
  RegisterUri("/redirect", HTTP_GET, CaptiveHandler);

  g_is_onboard = true;
  ESP_LOGI(kTag, "Onboarding server started on port %d", port);
}

void Stop() {
  if (g_server) {
    httpd_stop(g_server);
    g_server = nullptr;
  }
}

void PushSseEvent(const std::string &event_type, const std::string &data) {
  if (!g_sse_mutex || !g_server) return;

  std::string msg = "event: " + event_type + "\ndata: " + data + "\n\n";

  xSemaphoreTake(g_sse_mutex, portMAX_DELAY);
  for (int i = 0; i < kMaxSseClients; ++i) {
    if (g_sse_clients[i].active) {
      int err = httpd_socket_send(g_server, g_sse_clients[i].fd,
                                  msg.c_str(), msg.size(), 0);
      if (err < 0) {
        g_sse_clients[i].active = false;
        g_sse_clients[i].fd = -1;
      }
    }
  }
  xSemaphoreGive(g_sse_mutex);
}

bool IsRunning() { return g_server != nullptr; }

}  // namespace webserver
}  // namespace survaiv
