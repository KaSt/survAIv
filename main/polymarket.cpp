#include "polymarket.h"

#include <sstream>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "http.h"
#include "json_util.h"

namespace survaiv {

namespace {

constexpr const char *kTag = "survaiv_poly";

constexpr const char *kPolymarketMarketsUrl =
    "https://gamma-api.polymarket.com/markets?active=true&closed=false&ascending=false&limit=";
constexpr const char *kPolymarketGeoblockUrl = "https://polymarket.com/api/geoblock";

}  // namespace

GeoblockStatus FetchGeoblockStatus() {
  HttpResponse response = HttpRequest(kPolymarketGeoblockUrl, HTTP_METHOD_GET, {});
  GeoblockStatus status;
  if (response.err != ESP_OK || response.status_code != 200) {
    ESP_LOGW(kTag, "Unable to fetch geoblock state; defaulting to blocked.");
    return status;
  }

  cJSON *root = cJSON_Parse(response.body.c_str());
  if (root == nullptr) {
    return status;
  }

  cJSON *blocked = cJSON_GetObjectItemCaseSensitive(root, "blocked");
  if (blocked != nullptr) {
    status.blocked = cJSON_IsTrue(blocked);
  }
  status.country = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "country"));
  status.region = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "region"));
  status.ip = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "ip"));
  cJSON_Delete(root);
  return status;
}

std::vector<MarketSnapshot> FetchMarkets(int limit, int offset,
                                         const std::string &order) {
  // Each market is ~7 KB in the API response; cap fetch to keep response
  // under ~42 KB so it fits comfortably in heap alongside TLS buffers.
#if CONFIG_IDF_TARGET_ESP32S3
  int fetch_limit = std::min(limit, 50);  // S3: PSRAM allows more
#else
  int fetch_limit = std::min(limit, 6);   // C3: ~42 KB budget
#endif
  std::ostringstream url;
  url << kPolymarketMarketsUrl << fetch_limit << "&offset=" << offset << "&order=" << order;

  ESP_LOGI(kTag, "Fetching %d markets (heap: %lu)", fetch_limit,
           static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));

  HttpResponse response = HttpRequest(url.str(), HTTP_METHOD_GET, {});
  std::vector<MarketSnapshot> markets;
  if (response.err != ESP_OK || response.status_code != 200) {
    ESP_LOGW(kTag, "Polymarket market fetch failed: status=%d", response.status_code);
    return markets;
  }

  cJSON *root = cJSON_Parse(response.body.c_str());
  // Free the response body immediately — only the cJSON tree is needed now.
  { std::string().swap(response.body); }

  if (root == nullptr || !cJSON_IsArray(root)) {
    if (root != nullptr) {
      cJSON_Delete(root);
    }
    return markets;
  }

  cJSON *item = nullptr;
  cJSON_ArrayForEach(item, root) {
    MarketSnapshot market;
    market.id = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "id"));
    market.question = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "question"));
    market.slug = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "slug"));
    market.category = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "category"));
    market.end_date = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "endDate"));

    // Parse description for LLM context (truncate for memory).
    std::string desc = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "description"));
#if CONFIG_IDF_TARGET_ESP32S3
    constexpr size_t kMaxDescLen = 500;
#else
    constexpr size_t kMaxDescLen = 200;
#endif
    if (desc.size() > kMaxDescLen) desc.resize(kMaxDescLen);
    market.description = std::move(desc);
    market.liquidity = JsonToDouble(cJSON_GetObjectItemCaseSensitive(item, "liquidity"));
    market.volume = JsonToDouble(cJSON_GetObjectItemCaseSensitive(item, "volume"));

    // Parse clobTokenIds for live trading (array of two token ID strings).
    cJSON *clob_ids = cJSON_GetObjectItemCaseSensitive(item, "clobTokenIds");
    if (clob_ids != nullptr) {
      cJSON *parsed = nullptr;
      if (cJSON_IsArray(clob_ids)) {
        parsed = cJSON_Duplicate(clob_ids, 1);
      } else if (cJSON_IsString(clob_ids) && clob_ids->valuestring != nullptr) {
        parsed = cJSON_Parse(clob_ids->valuestring);
      }
      if (parsed != nullptr && cJSON_IsArray(parsed)) {
        cJSON *yes_tok = cJSON_GetArrayItem(parsed, 0);
        cJSON *no_tok = cJSON_GetArrayItem(parsed, 1);
        if (yes_tok != nullptr) market.clob_token_yes = JsonToString(yes_tok);
        if (no_tok != nullptr) market.clob_token_no = JsonToString(no_tok);
      }
      if (parsed != nullptr) cJSON_Delete(parsed);
    }

    std::vector<double> prices =
        ParseStringifiedArrayToDoubles(cJSON_GetObjectItemCaseSensitive(item, "outcomePrices"));
    if (prices.size() >= 2) {
      market.yes_price = prices[0];
      market.no_price = prices[1];
    }

    if (!market.id.empty() && !market.question.empty()) {
      // Skip near-resolved markets — no tradeable edge when price is extreme.
      if (market.yes_price > 0.95 || market.yes_price < 0.05) continue;
      markets.push_back(market);
      if (static_cast<int>(markets.size()) >= limit) break;
    }
  }

  cJSON_Delete(root);
  ESP_LOGI(kTag, "Fetched %d tradeable markets (from %d raw)", 
           static_cast<int>(markets.size()), fetch_limit);
  return markets;
}

std::string BuildMarketsJson(const std::vector<MarketSnapshot> &markets) {
  std::ostringstream json;
  json << "[";
  for (size_t i = 0; i < markets.size(); ++i) {
    const auto &market = markets[i];
    if (i != 0) {
      json << ",";
    }
    json << "{"
         << "\"id\":\"" << JsonEscape(market.id) << "\","
         << "\"question\":\"" << JsonEscape(market.question) << "\","
         << "\"description\":\"" << JsonEscape(market.description) << "\","
         << "\"category\":\"" << JsonEscape(market.category) << "\","
         << "\"yes_price\":" << market.yes_price << ","
         << "\"no_price\":" << market.no_price << ","
         << "\"liquidity\":" << market.liquidity << ","
         << "\"volume\":" << market.volume << ","
         << "\"end_date\":\"" << JsonEscape(market.end_date) << "\"}";
  }
  json << "]";
  return json.str();
}

std::string BuildPositionsJson(const std::vector<Position> &positions,
                               const std::vector<MarketSnapshot> &markets) {
  std::ostringstream json;
  json << "[";
  for (size_t i = 0; i < positions.size(); ++i) {
    const auto &position = positions[i];
    const MarketSnapshot *market = FindMarket(markets, position.market_id);
    double current_price = 0.0;
    if (market != nullptr) {
      current_price = position.side == "yes" ? market->yes_price : market->no_price;
    }
    double current_value = position.shares * current_price;
    double unrealized_pnl = current_value - position.stake_usdc;
    if (i != 0) {
      json << ",";
    }
    json << "{"
         << "\"market_id\":\"" << JsonEscape(position.market_id) << "\","
         << "\"question\":\"" << JsonEscape(position.question) << "\","
         << "\"side\":\"" << JsonEscape(position.side) << "\","
         << "\"entry_price\":" << position.entry_price << ","
         << "\"stake_usdc\":" << position.stake_usdc << ","
         << "\"current_value\":" << current_value << ","
         << "\"unrealized_pnl\":" << unrealized_pnl << ","
         << "\"is_live\":" << (position.is_live ? "true" : "false") << "}";
  }
  json << "]";
  return json.str();
}

}  // namespace survaiv
