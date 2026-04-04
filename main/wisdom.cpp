#include "wisdom.h"

#include <cstring>
#include <ctime>
#include <sstream>

#include "cJSON.h"
#include "config.h"
#include "http.h"
#include "json_util.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

namespace survaiv {
namespace wisdom {

namespace {

constexpr const char *kTag = "wisdom";
constexpr const char *kNvsNs = "survaiv_cfg";
constexpr const char *kWisdomKey = "wisdom";
constexpr const char *kStatsKey = "wis_stats";
constexpr int kMaxDecisions = 30;
#if CONFIG_IDF_TARGET_ESP32S3
constexpr int kMaxWisdomBytes = 4000;
constexpr int kMaxQuestionLen = 120;
#elif !CONFIG_SURVAIV_ENABLE_OTA
constexpr int kMaxWisdomBytes = 2000;
constexpr int kMaxQuestionLen = 90;
#else
constexpr int kMaxWisdomBytes = 800;
constexpr int kMaxQuestionLen = 60;
#endif
constexpr int kMaxChecksPerCall = 3;
constexpr int64_t kCheckCooldownSec = 3600;

// ── State ──────────────────────────────────────────────────────────────────────

TrackedDecision g_ring[kMaxDecisions];
int g_head = 0;
int g_count = 0;

std::string g_wisdom;
WisdomStats g_stats;
SemaphoreHandle_t g_mutex = nullptr;
bool g_frozen = false;

constexpr int kMaxModels = 8;
ModelEpoch g_models[kMaxModels];
int g_model_count = 0;

// ── NVS helpers ────────────────────────────────────────────────────────────────

void LoadFromNvs() {
  nvs_handle_t h;
  if (nvs_open(kNvsNs, NVS_READONLY, &h) != ESP_OK) return;

  char buf[kMaxWisdomBytes + 1] = {};
  size_t len = sizeof(buf);
  if (nvs_get_str(h, kWisdomKey, buf, &len) == ESP_OK) {
    g_wisdom = buf;
  }

  size_t blob_sz = sizeof(WisdomStats);
  nvs_get_blob(h, kStatsKey, &g_stats, &blob_sz);

  int8_t frz = 0;
  if (nvs_get_i8(h, "wis_freeze", &frz) == ESP_OK) {
    g_frozen = (frz != 0);
  }

  nvs_close(h);
}

void SaveWisdomToNvs() {
  config::SetString(kWisdomKey, g_wisdom);
}

void SaveStatsToNvs() {
  nvs_handle_t h;
  if (nvs_open(kNvsNs, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_blob(h, kStatsKey, &g_stats, sizeof(WisdomStats));
  nvs_commit(h);
  nvs_close(h);
}

void RecordModelUsage(const std::string &name, int64_t epoch) {
  if (name.empty()) return;
  for (int i = 0; i < g_model_count; ++i) {
    if (std::strncmp(g_models[i].name, name.c_str(), 47) == 0) {
      ++g_models[i].decision_count;
      return;
    }
  }
  if (g_model_count < kMaxModels) {
    auto &m = g_models[g_model_count++];
    std::strncpy(m.name, name.c_str(), 47);
    m.name[47] = '\0';
    m.first_seen = epoch;
    m.decision_count = 1;
  }
}

// ── Category tracking ──────────────────────────────────────────────────────────

int FindOrAddCategory(const char *name) {
  if (name == nullptr || name[0] == '\0') return -1;
  for (int i = 0; i < 8; ++i) {
    if (g_stats.categories[i].name[0] != '\0' &&
        std::strncmp(g_stats.categories[i].name, name, 15) == 0) {
      return i;
    }
  }
  for (int i = 0; i < 8; ++i) {
    if (g_stats.categories[i].name[0] == '\0') {
      std::strncpy(g_stats.categories[i].name, name, 15);
      g_stats.categories[i].name[15] = '\0';
      return i;
    }
  }
  return -1;
}

// ── Wisdom text generation ─────────────────────────────────────────────────────

void RegenerateWisdom() {
  std::ostringstream ss;
  int rule = 1;

  // Overall accuracy
  if (g_stats.total > 0) {
    int pct = (g_stats.correct * 100) / g_stats.total;
    ss << rule++ << ". Overall: " << g_stats.correct << "/" << g_stats.total
       << " correct (" << pct << "%)";
    if (pct >= 60)
      ss << " — positive edge confirmed.\n";
    else if (pct >= 45)
      ss << " — near breakeven, tighten filters.\n";
    else
      ss << " — negative edge, reduce risk.\n";
  }

  // Hold vs buy
  if (g_stats.holds_total > 0 && g_stats.buys_total > 0) {
    int hp = (g_stats.holds_correct * 100) / g_stats.holds_total;
    int bp = (g_stats.buys_correct * 100) / g_stats.buys_total;
    ss << rule++ << ". Holds " << hp << "% vs Buys " << bp << "% correct";
    if (hp > bp + 10)
      ss << " — holding saves capital.\n";
    else if (bp > hp + 10)
      ss << " — buying edge is strong.\n";
    else
      ss << " — similar performance.\n";
  } else if (g_stats.holds_total > 0) {
    int hp = (g_stats.holds_correct * 100) / g_stats.holds_total;
    ss << rule++ << ". Holds: " << hp << "% correct (" << g_stats.holds_correct
       << "/" << g_stats.holds_total << ").\n";
  } else if (g_stats.buys_total > 0) {
    int bp = (g_stats.buys_correct * 100) / g_stats.buys_total;
    ss << rule++ << ". Buys: " << bp << "% correct (" << g_stats.buys_correct
       << "/" << g_stats.buys_total << ").\n";
  }

  // Best and worst categories (min 3 samples)
  int best_i = -1, worst_i = -1;
  int best_pct = -1, worst_pct = 101;
  for (int i = 0; i < 8; ++i) {
    auto &c = g_stats.categories[i];
    if (c.total < 3) continue;
    int pct = (c.correct * 100) / c.total;
    if (pct > best_pct) {
      best_pct = pct;
      best_i = i;
    }
    if (pct < worst_pct) {
      worst_pct = pct;
      worst_i = i;
    }
  }
  if (best_i >= 0) {
    auto &c = g_stats.categories[best_i];
    ss << rule++ << ". " << c.name << ": " << best_pct << "% (" << c.correct
       << "/" << c.total << ") — strongest edge.\n";
  }
  if (worst_i >= 0 && worst_i != best_i) {
    auto &c = g_stats.categories[worst_i];
    ss << rule++ << ". " << c.name << ": " << worst_pct << "% (" << c.correct
       << "/" << c.total << ") — weakest, reduce size.\n";
  }

  // Strong signals in other categories (min 5 samples)
  for (int i = 0; i < 8; ++i) {
    if (i == best_i || i == worst_i) continue;
    auto &c = g_stats.categories[i];
    if (c.total < 5) continue;
    int pct = (c.correct * 100) / c.total;
    if (pct >= 70) {
      ss << rule++ << ". " << c.name << " at " << pct
         << "% — reliable, lean in.\n";
    } else if (pct <= 30) {
      ss << rule++ << ". " << c.name << " at " << pct
         << "% — consistently wrong, invert or avoid.\n";
    }
  }

  std::string text = ss.str();
  if (text.size() > kMaxWisdomBytes) {
    text.resize(kMaxWisdomBytes);
    auto pos = text.rfind('\n');
    if (pos != std::string::npos && pos > 0) text.resize(pos + 1);
  }
  g_wisdom = text;
}

}  // namespace

// ── Public API ─────────────────────────────────────────────────────────────────

void Init() {
  g_mutex = xSemaphoreCreateMutex();
  LoadFromNvs();
  ESP_LOGI(kTag, "Loaded: %zu B wisdom, %u evaluated", g_wisdom.size(),
           g_stats.total);
}

void TrackDecision(const std::string &market_id, const std::string &question,
                   const std::string &category,
                   const std::string &decision_type, const std::string &signal,
                   const std::string &model_name,
                   double yes_price, double confidence, double edge_bps) {
  xSemaphoreTake(g_mutex, portMAX_DELAY);

  int idx = (g_head + g_count) % kMaxDecisions;
  if (g_count == kMaxDecisions) {
    g_head = (g_head + 1) % kMaxDecisions;
  } else {
    ++g_count;
  }

  TrackedDecision &d = g_ring[idx];
  time_t now;
  time(&now);
  d.epoch = static_cast<int64_t>(now);
  d.market_id = market_id;
  d.question =
      question.size() <= kMaxQuestionLen ? question : question.substr(0, kMaxQuestionLen);
  d.category = category;
  d.decision_type = decision_type;
  d.signal = signal;
  d.model_name = model_name;
  d.yes_price_at_decision = yes_price;
  d.confidence = confidence;
  d.edge_bps = edge_bps;
  d.resolved = false;
  d.outcome_yes = false;
  d.final_yes_price = 0.0;
  d.checked = false;
  d.last_check_epoch = 0;

  RecordModelUsage(model_name, d.epoch);

  xSemaphoreGive(g_mutex);

  ESP_LOGI(kTag, "Track %s %s p=%.2f edge=%.0f model=%s", decision_type.c_str(),
           market_id.c_str(), yes_price, edge_bps, model_name.c_str());
}

void CheckOutcomes() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);

