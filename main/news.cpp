#include "news.h"

#include <sstream>
#include <utility>

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "http.h"
#include "json_util.h"

namespace survaiv {

namespace {
constexpr const char *kTag = "survaiv_news";

#if CONFIG_IDF_TARGET_ESP32S3
constexpr size_t kMaxSnippetLen = 300;
#elif !CONFIG_SURVAIV_ENABLE_OTA
constexpr size_t kMaxSnippetLen = 250;
#else
constexpr size_t kMaxSnippetLen = 150;
#endif

std::vector<NewsResult> SearchTavily(const std::string &query, int limit,
                                      const std::string &api_key) {
  std::vector<NewsResult> results;

  // Build JSON body.
  std::ostringstream body;
  body << "{\"query\":\"" << JsonEscape(query) << "\","
       << "\"api_key\":\"" << JsonEscape(api_key) << "\","
       << "\"search_depth\":\"basic\","
       << "\"include_answer\":false,"
       << "\"max_results\":" << limit << "}";

  std::vector<std::pair<std::string, std::string>> headers;
  headers.push_back({"Content-Type", "application/json"});

  HttpResponse resp = HttpRequest("https://api.tavily.com/search",
                                   HTTP_METHOD_POST, headers, body.str());

  if (resp.err != ESP_OK || resp.status_code != 200) {
    ESP_LOGW(kTag, "Tavily search failed: status=%d", resp.status_code);
    return results;
  }

  cJSON *root = cJSON_Parse(resp.body.c_str());
  { std::string().swap(resp.body); }
  if (root == nullptr) return results;

  cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "results");
  if (items == nullptr || !cJSON_IsArray(items)) {
    cJSON_Delete(root);
    return results;
  }

  cJSON *item = nullptr;
  cJSON_ArrayForEach(item, items) {
    NewsResult nr;
    nr.title = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "title"));
    nr.snippet = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "content"));
    nr.url = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "url"));
    if (nr.snippet.size() > kMaxSnippetLen) nr.snippet.resize(kMaxSnippetLen);
    if (!nr.title.empty()) results.push_back(std::move(nr));
    if (static_cast<int>(results.size()) >= limit) break;
  }

  cJSON_Delete(root);
  ESP_LOGI(kTag, "Tavily: %d results for '%s'", (int)results.size(), query.c_str());
  return results;
}

std::vector<NewsResult> SearchBrave(const std::string &query, int limit,
                                     const std::string &api_key) {
  std::vector<NewsResult> results;

  // Minimal URL encoding — replace spaces with +.
  std::string q = query;
  for (size_t i = 0; i < q.size(); ++i) {
    if (q[i] == ' ') q[i] = '+';
  }

  std::ostringstream url;
  url << "https://api.search.brave.com/res/v1/web/search?q=" << q
      << "&count=" << limit << "&text_decorations=false&result_filter=news";

  std::vector<std::pair<std::string, std::string>> headers;
  headers.push_back({"X-Subscription-Token", api_key});
  headers.push_back({"Accept", "application/json"});

  HttpResponse resp = HttpRequest(url.str(), HTTP_METHOD_GET, headers);

  if (resp.err != ESP_OK || resp.status_code != 200) {
    ESP_LOGW(kTag, "Brave search failed: status=%d", resp.status_code);
    return results;
  }

  cJSON *root = cJSON_Parse(resp.body.c_str());
  { std::string().swap(resp.body); }
  if (root == nullptr) return results;

  // Brave returns { "news": { "results": [...] } } or { "web": { "results": [...] } }
  cJSON *container = cJSON_GetObjectItemCaseSensitive(root, "news");
  if (container == nullptr) container = cJSON_GetObjectItemCaseSensitive(root, "web");
  cJSON *items = (container != nullptr)
      ? cJSON_GetObjectItemCaseSensitive(container, "results")
      : nullptr;
  if (items == nullptr || !cJSON_IsArray(items)) {
    cJSON_Delete(root);
    return results;
  }

  cJSON *item = nullptr;
  cJSON_ArrayForEach(item, items) {
    NewsResult nr;
    nr.title = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "title"));
    nr.snippet = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "description"));
    nr.url = JsonToString(cJSON_GetObjectItemCaseSensitive(item, "url"));
    if (nr.snippet.size() > kMaxSnippetLen) nr.snippet.resize(kMaxSnippetLen);
    if (!nr.title.empty()) results.push_back(std::move(nr));
    if (static_cast<int>(results.size()) >= limit) break;
  }

  cJSON_Delete(root);
  ESP_LOGI(kTag, "Brave: %d results for '%s'", (int)results.size(), query.c_str());
  return results;
}

}  // namespace

std::vector<NewsResult> SearchNews(const std::string &query, int limit) {
  std::string api_key = config::NewsApiKey();
  if (api_key.empty()) {
    ESP_LOGW(kTag, "No news API key configured — skipping search");
    return {};
  }

  std::string provider = config::NewsProvider();
  if (provider == "brave") {
    return SearchBrave(query, limit, api_key);
  }
  return SearchTavily(query, limit, api_key);
}

std::string BuildNewsJson(const std::vector<NewsResult> &results) {
  std::ostringstream json;
  json << "[";
  for (size_t i = 0; i < results.size(); ++i) {
    if (i != 0) json << ",";
    json << "{\"title\":\"" << JsonEscape(results[i].title) << "\","
         << "\"snippet\":\"" << JsonEscape(results[i].snippet) << "\","
         << "\"url\":\"" << JsonEscape(results[i].url) << "\"}";
  }
  json << "]";
  return json.str();
}

}  // namespace survaiv
