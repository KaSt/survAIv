#include "model_registry.h"

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "cJSON.h"
#include "esp_log.h"
#include "http.h"
#include "json_util.h"
#include "types.h"

namespace survaiv {
namespace models {

static const char *kTag = "models";

// ── Built-in model catalog ──────────────────────────────────────────
//
// Cross-referenced from tx402.ai /v1/models and x402engine.app discovery.
// Prices are per-request estimates from provider catalogs (April 2026).
//
// Rating guide:
//   reasoning 5 = frontier CoT (DeepSeek-R1, GPT-5.x, Claude Opus)
//   reasoning 4 = strong analysis (DeepSeek-V3.2, Qwen3-235B, GPT-5.1)
//   reasoning 3 = solid general (Llama-4-Maverick, Gemini Flash, Mistral)
//   reasoning 2 = adequate (Llama-70B, Qwen-72B, GPT-OSS-120B)
//   reasoning 1 = basic (GPT-OSS-20B, nano models)

// clang-format off
static const ModelInfo kModels[] = {
  // ─── Available on BOTH providers (prefer tx402 — much cheaper) ───
  {
    "DeepSeek V3.2",
    "deepseek/deepseek-v3.2", "deepseek-v3.2",
    0.0005, 0.005,
    4, 4, 163, TaskComplexity::kStandard,
    "Best value for trading. Strong reasoning at rock-bottom price on tx402."
  },
  {
    "DeepSeek R1",
    "deepseek/deepseek-r1-0528", "deepseek-r1",
    0.002038, 0.01,
    5, 2, 164, TaskComplexity::kExpert,
    "Top-tier chain-of-thought. Use sparingly for high-stakes decisions."
  },
  {
    "Qwen3 235B",
    "qwen/qwen3-235b-a22b-2507", "qwen",
    0.000335, 0.004,
    4, 3, 131, TaskComplexity::kStandard,
    "Large MoE. Great analysis at very low cost on tx402."
  },
  {
    "Llama 4 Maverick",
    "meta-llama/llama-4-maverick", "llama-4-maverick",
    0.000511, 0.003,
    3, 4, 1050, TaskComplexity::kStandard,
    "Huge context window. Good for multi-market analysis."
  },
  {
    "Llama 3.3 70B",
    "meta-llama/llama-3.3-70b-instruct", "llama",
    0.00026, 0.002,
    2, 5, 131, TaskComplexity::kTrivial,
    "Fast and cheap. Good for simple market scans and status checks."
  },
  {
    "DeepSeek V3",
    "deepseek/deepseek-chat-v3.1", "deepseek",
    0.000625, 0.005,
    4, 3, 164, TaskComplexity::kStandard,
    "Predecessor to V3.2. Still strong, slightly more verbose."
  },
  {
    "Kimi K2.5",
    "moonshotai/kimi-k2.5", "kimi",
    0.002063, 0.03,
    4, 3, 262, TaskComplexity::kComplex,
    "Moonshot's flagship. Long context, strong reasoning. Expensive on engine."
  },
  {
    "Qwen 2.5 72B",
    "qwen/qwen-2.5-72b-instruct", nullptr,
    0.000207, 0.0,
    2, 4, 33, TaskComplexity::kTrivial,
    "tx402 only. Cheap fallback for simple tasks."
  },
  {
    "MiniMax M2",
    "minimax/minimax-m2", "minimax",
    0.000782, 0.01,
    3, 3, 196, TaskComplexity::kStandard,
    "Balanced mid-range. Decent at analysis."
  },

  // ─── tx402.ai exclusives ─────────────────────────────────────────
  {
    "GPT-OSS 120B",
    "openai/gpt-oss-120b", nullptr,
    0.00015, 0.0,
    2, 4, 131, TaskComplexity::kTrivial,
    "tx402 only. Ultra-cheap for basic scanning."
  },
  {
    "GPT-OSS 20B",
    "openai/gpt-oss-20b", nullptr,
    0.000107, 0.0,
    1, 5, 131, TaskComplexity::kTrivial,
    "tx402 only. Cheapest available. Only for trivial formatting."
  },
  {
    "Qwen3 Coder 30B",
    "qwen/qwen3-coder-30b-a3b-instruct", nullptr,
    0.000194, 0.0,
    2, 5, 262, TaskComplexity::kTrivial,
    "tx402 only. Code-focused but useful for structured JSON output."
  },

  // ─── x402engine.app exclusives ───────────────────────────────────
  {
    "GPT-5 Nano",
    nullptr, "gpt-5-nano",
    0.0, 0.002,
    2, 5, 131, TaskComplexity::kTrivial,
    "Engine only. Fast, cheap for simple checks."
  },
  {
    "Grok 4 Fast",
    nullptr, "grok-4-fast",
    0.0, 0.004,
    3, 5, 131, TaskComplexity::kStandard,
    "Engine only. Quick reasoning from xAI."
  },
  {
    "Gemini 2.5 Flash",
    nullptr, "gemini-flash",
    0.0, 0.009,
    3, 5, 131, TaskComplexity::kStandard,
    "Engine only. Google's fast model. Good balance."
  },
  {
    "GPT-4o Mini",
    nullptr, "gpt-4o-mini",
    0.0, 0.003,
    2, 5, 128, TaskComplexity::kTrivial,
    "Engine only. OpenAI's small model. Cheap and fast."
  },
  {
    "Mistral Large 3",
    nullptr, "mistral",
    0.0, 0.006,
    3, 4, 131, TaskComplexity::kStandard,
    "Engine only. European model. Good general reasoning."
  },
  {
    "Claude Haiku 4.5",
    nullptr, "claude-haiku",
    0.0, 0.02,
    3, 5, 200, TaskComplexity::kStandard,
    "Engine only. Anthropic's fast tier. Precise but pricey."
  },
  {
    "GPT-5.1",
    nullptr, "gpt-5.1",
    0.0, 0.035,
    5, 3, 128, TaskComplexity::kExpert,
    "Engine only. Frontier reasoning. Very expensive — use only when critical."
  },
  {
    "Gemini 3.1 Flash Lite",
    nullptr, "gemini-3.1-flash-lite",
    0.0, 0.003,
    2, 5, 131, TaskComplexity::kTrivial,
    "Engine only. Google's cheapest. Good for simple scans."
  },
};
// clang-format on

static constexpr int kModelCount =
    static_cast<int>(sizeof(kModels) / sizeof(kModels[0]));

// ── Dynamic model list ──────────────────────────────────────────────

static constexpr int kMaxDynamic = 40;
static std::mutex g_dyn_mutex;
static std::vector<DynamicModel> g_dynamic;       // guarded by g_dyn_mutex
static std::vector<ModelInfo> g_dynamic_views;     // parallel array of ModelInfo

// Build a ModelInfo that borrows pointers from a DynamicModel.
static ModelInfo MakeView(const DynamicModel &d) {
  ModelInfo m{};
  m.name = d.name;
  m.tx402_id = d.tx402_id[0] ? d.tx402_id : nullptr;
  m.engine_id = d.engine_id[0] ? d.engine_id : nullptr;
  m.tx402_price = d.tx402_price;
  m.engine_price = d.engine_price;
  m.reasoning = d.reasoning;
  m.speed = d.speed;
  m.context_k = d.context_k;
  m.min_task = (d.reasoning >= 4) ? TaskComplexity::kComplex
             : (d.reasoning >= 2) ? TaskComplexity::kStandard
                                  : TaskComplexity::kTrivial;
  m.notes = d.notes;
  return m;
}

// ── Public API ──────────────────────────────────────────────────────

int ModelCount() {
  std::lock_guard<std::mutex> lock(g_dyn_mutex);
  return kModelCount + static_cast<int>(g_dynamic_views.size());
}

const ModelInfo &GetModel(int i) {
  if (i < kModelCount) return kModels[i];
  std::lock_guard<std::mutex> lock(g_dyn_mutex);
  int di = i - kModelCount;
  if (di >= 0 && di < static_cast<int>(g_dynamic_views.size()))
    return g_dynamic_views[di];
  return kModels[0]; // safety fallback
}

double CheapestPrice(const ModelInfo &m) {
  if (m.tx402_price > 0 && m.engine_price > 0)
    return (m.tx402_price < m.engine_price) ? m.tx402_price : m.engine_price;
  if (m.tx402_price > 0) return m.tx402_price;
  return m.engine_price;
}

// Case-insensitive substring check.
static bool ContainsCI(const std::string &haystack, const std::string &needle) {
  if (needle.size() > haystack.size()) return false;
  for (size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
    bool match = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (tolower(static_cast<unsigned char>(haystack[i + j])) !=
          tolower(static_cast<unsigned char>(needle[j]))) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

// Extract the short suffix after the last '/'.
static std::string StripOrgPrefix(const std::string &id) {
  auto pos = id.rfind('/');
  return (pos != std::string::npos) ? id.substr(pos + 1) : id;
}

// Normalize separators so "gpt-oss:20b" matches "gpt-oss-20b".
static std::string NormalizeName(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out += (c == ':' || c == '_' || c == '.') ? '-'
                                              : static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// Strip "llm-" prefix from engine IDs.
static std::string StripLlmPrefix(const std::string &id) {
  if (id.size() > 4 && id[0] == 'l' && id[1] == 'l' && id[2] == 'm' && id[3] == '-')
    return id.substr(4);
  return id;
}

// Internal helper: search a list of ModelInfo entries for price.
static double LookupPriceIn(const ModelInfo *list, int count,
                            const std::string &query) {
  for (int i = 0; i < count; ++i) {
    const ModelInfo &m = list[i];
    if (m.tx402_id && NormalizeName(StripOrgPrefix(m.tx402_id)) == query)
      return CheapestPrice(m);
    if (m.engine_id && NormalizeName(m.engine_id) == query)
      return CheapestPrice(m);
  }
  for (int i = 0; i < count; ++i) {
    const ModelInfo &m = list[i];
    if (m.tx402_id) {
      std::string stripped = NormalizeName(StripOrgPrefix(m.tx402_id));
      if (ContainsCI(stripped, query) || ContainsCI(query, stripped))
        return CheapestPrice(m);
    }
    if (m.engine_id) {
      std::string eid = NormalizeName(m.engine_id);
      if (ContainsCI(eid, query) || ContainsCI(query, eid))
        return CheapestPrice(m);
    }
  }
  for (int i = 0; i < count; ++i) {
    const ModelInfo &m = list[i];
    std::string norm_name = NormalizeName(m.name);
    if (ContainsCI(norm_name, query) || ContainsCI(query, norm_name))
      return CheapestPrice(m);
  }
  return 0.0;
}

double LookupPrice(const std::string &model_name) {
  if (model_name.empty()) return 0.0;
  std::string query = NormalizeName(StripOrgPrefix(model_name));

  // Check dynamic list first.
  {
    std::lock_guard<std::mutex> lock(g_dyn_mutex);
    if (!g_dynamic_views.empty()) {
      double p = LookupPriceIn(g_dynamic_views.data(),
                                static_cast<int>(g_dynamic_views.size()), query);
      if (p > 0.0) return p;
    }
  }

  // Fall back to hardcoded.
  double p = LookupPriceIn(kModels, kModelCount, query);
  if (p > 0.0) return p;

  ESP_LOGW(kTag, "No price match for model '%s'", model_name.c_str());
  return 0.0;
}

static bool IsEngineUrl(const std::string &url) {
  return url.find("x402-gateway") != std::string::npos ||
         url.find("x402engine") != std::string::npos;
}

static bool IsClaw402Url(const std::string &url) {
  return url.find("claw402") != std::string::npos;
}

// Internal helper: run selection scoring on a list of ModelInfo entries.
static void ScoreModels(const ModelInfo *list, int count, bool use_engine,
                        TaskComplexity task, double budget_per_cycle,
                        double remaining_usdc,
                        const ModelInfo *&best, double &best_score) {
  uint8_t min_reasoning = 1;
  switch (task) {
    case TaskComplexity::kTrivial:  min_reasoning = 1; break;
    case TaskComplexity::kStandard: min_reasoning = 2; break;
    case TaskComplexity::kComplex:  min_reasoning = 3; break;
    case TaskComplexity::kExpert:   min_reasoning = 4; break;
  }

  for (int i = 0; i < count; ++i) {
    const ModelInfo &m = list[i];
    const char *mid = use_engine ? m.engine_id : m.tx402_id;
    double price = use_engine ? m.engine_price : m.tx402_price;
    if (mid == nullptr || price <= 0) continue;
    if (m.reasoning < min_reasoning) continue;
    if (price > budget_per_cycle * 0.25 && task != TaskComplexity::kExpert)
      continue;

    double quality = static_cast<double>(m.reasoning * m.reasoning);
    double score = quality / price;
    if (remaining_usdc < 2.0) score *= (1.0 / (price * 1000.0 + 1.0));
    if (static_cast<uint8_t>(m.min_task) <= static_cast<uint8_t>(task))
      score *= 1.2;
    if (score > best_score) {
      best_score = score;
      best = &m;
    }
  }
}

// Internal helper: find cheapest model in a list.
static void FindCheapest(const ModelInfo *list, int count, bool use_engine,
                         const ModelInfo *&best, double &cheapest) {
  for (int i = 0; i < count; ++i) {
    const ModelInfo &m = list[i];
    const char *mid = use_engine ? m.engine_id : m.tx402_id;
    double price = use_engine ? m.engine_price : m.tx402_price;
    if (mid == nullptr || price <= 0) continue;
    if (price < cheapest) {
      cheapest = price;
      best = &m;
    }
  }
}

ModelSelection SelectModel(const std::string &provider_url,
                           TaskComplexity task, double remaining_usdc,
                           int estimated_cycles) {
  bool use_engine = IsEngineUrl(provider_url);

  // claw402 is data-only, not LLM — don't select models for it.
  if (IsClaw402Url(provider_url)) {
    ESP_LOGW(kTag, "claw402 is data-only, not LLM provider");
    return {};
  }

  double budget_per_cycle = (estimated_cycles > 0)
                                ? remaining_usdc / estimated_cycles
                                : remaining_usdc;

  const ModelInfo *best = nullptr;
  double best_score = -1.0;

  // Score dynamic models first (may have fresher pricing).
  {
    std::lock_guard<std::mutex> lock(g_dyn_mutex);
    if (!g_dynamic_views.empty()) {
      ScoreModels(g_dynamic_views.data(),
                  static_cast<int>(g_dynamic_views.size()), use_engine, task,
                  budget_per_cycle, remaining_usdc, best, best_score);
    }
  }

  // Score hardcoded models.
  ScoreModels(kModels, kModelCount, use_engine, task, budget_per_cycle,
              remaining_usdc, best, best_score);

  // Fallback: cheapest across both lists.
  if (!best) {
    double cheapest = 1e9;
    {
      std::lock_guard<std::mutex> lock(g_dyn_mutex);
      if (!g_dynamic_views.empty()) {
        FindCheapest(g_dynamic_views.data(),
                     static_cast<int>(g_dynamic_views.size()), use_engine,
                     best, cheapest);
      }
    }
    FindCheapest(kModels, kModelCount, use_engine, best, cheapest);
  }

  ModelSelection sel{};
  if (best) {
    sel.model = best;
    sel.provider_url = provider_url.c_str();
    sel.model_id = use_engine ? best->engine_id : best->tx402_id;
    sel.price = use_engine ? best->engine_price : best->tx402_price;
    ESP_LOGI(kTag, "Selected: %s ($%.6f) for %s task [budget=$%.4f]",
             best->name, sel.price,
             task == TaskComplexity::kTrivial  ? "trivial"
             : task == TaskComplexity::kStandard ? "standard"
             : task == TaskComplexity::kComplex  ? "complex"
                                                  : "expert",
             remaining_usdc);
  }
  return sel;
}

// ── Dynamic registry refresh ────────────────────────────────────────

// Parse "$0.001234" -> 0.001234.
static double ParseDollarString(const char *s) {
  if (!s || !*s) return 0.0;
  if (*s == '$') ++s;
  char *end = nullptr;
  double v = strtod(s, &end);
  return (end != s) ? v : 0.0;
}

// Heuristic reasoning score from model name.
static uint8_t EstimateReasoning(const std::string &n) {
  if (ContainsCI(n, "r1") || ContainsCI(n, "o1") || ContainsCI(n, "o4"))
    return 5;
  if (ContainsCI(n, "deepseek-v3") || ContainsCI(n, "qwen3-235") ||
      ContainsCI(n, "gpt-5.") || ContainsCI(n, "gpt-5-") ||
      ContainsCI(n, "claude-opus") || ContainsCI(n, "grok-4"))
    return 4;
  if (ContainsCI(n, "gemini") || ContainsCI(n, "claude-sonnet") ||
      ContainsCI(n, "llama-4") || ContainsCI(n, "kimi") ||
      ContainsCI(n, "mistral-large") || ContainsCI(n, "glm"))
    return 3;
  if (ContainsCI(n, "70b") || ContainsCI(n, "72b") || ContainsCI(n, "120b") ||
      ContainsCI(n, "claude-haiku") || ContainsCI(n, "minimax"))
    return 2;
  return 1;
}

// Heuristic speed score from model name.
static uint8_t EstimateSpeed(const std::string &n) {
  if (ContainsCI(n, "r1") || ContainsCI(n, "o1") || ContainsCI(n, "o4") ||
      ContainsCI(n, "opus"))
    return 2;
  if (ContainsCI(n, "nano") || ContainsCI(n, "mini") || ContainsCI(n, "lite") ||
      ContainsCI(n, "flash") || ContainsCI(n, "fast") || ContainsCI(n, "20b"))
    return 5;
  if (ContainsCI(n, "70b") || ContainsCI(n, "72b") || ContainsCI(n, "haiku") ||
      ContainsCI(n, "grok-4-fast"))
    return 4;
  return 3;
}

// Safe strncpy that always null-terminates.
static void SafeCopy(char *dst, size_t dst_sz, const char *src) {
  if (!src) { dst[0] = '\0'; return; }
  size_t len = strlen(src);
  if (len >= dst_sz) len = dst_sz - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

// Temporary struct for merging entries from both providers.
struct MergeEntry {
  std::string norm_key;
  DynamicModel dm;
};

static std::string MakeNormKey(const std::string &id) {
  return NormalizeName(StripLlmPrefix(StripOrgPrefix(id)));
}

// Check if a dynamic model duplicates a hardcoded one.
static bool DuplicatesHardcoded(const std::string &norm_key) {
  for (int i = 0; i < kModelCount; ++i) {
    if (kModels[i].tx402_id) {
      if (MakeNormKey(kModels[i].tx402_id) == norm_key) return true;
    }
    if (kModels[i].engine_id) {
      if (MakeNormKey(kModels[i].engine_id) == norm_key) return true;
    }
  }
  return false;
}

void RefreshRegistry() {
  ESP_LOGI(kTag, "Refreshing dynamic model registry...");

  std::vector<MergeEntry> entries;
  entries.reserve(kMaxDynamic);

  // ── Fetch tx402.ai catalog ────────────────────────────────────
  {
    HttpResponse resp = HttpRequest("https://tx402.ai/v1/models", HTTP_METHOD_GET, {});
    if (resp.err == ESP_OK && resp.status_code == 200 && !resp.body.empty()) {
      cJSON *root = cJSON_Parse(resp.body.c_str());
      cJSON *data = root ? cJSON_GetObjectItemCaseSensitive(root, "data") : nullptr;
      if (cJSON_IsArray(data)) {
        int n = cJSON_GetArraySize(data);
        ESP_LOGI(kTag, "tx402: %d models in catalog", n);
        for (int i = 0; i < n && static_cast<int>(entries.size()) < kMaxDynamic; ++i) {
          cJSON *item = cJSON_GetArrayItem(data, i);
          cJSON *jid = cJSON_GetObjectItemCaseSensitive(item, "id");
          if (!cJSON_IsString(jid)) continue;

          std::string full_id = jid->valuestring;
          std::string norm_key = MakeNormKey(full_id);
          if (DuplicatesHardcoded(norm_key)) continue;

          // Check for existing merge entry.
          bool found = false;
          for (auto &e : entries) {
            if (e.norm_key == norm_key) {
              SafeCopy(e.dm.tx402_id, sizeof(e.dm.tx402_id), full_id.c_str());
              cJSON *pricing = cJSON_GetObjectItemCaseSensitive(item, "pricing");
              if (pricing) {
                cJSON *est = cJSON_GetObjectItemCaseSensitive(pricing, "estimated_per_request");
                if (cJSON_IsString(est))
                  e.dm.tx402_price = ParseDollarString(est->valuestring);
              }
              found = true;
              break;
            }
          }
          if (found) continue;

          MergeEntry me;
          me.norm_key = norm_key;
          memset(&me.dm, 0, sizeof(me.dm));

          std::string short_name = StripOrgPrefix(full_id);
          SafeCopy(me.dm.name, sizeof(me.dm.name), short_name.c_str());
          SafeCopy(me.dm.tx402_id, sizeof(me.dm.tx402_id), full_id.c_str());

          cJSON *pricing = cJSON_GetObjectItemCaseSensitive(item, "pricing");
          if (pricing) {
            cJSON *est = cJSON_GetObjectItemCaseSensitive(pricing, "estimated_per_request");
            if (cJSON_IsString(est))
              me.dm.tx402_price = ParseDollarString(est->valuestring);
          }

          cJSON *ctx = cJSON_GetObjectItemCaseSensitive(item, "context_window");
          if (cJSON_IsNumber(ctx))
            me.dm.context_k = static_cast<uint16_t>(ctx->valueint / 1000);
          else
            me.dm.context_k = 128;

          me.dm.reasoning = EstimateReasoning(norm_key);
          me.dm.speed = EstimateSpeed(norm_key);
          SafeCopy(me.dm.notes, sizeof(me.dm.notes), "Dynamic (tx402)");

          entries.push_back(me);
        }
      }
      if (root) cJSON_Delete(root);
    } else {
      ESP_LOGW(kTag, "tx402 catalog fetch failed: status=%d", resp.status_code);
    }
  }

  // ── Fetch x402engine.app catalog ──────────────────────────────
  {
    HttpResponse resp = HttpRequest(
        "https://x402engine.app/.well-known/x402.json", HTTP_METHOD_GET, {});
    if (resp.err == ESP_OK && resp.status_code == 200 && !resp.body.empty()) {
      cJSON *root = cJSON_Parse(resp.body.c_str());
      cJSON *categories = root ? cJSON_GetObjectItemCaseSensitive(root, "categories") : nullptr;
      cJSON *compute = categories ? cJSON_GetObjectItemCaseSensitive(categories, "compute") : nullptr;
      if (cJSON_IsArray(compute)) {
        int n = cJSON_GetArraySize(compute);
        ESP_LOGI(kTag, "x402engine: %d compute items in catalog", n);
        for (int i = 0; i < n && static_cast<int>(entries.size()) < kMaxDynamic; ++i) {
          cJSON *item = cJSON_GetArrayItem(compute, i);
          cJSON *jid = cJSON_GetObjectItemCaseSensitive(item, "id");
          if (!cJSON_IsString(jid)) continue;
          std::string eid = jid->valuestring;

          // Only items with "llm-" prefix are LLM models.
          if (eid.size() < 4 || eid.substr(0, 4) != "llm-") continue;

          std::string engine_short = eid.substr(4);
          std::string norm_key = NormalizeName(engine_short);
          if (DuplicatesHardcoded(norm_key)) continue;

          double price = 0.0;
          cJSON *jprice = cJSON_GetObjectItemCaseSensitive(item, "price");
          if (cJSON_IsString(jprice))
            price = ParseDollarString(jprice->valuestring);

          bool found = false;
          for (auto &e : entries) {
            if (e.norm_key == norm_key) {
              SafeCopy(e.dm.engine_id, sizeof(e.dm.engine_id), engine_short.c_str());
              e.dm.engine_price = price;
              SafeCopy(e.dm.notes, sizeof(e.dm.notes), "Dynamic (both)");
              found = true;
              break;
            }
          }
          if (found) continue;

          MergeEntry me;
          me.norm_key = norm_key;
          memset(&me.dm, 0, sizeof(me.dm));

          cJSON *jname = cJSON_GetObjectItemCaseSensitive(item, "name");
          if (cJSON_IsString(jname))
            SafeCopy(me.dm.name, sizeof(me.dm.name), jname->valuestring);
          else
            SafeCopy(me.dm.name, sizeof(me.dm.name), engine_short.c_str());

          SafeCopy(me.dm.engine_id, sizeof(me.dm.engine_id), engine_short.c_str());
          me.dm.engine_price = price;
          me.dm.context_k = 128;
          me.dm.reasoning = EstimateReasoning(norm_key);
          me.dm.speed = EstimateSpeed(norm_key);
          SafeCopy(me.dm.notes, sizeof(me.dm.notes), "Dynamic (engine)");

          entries.push_back(me);
        }
      }
      if (root) cJSON_Delete(root);
    } else {
      ESP_LOGW(kTag, "x402engine catalog fetch failed: status=%d", resp.status_code);
    }
  }

  // ── Swap into global list ─────────────────────────────────────
  {
    std::lock_guard<std::mutex> lock(g_dyn_mutex);
    g_dynamic.clear();
    g_dynamic_views.clear();
    g_dynamic.reserve(entries.size());
    g_dynamic_views.reserve(entries.size());
    for (auto &e : entries) {
      g_dynamic.push_back(e.dm);
    }
    for (auto &dm : g_dynamic) {
      g_dynamic_views.push_back(MakeView(dm));
    }
  }

  ESP_LOGI(kTag, "Dynamic registry: %d new models (total: %d + %d hardcoded)",
           static_cast<int>(entries.size()),
           static_cast<int>(entries.size()), kModelCount);
}

}  // namespace models
}  // namespace survaiv