  time_t now;
  time(&now);
  int64_t now_epoch = static_cast<int64_t>(now);

  // Collect up to kMaxChecksPerCall targets while holding mutex
  int n_targets = 0;
  int indices[kMaxChecksPerCall];
  std::string mids[kMaxChecksPerCall];

  for (int i = 0; i < g_count && n_targets < kMaxChecksPerCall; ++i) {
    int ri = (g_head + i) % kMaxDecisions;
    TrackedDecision &d = g_ring[ri];
    if (d.resolved || d.market_id.empty()) continue;
    if (now_epoch - d.last_check_epoch < kCheckCooldownSec) continue;
    indices[n_targets] = ri;
    mids[n_targets] = d.market_id;
    ++n_targets;
  }

  xSemaphoreGive(g_mutex);

  // Fetch each market outside the lock
  for (int t = 0; t < n_targets; ++t) {
    std::string url =
        "https://gamma-api.polymarket.com/markets/" + mids[t];
    HttpResponse resp = HttpRequest(url, HTTP_METHOD_GET, {});

    xSemaphoreTake(g_mutex, portMAX_DELAY);

    TrackedDecision &d = g_ring[indices[t]];

    // Guard against ring overwrite during HTTP call
    if (d.market_id != mids[t]) {
      xSemaphoreGive(g_mutex);
      continue;
    }

    d.last_check_epoch = now_epoch;

    if (resp.err != ESP_OK || resp.status_code != 200) {
      ESP_LOGW(kTag, "Check %s failed: %d", mids[t].c_str(),
               resp.status_code);
      xSemaphoreGive(g_mutex);
      continue;
    }

    cJSON *root = cJSON_Parse(resp.body.c_str());
    if (root == nullptr) {
      xSemaphoreGive(g_mutex);
      continue;
    }

    cJSON *closed_j = cJSON_GetObjectItemCaseSensitive(root, "closed");
    bool is_closed = closed_j != nullptr && cJSON_IsTrue(closed_j);

    std::vector<double> prices = ParseStringifiedArrayToDoubles(
        cJSON_GetObjectItemCaseSensitive(root, "outcomePrices"));
    double cur_yes = prices.size() >= 1 ? prices[0] : 0.0;

    if (is_closed && prices.size() >= 2) {
      d.resolved = true;
      d.checked = false;
      d.final_yes_price = cur_yes;
      // ["1","0"] → YES won, ["0","1"] → NO won
      d.outcome_yes = (prices[0] > 0.9 && prices[1] < 0.1);
      ESP_LOGI(kTag, "Resolved %s → %s (p=%.2f)", d.market_id.c_str(),
               d.outcome_yes ? "YES" : "NO", cur_yes);
    } else if (cur_yes > 0.0) {
      double delta = cur_yes - d.yes_price_at_decision;
      double abs_delta = delta < 0.0 ? -delta : delta;
      if (abs_delta > 0.15) {
        d.resolved = true;
        d.checked = false;
        d.final_yes_price = cur_yes;
        d.outcome_yes = (delta > 0.0);
        ESP_LOGI(kTag, "Price-resolved %s Δ%.2f (%.2f→%.2f)",
                 d.market_id.c_str(), delta, d.yes_price_at_decision,
                 cur_yes);
      }
    }

    cJSON_Delete(root);
    xSemaphoreGive(g_mutex);
  }
}

void EvaluateAndUpdateWisdom() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);

