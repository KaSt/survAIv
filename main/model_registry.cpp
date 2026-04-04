#include "model_registry.h"

#include <cstring>

#include "esp_log.h"

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

// ── Public API ──────────────────────────────────────────────────────

int ModelCount() { return kModelCount; }

const ModelInfo &GetModel(int i) { return kModels[i]; }

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

// Extract the short suffix after the last '/' (e.g. "openai/gpt-oss-20b" → "gpt-oss-20b").
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

double LookupPrice(const std::string &model_name) {
  if (model_name.empty()) return 0.0;

  std::string query = NormalizeName(StripOrgPrefix(model_name));

  // Pass 1: exact match against normalized stripped IDs.
  for (int i = 0; i < kModelCount; ++i) {
    const ModelInfo &m = kModels[i];
    if (m.tx402_id && NormalizeName(StripOrgPrefix(m.tx402_id)) == query)
      return CheapestPrice(m);
    if (m.engine_id && NormalizeName(m.engine_id) == query)
      return CheapestPrice(m);
  }

  // Pass 2: the query is a substring of a known ID, or vice versa.
  // E.g. "deepseek-v3" matches "deepseek-v3.2", "gpt-oss:20b" matches "gpt-oss-20b".
  for (int i = 0; i < kModelCount; ++i) {
    const ModelInfo &m = kModels[i];
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

  // Pass 3: check against the human-readable name (e.g. "DeepSeek V3.2").
  for (int i = 0; i < kModelCount; ++i) {
    const ModelInfo &m = kModels[i];
    std::string norm_name = NormalizeName(m.name);
    if (ContainsCI(norm_name, query) || ContainsCI(query, norm_name))
      return CheapestPrice(m);
  }

  ESP_LOGW(kTag, "No price match for model '%s' — using flat estimate", model_name.c_str());
  return 0.0;
}

static bool IsEngineUrl(const std::string &url) {
  return url.find("x402-gateway") != std::string::npos ||
         url.find("x402engine") != std::string::npos;
}

ModelSelection SelectModel(const std::string &provider_url,
                           TaskComplexity task, double remaining_usdc,
                           int estimated_cycles) {
  bool use_engine = IsEngineUrl(provider_url);

  // Budget per remaining cycle (with safety margin).
  double budget_per_cycle = (estimated_cycles > 0)
                                ? remaining_usdc / estimated_cycles
                                : remaining_usdc;

  const ModelInfo *best = nullptr;
  double best_score = -1.0;

  for (int i = 0; i < kModelCount; ++i) {
    const ModelInfo &m = kModels[i];

    // Skip models not available on the configured provider.
    const char *mid = use_engine ? m.engine_id : m.tx402_id;
    double price = use_engine ? m.engine_price : m.tx402_price;
    if (mid == nullptr || price <= 0) continue;

    // Skip models below required reasoning floor for the task.
    uint8_t min_reasoning = 1;
    switch (task) {
      case TaskComplexity::kTrivial:  min_reasoning = 1; break;
      case TaskComplexity::kStandard: min_reasoning = 2; break;
      case TaskComplexity::kComplex:  min_reasoning = 3; break;
      case TaskComplexity::kExpert:   min_reasoning = 4; break;
    }
    if (m.reasoning < min_reasoning) continue;

    // Skip if this single request would eat more than 25% of per-cycle budget.
    if (price > budget_per_cycle * 0.25 &&
        task != TaskComplexity::kExpert) continue;

    // Score: reasoning quality weighted by cost efficiency.
    // Higher reasoning is better, lower price is better.
    // We use reasoning^2 / price to favor quality while still penalizing cost.
    double quality = static_cast<double>(m.reasoning * m.reasoning);
    double score = quality / price;

    // Budget pressure: when cash is critically low, boost score of cheap models.
    if (remaining_usdc < 2.0) {
      score *= (1.0 / (price * 1000.0 + 1.0));
    }

    // Bonus for models that match or exceed the task level (not overkill).
    if (static_cast<uint8_t>(m.min_task) <= static_cast<uint8_t>(task)) {
      score *= 1.2;
    }

    if (score > best_score) {
      best_score = score;
      best = &m;
    }
  }

  // Fallback: if nothing matched (budget too tight), pick the absolute cheapest.
  if (!best) {
    double cheapest = 1e9;
    for (int i = 0; i < kModelCount; ++i) {
      const ModelInfo &m = kModels[i];
      const char *mid = use_engine ? m.engine_id : m.tx402_id;
      double price = use_engine ? m.engine_price : m.tx402_price;
      if (mid == nullptr || price <= 0) continue;
      if (price < cheapest) {
        cheapest = price;
        best = &m;
      }
    }
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

}  // namespace models
}  // namespace survaiv
