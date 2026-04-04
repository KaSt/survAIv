#include "claw402.h"

#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "http.h"
#include "json_util.h"
#include "types.h"
#include "wallet.h"
#include "x402.h"

namespace survaiv {
namespace claw402 {

static const char *kTag = "claw402";
static const char *kBaseUrl = "https://claw402.ai/api/v1";
static double g_total_spent = 0.0;

// ── Helpers ──────────────────────────────────────────────────────

// Issue a GET request to a claw402 endpoint, handling x402 payment if needed.
// Returns the parsed cJSON root on success (caller must cJSON_Delete), or
// nullptr on failure.
static cJSON *FetchWithPayment(const std::string &url) {
  std::vector<std::pair<std::string, std::string>> headers;

  HttpResponse resp = HttpRequest(url, HTTP_METHOD_GET, headers);

  // Handle x402 402 Payment Required.
  if (resp.status_code == 402) {
    if (!wallet::IsReady()) {
      ESP_LOGE(kTag, "Wallet not ready for claw402 payment");
      return nullptr;
    }
    std::string payment = x402::MakePayment(resp);
    if (payment.empty()) {
      ESP_LOGE(kTag, "x402 payment construction failed for claw402");
      return nullptr;
    }
    headers.emplace_back("X-PAYMENT", payment);
    resp = HttpRequest(url, HTTP_METHOD_GET, headers);

    // Track spending ($0.001 per call).
    g_total_spent += 0.001;
    ESP_LOGI(kTag, "claw402 payment OK (session total: $%.4f)", g_total_spent);
  }

  if (resp.err != ESP_OK || resp.status_code < 200 || resp.status_code >= 300) {
    ESP_LOGW(kTag, "claw402 request failed: url=%s status=%d", url.c_str(),
             resp.status_code);
    return nullptr;
  }

  cJSON *root = cJSON_Parse(resp.body.c_str());
  if (!root) {
    ESP_LOGW(kTag, "Failed to parse claw402 JSON response");
  }
  return root;
}

// ── Public API ───────────────────────────────────────────────────

void Init() {
  g_total_spent = 0.0;
  ESP_LOGI(kTag, "claw402 data provider ready (claw402.ai)");
}

double TotalSpentUsdc() { return g_total_spent; }

FearGreed FetchFearGreed() {
  FearGreed result;
  std::string url = std::string(kBaseUrl) + "/fear-greed";
  cJSON *root = FetchWithPayment(url);
  if (!root) return result;

  // Try data wrapper or flat object.
  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  cJSON *src = data ? data : root;

  cJSON *val = cJSON_GetObjectItemCaseSensitive(src, "value");
  if (cJSON_IsNumber(val)) {
    result.value = val->valueint;
    result.valid = true;
  }
  cJSON *cls = cJSON_GetObjectItemCaseSensitive(src, "classification");
  if (!cls) cls = cJSON_GetObjectItemCaseSensitive(src, "value_classification");
  if (cJSON_IsString(cls)) {
    result.classification = cls->valuestring;
  }
  cJSON *ts = cJSON_GetObjectItemCaseSensitive(src, "timestamp");
  if (cJSON_IsNumber(ts)) {
    result.timestamp = static_cast<int64_t>(ts->valuedouble);
  }

  cJSON_Delete(root);
  if (result.valid) {
    ESP_LOGI(kTag, "Fear&Greed: %d (%s)", result.value,
             result.classification.c_str());
  }
  return result;
}

EtfInflow FetchBtcEtfInflow() {
  EtfInflow result;
  std::string url = std::string(kBaseUrl) + "/btc-etf-inflow";
  cJSON *root = FetchWithPayment(url);
  if (!root) return result;

  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  cJSON *src = data ? data : root;

  cJSON *flow = cJSON_GetObjectItemCaseSensitive(src, "total_net_flow");
  if (!flow) flow = cJSON_GetObjectItemCaseSensitive(src, "netFlow");
  if (!flow) flow = cJSON_GetObjectItemCaseSensitive(src, "net_flow");
  if (cJSON_IsNumber(flow)) {
    result.total_net_flow = flow->valuedouble;
    result.valid = true;
  }
  cJSON *dt = cJSON_GetObjectItemCaseSensitive(src, "date");
  if (cJSON_IsString(dt)) {
    result.date = dt->valuestring;
  }

  cJSON_Delete(root);
  if (result.valid) {
    ESP_LOGI(kTag, "BTC ETF flow: $%.0f (%s)", result.total_net_flow,
             result.date.c_str());
  }
  return result;
}

LargeTradesResult FetchLargeTrades(const std::string &symbol) {
  LargeTradesResult result;
  std::string url =
      std::string(kBaseUrl) + "/large-trades?symbol=" + symbol + "&limit=8";
  cJSON *root = FetchWithPayment(url);
  if (!root) return result;

  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  cJSON *arr = nullptr;
  if (cJSON_IsArray(data)) {
    arr = data;
  } else if (cJSON_IsArray(root)) {
    arr = root;
  } else if (data) {
    cJSON *trades = cJSON_GetObjectItemCaseSensitive(data, "trades");
    if (cJSON_IsArray(trades)) arr = trades;
  }

  if (arr) {
    int n = cJSON_GetArraySize(arr);
    if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) {
      cJSON *item = cJSON_GetArrayItem(arr, i);
      LargeTrade &t = result.trades[result.count];
      t.symbol = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "symbol"));
      if (t.symbol.empty()) t.symbol = symbol;
      t.side = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "side"));
      t.amount = JsonToDouble(cJSON_GetObjectItemCaseSensitive(item, "amount"));
      if (t.amount == 0.0)
        t.amount = JsonToDouble(cJSON_GetObjectItemCaseSensitive(item, "value"));
      cJSON *ts = cJSON_GetObjectItemCaseSensitive(item, "timestamp");
      if (cJSON_IsNumber(ts))
        t.timestamp = static_cast<int64_t>(ts->valuedouble);
      ++result.count;
    }
    result.valid = true;
  }

  cJSON_Delete(root);
  ESP_LOGI(kTag, "Large trades (%s): %d entries", symbol.c_str(), result.count);
  return result;
}