  bool any = false;

  for (int i = 0; i < g_count; ++i) {
    int ri = (g_head + i) % kMaxDecisions;
    TrackedDecision &d = g_ring[ri];
    if (!d.resolved || d.checked) continue;
    d.checked = true;
    any = true;

    bool is_hold = (d.decision_type == "hold");
    bool is_buy = (d.decision_type.find("buy") != std::string::npos);
    bool correct = false;

    if (is_hold) {
      double move = d.final_yes_price - d.yes_price_at_decision;
      if (move < 0.0) move = -move;
      correct = (move <= 0.20);
      ++g_stats.holds_total;
      if (correct) ++g_stats.holds_correct;
    } else if (is_buy) {
      bool yes_side = (d.decision_type.find("yes") != std::string::npos);
      if (yes_side) {
        correct = d.outcome_yes ||
                  (d.final_yes_price - d.yes_price_at_decision > 0.10);
      } else {
        correct = !d.outcome_yes ||
                  (d.yes_price_at_decision - d.final_yes_price > 0.10);
      }
      ++g_stats.buys_total;
      if (correct) ++g_stats.buys_correct;
    }

    ++g_stats.total;
    if (correct) ++g_stats.correct;

    int ci = FindOrAddCategory(d.category.c_str());
    if (ci >= 0) {
      ++g_stats.categories[ci].total;
      if (correct) ++g_stats.categories[ci].correct;
    }

    ESP_LOGI(kTag, "Eval %s %s → %s (%u/%u)", d.decision_type.c_str(),
             d.market_id.c_str(), correct ? "OK" : "WRONG", g_stats.correct,
             g_stats.total);
  }

