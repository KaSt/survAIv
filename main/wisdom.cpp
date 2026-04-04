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
constexpr int kMaxWisdomBytes = 800;
constexpr int kMaxQuestionLen = 60;
constexpr int kMaxChecksPerCall = 3;
constexpr int64_t kCheckCooldownSec = 3600;

// ── State ──────────────────────────────────────────────────────────────────────

TrackedDecision g_ring[kMaxDecisions];
int g_head = 0;
int g_count = 0;

std::string g_wisdom;
WisdomStats g_stats;
SemaphoreHandle_t g_mutex = nullptr;

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
  d.yes_price_at_decision = yes_price;
  d.confidence = confidence;
  d.edge_bps = edge_bps;
  d.resolved = false;
  d.outcome_yes = false;
  d.final_yes_price = 0.0;
  d.checked = false;
  d.last_check_epoch = 0;

  xSemaphoreGive(g_mutex);

  ESP_LOGI(kTag, "Track %s %s p=%.2f edge=%.0f", decision_type.c_str(),
           market_id.c_str(), yes_price, edge_bps);
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
    RegenerateWisdom();
    SaveStatsToNvs();
    SaveWisdomToNvs();
    ESP_LOGI(kTag, "Wisdom updated (%zu B)", g_wisdom.size());
  }

  xSemaphoreGive(g_mutex);
}

std::string GetWisdom() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  std::string result = g_wisdom;
  xSemaphoreGive(g_mutex);
  return result;
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
  ss << "]}";

  xSemaphoreGive(g_mutex);
  return ss.str();
}

}  // namespace wisdom
}  // namespace survaiv
