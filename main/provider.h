#pragma once

// ── x402 Provider Adapter Interface ─────────────────────────────────
//
// Each x402 pay-per-use provider is described by a lightweight adapter
// struct with function pointers.  Adding a new provider means:
//
//   1. Define a static const LlmAdapter (or DataAdapter) in provider.cpp
//      with the six required fields / callbacks.
//   2. Call RegisterLlmAdapter(&kMyAdapter) in providers::Init().
//   3. Done — model_registry and agent will discover it automatically.
//
// No virtual tables, no heap allocation for the adapters themselves.
// Designed for ESP32 with ≤400 KB SRAM.

#include <cstdint>
#include <string>

namespace survaiv {
namespace providers {

// ── Auth method ─────────────────────────────────────────────────────

enum class AuthMethod : uint8_t {
  kApiKey = 0,  // Bearer token in Authorization header
  kX402   = 1,  // x402 wallet micropayment (402 → sign → retry)
  kNone   = 2,  // No authentication required
};

// ── Catalog model (parsed from a provider's discovery endpoint) ─────

struct CatalogModel {
  char id[64];            // Provider-specific model ID (e.g. "openai/gpt-oss-20b")
  char display_name[48];  // Human-readable name
  double price_per_req;   // Estimated $/request
  uint32_t context_k;     // Context window in thousands of tokens
};

// ── LLM Provider Adapter ────────────────────────────────────────────
//
// Describes how to talk to one LLM inference provider.
// All pointers are to static data or functions — no ownership.

struct LlmAdapter {
  // Identity
  const char *name;          // Short key: "tx402", "x402engine", "custom"
  const char *display_name;  // UI label: "tx402.ai", "x402engine.app"

  // Defaults
  const char *default_base_url;  // e.g. "https://tx402.ai/v1"
  const char *catalog_url;       // Discovery endpoint (nullptr = none)

  // Authentication
  AuthMethod auth;

  // Whether the model ID goes into the JSON body ("model":"…").
  // If false, the model ID is encoded in the URL path instead.
  bool model_in_body;

  // ── Callbacks ─────────────────────────────────────────────────

  // Return true if `url` belongs to this provider.
  bool (*matches_url)(const std::string &url);

  // Build the full inference URL from a base URL and model ID.
  // e.g. tx402:      base + "/chat/completions"
  //      x402engine: base + "/api/llm/" + model_id
  std::string (*build_inference_url)(const std::string &base_url,
                                     const std::string &model_id);

  // Parse a catalog response body into CatalogModel entries.
  // Returns the number of models written to `out` (≤ max_out).
  // Return 0 on failure or if this provider has no catalog.
  int (*parse_catalog)(const std::string &body, CatalogModel *out, int max_out);
};

// ── Data Provider Adapter ───────────────────────────────────────────
//
// Describes a non-LLM data API that uses x402 micropayments.

struct DataAdapter {
  const char *name;
  const char *display_name;
  const char *base_url;
  const char *catalog_url;   // Free discovery endpoint (nullptr = none)
  double price_per_call;     // Flat $/call (e.g. 0.001)
  AuthMethod auth;

  bool (*matches_url)(const std::string &url);
};

// ── Provider Registry ───────────────────────────────────────────────

// Maximum number of registered adapters (static storage, no heap).
inline constexpr int kMaxLlmAdapters  = 8;
inline constexpr int kMaxDataAdapters = 4;

// Call once at startup (registers built-in adapters).
void Init();

// Register a custom adapter at runtime (returns false if full).
bool RegisterLlmAdapter(const LlmAdapter *a);
bool RegisterDataAdapter(const DataAdapter *a);

// Query registered adapters.
int LlmAdapterCount();
const LlmAdapter *GetLlmAdapter(int i);

int DataAdapterCount();
const DataAdapter *GetDataAdapter(int i);

// Find the adapter whose matches_url() returns true for `url`.
// Returns nullptr if no match (treat as generic OpenAI-compatible).
const LlmAdapter *FindLlmAdapter(const std::string &url);
const DataAdapter *FindDataAdapter(const std::string &url);

// Find by short name (e.g. "tx402", "x402engine").
const LlmAdapter *FindLlmAdapterByName(const std::string &name);

// Convenience: does the active LLM config use x402 payments?
bool ActiveProviderUsesX402();

}  // namespace providers
}  // namespace survaiv