  if (any) {
    SaveStatsToNvs();
    if (!g_frozen) {
      RegenerateWisdom();
      SaveWisdomToNvs();
      ESP_LOGI(kTag, "Wisdom updated (%zu B)", g_wisdom.size());
    } else {
      ESP_LOGI(kTag, "Stats updated (wisdom frozen)");
    }
  }

  xSemaphoreGive(g_mutex);
}

std::string GetWisdom() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  std::string result = g_wisdom;
  xSemaphoreGive(g_mutex);
  return result;
}

bool IsFrozen() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  bool f = g_frozen;
  xSemaphoreGive(g_mutex);
  return f;
}

void SetFrozen(bool frozen) {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_frozen = frozen;
  xSemaphoreGive(g_mutex);
  config::SetInt("wis_freeze", frozen ? 1 : 0);
  ESP_LOGI(kTag, "Learning %s", frozen ? "frozen" : "resumed");
}

std::string StatsJson() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);

  std::ostringstream ss;
  ss << "{\"total_tracked\":" << g_count
     << ",\"total_resolved\":" << g_stats.total
     << ",\"total_correct\":" << g_stats.correct
     << ",\"hold_resolved\":" << g_stats.holds_total
     << ",\"hold_correct\":" << g_stats.holds_correct
     << ",\"buy_resolved\":" << g_stats.buys_total
     << ",\"buy_correct\":" << g_stats.buys_correct
     << ",\"frozen\":" << (g_frozen ? "true" : "false")
     << ",\"wisdom_text\":\"" << JsonEscape(g_wisdom)
     << "\",\"categories\":[";
  bool first = true;
  for (int i = 0; i < 8; ++i) {
    if (g_stats.categories[i].name[0] == '\0') continue;
    if (!first) ss << ',';
    first = false;
    ss << "{\"name\":\"" << JsonEscape(g_stats.categories[i].name)
       << "\",\"total\":" << g_stats.categories[i].total
       << ",\"correct\":" << g_stats.categories[i].correct << '}';
  }

  ss << "],\"models\":[";
  first = true;
  for (int i = 0; i < g_model_count; ++i) {
    if (!first) ss << ',';
    first = false;
    ss << "{\"name\":\"" << JsonEscape(g_models[i].name)
       << "\",\"first_seen\":" << g_models[i].first_seen
       << ",\"decisions\":" << g_models[i].decision_count << '}';
  }
  ss << "]}";

  xSemaphoreGive(g_mutex);
  return ss.str();
}

// ── Export / Import ─────────────────────────────────────────────────────────────

