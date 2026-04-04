#pragma once

#include <cstdint>
#include <string>

namespace survaiv {
namespace models {

// How complex is the task the agent needs to perform.
enum class TaskComplexity : uint8_t {
  kTrivial = 0,   // Status checks, simple formatting
  kStandard = 1,  // Market scanning, position evaluation
  kComplex = 2,   // Multi-factor trade decisions, risk analysis
  kExpert = 3     // Deep reasoning, contrarian analysis, edge-case logic
};

struct ModelInfo {
  const char *name;        // Human-readable name
  const char *tx402_id;    // ID on tx402.ai (nullptr if unavailable)
  const char *engine_id;   // ID on x402engine.app (nullptr if unavailable)
  double tx402_price;      // $/request on tx402.ai (0 if N/A)
  double engine_price;     // $/request on x402engine (0 if N/A)
  uint8_t reasoning;       // 1–5: logical/analytical depth
  uint8_t speed;           // 1–5: response latency (5=fastest)
  uint16_t context_k;      // Context window in thousands of tokens
  TaskComplexity min_task; // Minimum complexity this model handles well
  const char *notes;       // One-liner: best use case
};

// Heap-allocated model entry for dynamically discovered models.
struct DynamicModel {
  char name[48];
  char tx402_id[64];
  char engine_id[48];
  double tx402_price;
  double engine_price;
  uint16_t context_k;
  uint8_t reasoning;
  uint8_t speed;
  char notes[64];
};

struct ModelSelection {
  const ModelInfo *model;
  const char *provider_url;  // "https://tx402.ai/v1" or gateway URL
  const char *model_id;      // Provider-specific model ID
  double price;              // Price per request
};

// Number of models in the registry (hardcoded + dynamic).
int ModelCount();

// Access model at index i (0..ModelCount()-1).
// For i < hardcoded count, returns the static entry.
// For i >= hardcoded count, returns a temporary ModelInfo pointing into
// the dynamic list (valid until next RefreshRegistry call).
const ModelInfo &GetModel(int i);

// Select the best model for a task, given the provider base URL and remaining
// budget. Returns the cheapest model that meets the task's reasoning needs.
// When budget_pressure is high (low remaining USDC), it biases toward cheaper
// models even if they are slightly less capable.
//
// provider_url: the configured base URL (to pick tx402 vs engine IDs).
// task: complexity of the current decision.
// remaining_usdc: agent's remaining cash for inference.
// estimated_cycles: how many more cycles the agent wants to survive.
ModelSelection SelectModel(const std::string &provider_url,
                           TaskComplexity task,
                           double remaining_usdc,
                           int estimated_cycles);

// Convenience: return the cheapest price for a given model across providers.
double CheapestPrice(const ModelInfo &m);

// Look up the estimated per-request price for a model name string (e.g.
// "gpt-oss-20b", "openai/gpt-oss-20b", "deepseek-v3.2"). Matches against
// all known tx402 and engine IDs with fuzzy suffix matching. Returns the
// cheapest known price, or 0 if no match is found.
double LookupPrice(const std::string &model_name);

// Fetch model/price catalogs from tx402.ai and x402engine.app and build
// a dynamic model list. Safe to call periodically (every ~24h). Failures
// are non-fatal — the hardcoded list is always available as fallback.
void RefreshRegistry();

}  // namespace models
}  // namespace survaiv