NewsResult FetchNews(const std::string &lang, int limit) {
  NewsResult result;
  if (limit > 6) limit = 6;
  std::ostringstream url;
  url << kBaseUrl << "/news?lang=" << lang << "&limit=" << limit;
  cJSON *root = FetchWithPayment(url.str());
  if (!root) return result;

  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  cJSON *arr = nullptr;
  if (cJSON_IsArray(data)) {
    arr = data;
  } else if (cJSON_IsArray(root)) {
    arr = root;
  } else if (data) {
    cJSON *news = cJSON_GetObjectItemCaseSensitive(data, "news");
    if (cJSON_IsArray(news)) arr = news;
  }

  if (arr) {
    int n = cJSON_GetArraySize(arr);
    if (n > 6) n = 6;
    for (int i = 0; i < n; ++i) {
      cJSON *item = cJSON_GetArrayItem(arr, i);
      NewsItem &ni = result.items[result.count];
      ni.title = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "title"));
      ni.source = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "source"));
      cJSON *ts = cJSON_GetObjectItemCaseSensitive(item, "timestamp");
      if (!ts)
        ts = cJSON_GetObjectItemCaseSensitive(item, "published_at");
      if (cJSON_IsNumber(ts))
        ni.timestamp = static_cast<int64_t>(ts->valuedouble);
      ++result.count;
    }
    result.valid = true;
  }

  cJSON_Delete(root);
  ESP_LOGI(kTag, "News: %d headlines", result.count);
  return result;
}

}  // namespace claw402
}  // namespace survaiv