std::string ExportKnowledge() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);

  std::ostringstream ss;
  time_t now;
  time(&now);

  ss << "{\"format\":\"survaiv-knowledge-v1\",\"exported_at\":" << static_cast<int64_t>(now);

  // Wisdom text + stats.
  ss << ",\"wisdom_text\":\"" << JsonEscape(g_wisdom) << '"'
     << ",\"stats\":{\"total\":" << g_stats.total
     << ",\"correct\":" << g_stats.correct
     << ",\"holds_total\":" << g_stats.holds_total
     << ",\"holds_correct\":" << g_stats.holds_correct
     << ",\"buys_total\":" << g_stats.buys_total
     << ",\"buys_correct\":" << g_stats.buys_correct
     << ",\"categories\":[";

  bool first = true;
  for (int i = 0; i < 8; ++i) {
    if (g_stats.categories[i].name[0] == '\0') continue;
    if (!first) ss << ',';
    first = false;
    ss << "{\"n\":\"" << JsonEscape(g_stats.categories[i].name)
       << "\",\"t\":" << g_stats.categories[i].total
       << ",\"c\":" << g_stats.categories[i].correct << '}';
  }
  ss << "]}";

  // Model history.
  ss << ",\"models\":[";
  first = true;
  for (int i = 0; i < g_model_count; ++i) {
    if (!first) ss << ',';
    first = false;
    ss << "{\"name\":\"" << JsonEscape(g_models[i].name)
       << "\",\"first_seen\":" << g_models[i].first_seen
       << ",\"decisions\":" << g_models[i].decision_count << '}';
  }
  ss << "]";

  // Tracked decisions ring buffer.
  ss << ",\"decisions\":[";
  first = true;
  for (int i = 0; i < g_count; ++i) {
    int ri = (g_head + i) % kMaxDecisions;
    const TrackedDecision &d = g_ring[ri];
    if (!first) ss << ',';
    first = false;
    ss << "{\"e\":" << d.epoch
       << ",\"m\":\"" << JsonEscape(d.market_id) << '"'
       << ",\"q\":\"" << JsonEscape(d.question) << '"'
       << ",\"cat\":\"" << JsonEscape(d.category) << '"'
       << ",\"dt\":\"" << JsonEscape(d.decision_type) << '"'
       << ",\"sig\":\"" << JsonEscape(d.signal) << '"'
       << ",\"mdl\":\"" << JsonEscape(d.model_name) << '"'
       << ",\"yp\":" << d.yes_price_at_decision
       << ",\"conf\":" << d.confidence
       << ",\"eb\":" << d.edge_bps
       << ",\"res\":" << (d.resolved ? "true" : "false")
       << ",\"oy\":" << (d.outcome_yes ? "true" : "false")
       << ",\"fp\":" << d.final_yes_price
       << ",\"chk\":" << (d.checked ? "true" : "false")
       << ",\"lce\":" << d.last_check_epoch << '}';
  }
  ss << "]}";

  xSemaphoreGive(g_mutex);
  return ss.str();
}

