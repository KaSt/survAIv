#include "provider.h"

#include <cstring>

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"

namespace survaiv {
namespace providers {

static const char *kTag = "provider";

// ── Registry storage ────────────────────────────────────────────────

static const LlmAdapter *g_llm[kMaxLlmAdapters];
static int g_llm_count = 0;

static const DataAdapter *g_data[kMaxDataAdapters];
static int g_data_count = 0;

// ── Helpers ─────────────────────────────────────────────────────────

static double ParseDollarString(const char *s) {
  if (!s || !*s) return 0.0;
  if (*s == '$') ++s;
  char *end = nullptr;
  double v = strtod(s, &end);
  return (end != s) ? v : 0.0;
}

static void SafeCopy(char *dst, size_t sz, const char *src) {
  if (!src) { dst[0] = '\0'; return; }
  size_t len = strlen(src);
  if (len >= sz) len = sz - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

// ═══════════════════════════════════════════════════════════════════
// ██  tx402.ai  ██
// ═══════════════════════════════════════════════════════════════════
//
// OpenAI-compatible API. Model ID in request body. Supports both
// API-key auth and x402 micropayments.
//
// Catalog: GET https://tx402.ai/v1/models
// Response: {"data":[{"id":"openai/gpt-oss-20b","pricing":{
//   "estimated_per_request":"$0.000107"},"context_window":131000}]}

static bool Tx402MatchesUrl(const std::string &url) {
  return url.find("tx402.ai") != std::string::npos;
}

static std::string Tx402BuildUrl(const std::string &base,
                                 const std::string & /*model_id*/) {
  // tx402 uses standard OpenAI /chat/completions endpoint.
  std::string u = base;
  if (!u.empty() && u.back() == '/') u.pop_back();
  // If base already ends with /v1, append path; otherwise add /v1.
  if (u.size() >= 3 && u.substr(u.size() - 3) == "/v1")
    return u + "/chat/completions";
  return u + "/v1/chat/completions";
}

static int Tx402ParseCatalog(const std::string &body, CatalogModel *out,
                             int max_out) {
  cJSON *root = cJSON_Parse(body.c_str());
  if (!root) return 0;

  cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (!cJSON_IsArray(data)) { cJSON_Delete(root); return 0; }

  int count = 0;
  int n = cJSON_GetArraySize(data);
  for (int i = 0; i < n && count < max_out; ++i) {
    cJSON *item = cJSON_GetArrayItem(data, i);
    cJSON *jid = cJSON_GetObjectItemCaseSensitive(item, "id");
    if (!cJSON_IsString(jid)) continue;

    CatalogModel &m = out[count];
    SafeCopy(m.id, sizeof(m.id), jid->valuestring);

    // Display name: strip org prefix.
    const char *slash = strrchr(jid->valuestring, '/');
    SafeCopy(m.display_name, sizeof(m.display_name),
             slash ? slash + 1 : jid->valuestring);

    m.price_per_req = 0.0;
    cJSON *pricing = cJSON_GetObjectItemCaseSensitive(item, "pricing");
    if (pricing) {
      cJSON *est = cJSON_GetObjectItemCaseSensitive(pricing,
                                                     "estimated_per_request");
      if (cJSON_IsString(est))
        m.price_per_req = ParseDollarString(est->valuestring);
    }

    cJSON *ctx = cJSON_GetObjectItemCaseSensitive(item, "context_window");
    m.context_k = cJSON_IsNumber(ctx)
                      ? static_cast<uint32_t>(ctx->valueint / 1000)
                      : 128;
    ++count;
  }

  cJSON_Delete(root);
  return count;
}

static const LlmAdapter kTx402Adapter = {
    .name              = "tx402",
    .display_name      = "tx402.ai",
    .default_base_url  = "https://tx402.ai/v1",
    .catalog_url       = "https://tx402.ai/v1/models",
    .auth              = AuthMethod::kX402,
    .model_in_body     = true,
    .matches_url       = Tx402MatchesUrl,
    .build_inference_url = Tx402BuildUrl,
    .parse_catalog     = Tx402ParseCatalog,
};

// ═══════════════════════════════════════════════════════════════════
// ██  x402engine.app  ██
// ═══════════════════════════════════════════════════════════════════
//
// Model ID goes in the URL path, NOT in the body.
// Base URL for inference: https://x402-gateway-production.up.railway.app
//
// Catalog: GET https://x402engine.app/.well-known/x402.json
// Response: {"categories":{"compute":[
//   {"id":"llm-deepseek-v3.2","name":"DeepSeek V3.2","price":"$0.005",
//    "endpoint":"https://…/api/llm/deepseek-v3.2"},...]}}
//
// Only items whose "id" starts with "llm-" are LLM models.

static bool EngineMatchesUrl(const std::string &url) {
  return url.find("x402-gateway") != std::string::npos ||
         url.find("x402engine") != std::string::npos;
}

static std::string EngineBuildUrl(const std::string &base,
                                  const std::string &model_id) {
  std::string u = base;
  if (!u.empty() && u.back() == '/') u.pop_back();
  return u + "/api/llm/" + model_id;
}

static int EngineParseHelper(const std::string &body, CatalogModel *out,
                             int max_out) {
  cJSON *root = cJSON_Parse(body.c_str());
  if (!root) return 0;

  cJSON *cats = cJSON_GetObjectItemCaseSensitive(root, "categories");
  cJSON *compute = cats ? cJSON_GetObjectItemCaseSensitive(cats, "compute")
                        : nullptr;
  if (!cJSON_IsArray(compute)) { cJSON_Delete(root); return 0; }

  int count = 0;
  int n = cJSON_GetArraySize(compute);
  for (int i = 0; i < n && count < max_out; ++i) {
    cJSON *item = cJSON_GetArrayItem(compute, i);
    cJSON *jid = cJSON_GetObjectItemCaseSensitive(item, "id");
    if (!cJSON_IsString(jid)) continue;

    const char *raw_id = jid->valuestring;
    // Only keep LLM items (id starts with "llm-").
    if (strncmp(raw_id, "llm-", 4) != 0) continue;
    const char *short_id = raw_id + 4;

    CatalogModel &m = out[count];
    SafeCopy(m.id, sizeof(m.id), short_id);

    cJSON *jname = cJSON_GetObjectItemCaseSensitive(item, "name");
    SafeCopy(m.display_name, sizeof(m.display_name),
             cJSON_IsString(jname) ? jname->valuestring : short_id);

    cJSON *jprice = cJSON_GetObjectItemCaseSensitive(item, "price");
    m.price_per_req = cJSON_IsString(jprice)
                          ? ParseDollarString(jprice->valuestring)
                          : 0.0;
    m.context_k = 128;  // x402engine doesn't expose context window
    ++count;
  }

  cJSON_Delete(root);
  return count;
}

static const LlmAdapter kEngineAdapter = {
    .name              = "x402engine",
    .display_name      = "x402engine.app",
    .default_base_url  = "https://x402-gateway-production.up.railway.app",
    // Catalog disabled: response is ~55 KB (all service categories) which
    // exceeds what ESP32-C3 can safely fetch alongside TLS overhead.
    // x402engine models are covered by the hardcoded registry instead.
    .catalog_url       = nullptr,
    .auth              = AuthMethod::kX402,
    .model_in_body     = false,
    .matches_url       = EngineMatchesUrl,
    .build_inference_url = EngineBuildUrl,
    .parse_catalog     = EngineParseHelper,
};

// ═══════════════════════════════════════════════════════════════════
// ██  Custom / Generic OpenAI-compatible  ██
// ═══════════════════════════════════════════════════════════════════
//
// Catch-all for any OpenAI-compatible endpoint (local, cloud, etc.).
// Model ID in body, API-key auth, no catalog.

static bool CustomMatchesUrl(const std::string & /*url*/) {
  // This is the fallback — never proactively matches.
  return false;
}

static std::string CustomBuildUrl(const std::string &base,
                                  const std::string & /*model_id*/) {
  std::string u = base;
  if (!u.empty() && u.back() == '/') u.pop_back();
  // Append /chat/completions if not already present.
  if (u.size() >= 18 &&
      u.substr(u.size() - 18) == "/chat/completions")
    return u;
  if (u.size() >= 3 && u.substr(u.size() - 3) == "/v1")
    return u + "/chat/completions";
  return u + "/v1/chat/completions";
}

static int CustomNoCatalog(const std::string &, CatalogModel *, int) {
  return 0;
}

static const LlmAdapter kCustomAdapter = {
    .name              = "custom",
    .display_name      = "Custom endpoint",
    .default_base_url  = nullptr,
    .catalog_url       = nullptr,
    .auth              = AuthMethod::kApiKey,
    .model_in_body     = true,
    .matches_url       = CustomMatchesUrl,
    .build_inference_url = CustomBuildUrl,
    .parse_catalog     = CustomNoCatalog,
};

// ═══════════════════════════════════════════════════════════════════
// ██  claw402.ai (data provider)  ██
// ═══════════════════════════════════════════════════════════════════
//
// Crypto data API gateway (coinank). All endpoints $0.001 via x402.
// NOT an LLM provider — registered as a DataAdapter only.

static bool Claw402MatchesUrl(const std::string &url) {
  return url.find("claw402") != std::string::npos;
}

static const DataAdapter kClaw402Adapter = {
    .name          = "claw402",
    .display_name  = "claw402.ai",
    .base_url      = "https://claw402.ai/api/v1",
    .catalog_url   = "https://claw402.ai/api/v1/catalog",
    .price_per_call = 0.001,
    .auth          = AuthMethod::kX402,
    .matches_url   = Claw402MatchesUrl,
};

// ── Registry API ────────────────────────────────────────────────────

bool RegisterLlmAdapter(const LlmAdapter *a) {
  if (g_llm_count >= kMaxLlmAdapters) return false;
  g_llm[g_llm_count++] = a;
  ESP_LOGI(kTag, "Registered LLM adapter: %s", a->display_name);
  return true;
}

bool RegisterDataAdapter(const DataAdapter *a) {
  if (g_data_count >= kMaxDataAdapters) return false;
  g_data[g_data_count++] = a;
  ESP_LOGI(kTag, "Registered data adapter: %s", a->display_name);
  return true;
}

void Init() {
  g_llm_count = 0;
  g_data_count = 0;

  RegisterLlmAdapter(&kTx402Adapter);
  RegisterLlmAdapter(&kEngineAdapter);
  RegisterLlmAdapter(&kCustomAdapter);   // Fallback — always last.
  RegisterDataAdapter(&kClaw402Adapter);

  ESP_LOGI(kTag, "Provider registry: %d LLM + %d data adapters",
           g_llm_count, g_data_count);
}

int LlmAdapterCount() { return g_llm_count; }
const LlmAdapter *GetLlmAdapter(int i) {
  return (i >= 0 && i < g_llm_count) ? g_llm[i] : nullptr;
}

int DataAdapterCount() { return g_data_count; }
const DataAdapter *GetDataAdapter(int i) {
  return (i >= 0 && i < g_data_count) ? g_data[i] : nullptr;
}

const LlmAdapter *FindLlmAdapter(const std::string &url) {
  for (int i = 0; i < g_llm_count; ++i) {
    if (g_llm[i]->matches_url && g_llm[i]->matches_url(url))
      return g_llm[i];
  }
  // Return custom fallback (last registered, if present).
  for (int i = g_llm_count - 1; i >= 0; --i) {
    if (strcmp(g_llm[i]->name, "custom") == 0) return g_llm[i];
  }
  return nullptr;
}

const DataAdapter *FindDataAdapter(const std::string &url) {
  for (int i = 0; i < g_data_count; ++i) {
    if (g_data[i]->matches_url && g_data[i]->matches_url(url))
      return g_data[i];
  }
  return nullptr;
}

const LlmAdapter *FindLlmAdapterByName(const std::string &name) {
  for (int i = 0; i < g_llm_count; ++i) {
    if (name == g_llm[i]->name) return g_llm[i];
  }
  return nullptr;
}

bool ActiveProviderUsesX402() {
  std::string url = config::OpenaiBaseUrl();
  const LlmAdapter *a = FindLlmAdapter(url);
  if (a && a->auth == AuthMethod::kX402) return true;

  // Also check if any registered data provider uses x402 and matches
  // the configured "llm_provider" name.
  std::string prov = config::LlmProvider();
  for (int i = 0; i < g_data_count; ++i) {
    if (prov == g_data[i]->name && g_data[i]->auth == AuthMethod::kX402)
      return true;
  }
  return false;
}

}  // namespace providers
}  // namespace survaiv