bool ImportKnowledge(const std::string &json) {
  cJSON *root = cJSON_Parse(json.c_str());
  if (!root) {
    ESP_LOGE(kTag, "Import: invalid JSON");
    return false;
  }

  // Validate format marker.
  cJSON *fmt = cJSON_GetObjectItemCaseSensitive(root, "format");
  if (!fmt || !cJSON_IsString(fmt) ||
      std::strncmp(fmt->valuestring, "survaiv-knowledge-v1", 20) != 0) {
    ESP_LOGE(kTag, "Import: unknown format");
    cJSON_Delete(root);
    return false;
  }

  xSemaphoreTake(g_mutex, portMAX_DELAY);

  // Wisdom text.
  cJSON *wt = cJSON_GetObjectItemCaseSensitive(root, "wisdom_text");
  if (wt && cJSON_IsString(wt)) {
    g_wisdom = wt->valuestring;
    if (g_wisdom.size() > kMaxWisdomBytes) g_wisdom.resize(kMaxWisdomBytes);
  }

  // Stats.
  cJSON *stats = cJSON_GetObjectItemCaseSensitive(root, "stats");
  if (stats) {
    g_stats = WisdomStats{};
    g_stats.total = static_cast<uint16_t>(JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(stats, "total")));
    g_stats.correct = static_cast<uint16_t>(JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(stats, "correct")));
    g_stats.holds_total = static_cast<uint16_t>(JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(stats, "holds_total")));
    g_stats.holds_correct = static_cast<uint16_t>(JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(stats, "holds_correct")));
    g_stats.buys_total = static_cast<uint16_t>(JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(stats, "buys_total")));
    g_stats.buys_correct = static_cast<uint16_t>(JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(stats, "buys_correct")));

    cJSON *cats = cJSON_GetObjectItemCaseSensitive(stats, "categories");
    int ci = 0;
    cJSON *cat = nullptr;
    cJSON_ArrayForEach(cat, cats) {
      if (ci >= 8) break;
      cJSON *n = cJSON_GetObjectItemCaseSensitive(cat, "n");
      if (n && cJSON_IsString(n)) {
        std::strncpy(g_stats.categories[ci].name, n->valuestring, 15);
        g_stats.categories[ci].name[15] = '\0';
      }
      g_stats.categories[ci].total = static_cast<uint16_t>(
          JsonToDouble(cJSON_GetObjectItemCaseSensitive(cat, "t")));
      g_stats.categories[ci].correct = static_cast<uint16_t>(
          JsonToDouble(cJSON_GetObjectItemCaseSensitive(cat, "c")));
      ++ci;
    }
  }

  // Model history.
  cJSON *models = cJSON_GetObjectItemCaseSensitive(root, "models");
  g_model_count = 0;
  cJSON *mdl = nullptr;
  cJSON_ArrayForEach(mdl, models) {
    if (g_model_count >= kMaxModels) break;
    cJSON *nm = cJSON_GetObjectItemCaseSensitive(mdl, "name");
    if (nm && cJSON_IsString(nm)) {
      auto &m = g_models[g_model_count];
      std::strncpy(m.name, nm->valuestring, 47);
      m.name[47] = '\0';
      m.first_seen = static_cast<int64_t>(JsonToDouble(
          cJSON_GetObjectItemCaseSensitive(mdl, "first_seen")));
      m.decision_count = static_cast<uint16_t>(JsonToDouble(
          cJSON_GetObjectItemCaseSensitive(mdl, "decisions")));
      ++g_model_count;
    }
  }

  // Decisions ring.
  cJSON *decs = cJSON_GetObjectItemCaseSensitive(root, "decisions");
  g_head = 0;
  g_count = 0;
  cJSON *dj = nullptr;
  cJSON_ArrayForEach(dj, decs) {
    if (g_count >= kMaxDecisions) break;
    TrackedDecision &d = g_ring[g_count];
    d.epoch = static_cast<int64_t>(JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(dj, "e")));
    d.market_id = JsonToString(cJSON_GetObjectItemCaseSensitive(dj, "m"));
    d.question = JsonToString(cJSON_GetObjectItemCaseSensitive(dj, "q"));
    d.category = JsonToString(cJSON_GetObjectItemCaseSensitive(dj, "cat"));
    d.decision_type = JsonToString(cJSON_GetObjectItemCaseSensitive(dj, "dt"));
    d.signal = JsonToString(cJSON_GetObjectItemCaseSensitive(dj, "sig"));
    d.model_name = JsonToString(cJSON_GetObjectItemCaseSensitive(dj, "mdl"));
    d.yes_price_at_decision = JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(dj, "yp"));
    d.confidence = JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(dj, "conf"));
    d.edge_bps = JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(dj, "eb"));
    cJSON *res = cJSON_GetObjectItemCaseSensitive(dj, "res");
    d.resolved = res && cJSON_IsTrue(res);
    cJSON *oy = cJSON_GetObjectItemCaseSensitive(dj, "oy");
    d.outcome_yes = oy && cJSON_IsTrue(oy);
    d.final_yes_price = JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(dj, "fp"));
    cJSON *chk = cJSON_GetObjectItemCaseSensitive(dj, "chk");
    d.checked = chk && cJSON_IsTrue(chk);
    d.last_check_epoch = static_cast<int64_t>(JsonToDouble(
        cJSON_GetObjectItemCaseSensitive(dj, "lce")));
    ++g_count;
  }

  // Persist imported data.
  SaveStatsToNvs();
  SaveWisdomToNvs();

  ESP_LOGI(kTag, "Imported: %u stats, %d decisions, %d models, %zu B wisdom",
           g_stats.total, g_count, g_model_count, g_wisdom.size());

  xSemaphoreGive(g_mutex);
  cJSON_Delete(root);
  return true;
}

}  // namespace wisdom
}  // namespace survaiv
