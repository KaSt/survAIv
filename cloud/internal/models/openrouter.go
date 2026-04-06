package models

import (
"encoding/json"
"fmt"
"io"
"log/slog"
"net/http"
"sort"
"strconv"
"strings"
"sync"
"time"
)

var fetchClient = &http.Client{Timeout: 15 * time.Second}

// catalogSource describes a provider model endpoint.
type catalogSource struct {
Label   string
URL     string
AuthKey string // empty = no auth needed
Parser  func(body []byte, label string) []ModelInfo
}

// FetchAllCatalogs fetches model catalogs from all known public providers
// and the user's configured LLM endpoint, merging into the registry.
func FetchAllCatalogs(llmURL, apiKey string) {
sources := publicSources()

// Add user's configured endpoint if not already covered.
if llmURL != "" {
label := endpointLabel(llmURL)
alreadyCovered := false
for _, s := range sources {
if strings.Contains(llmURL, extractDomain(s.URL)) {
// User endpoint matches a known source; inject API key if available.
if apiKey != "" {
s.AuthKey = apiKey
}
alreadyCovered = true
break
}
}
if !alreadyCovered {
sources = append(sources, catalogSource{
Label:   label,
URL:     deriveModelsURL(llmURL),
AuthKey: apiKey,
Parser:  parseOpenAICompat,
})
}
}

// Add API key to auth-required sources if the user's endpoint matches.
if apiKey != "" && llmURL != "" {
for i := range sources {
if sources[i].AuthKey == "" && strings.Contains(llmURL, extractDomain(sources[i].URL)) {
sources[i].AuthKey = apiKey
}
}
}

var wg sync.WaitGroup
for _, src := range sources {
src := src
wg.Add(1)
go func() {
defer wg.Done()
fetchSource(src)
}()
}
wg.Wait()
}

// publicSources returns all known provider model endpoints.
// Sources marked with AuthKey="" are public; those with AuthKey="NEEDS_KEY"
// are skipped unless the user's configured endpoint matches (key injected).
func publicSources() []catalogSource {
return []catalogSource{
// ── Public (no auth needed) ──
{Label: "OpenRouter", URL: "https://openrouter.ai/api/v1/models", Parser: parseOpenRouterCatalog},
{Label: "DeepInfra", URL: "https://api.deepinfra.com/v1/openai/models", Parser: parseOpenAICompat},
{Label: "SambaNova", URL: "https://api.sambanova.ai/v1/models", Parser: parseOpenAICompat},
{Label: "Kluster", URL: "https://api.kluster.ai/v1/models", Parser: parseOpenAICompat},

// ── Auth required — fetched only if user's LLM URL matches ──
{Label: "GreenPT", URL: "https://api.greenpt.ai/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Mistral", URL: "https://api.mistral.ai/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Together", URL: "https://api.together.xyz/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Groq", URL: "https://api.groq.com/openai/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "DeepSeek", URL: "https://api.deepseek.com/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Fireworks", URL: "https://api.fireworks.ai/inference/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Hyperbolic", URL: "https://api.hyperbolic.xyz/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Cerebras", URL: "https://api.cerebras.ai/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "SiliconFlow", URL: "https://api.siliconflow.cn/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Novita", URL: "https://api.novita.ai/v3/openai/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "OpenAI", URL: "https://api.openai.com/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Moonshot", URL: "https://api.moonshot.cn/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Zhipu", URL: "https://open.bigmodel.cn/api/paas/v4/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Baichuan", URL: "https://api.baichuan-ai.com/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Yi", URL: "https://api.01.ai/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "MiniMax", URL: "https://api.minimax.chat/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Alibaba", URL: "https://dashscope.aliyuncs.com/compatible-mode/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "xAI", URL: "https://api.x.ai/v1/models", AuthKey: "NEEDS_KEY", Parser: parseOpenAICompat},
{Label: "Cohere", URL: "https://api.cohere.com/v2/models", AuthKey: "NEEDS_KEY", Parser: parseCohereCompat},
}
}

func fetchSource(src catalogSource) {
// Skip auth-required sources that don't have a real key.
if src.AuthKey == "NEEDS_KEY" {
return
}

req, err := http.NewRequest("GET", src.URL, nil)
if err != nil {
return
}
if src.AuthKey != "" {
req.Header.Set("Authorization", "Bearer "+src.AuthKey)
}

resp, err := fetchClient.Do(req)
if err != nil {
slog.Debug("catalog fetch failed", "provider", src.Label, "err", err)
return
}
defer resp.Body.Close()

if resp.StatusCode != 200 {
slog.Debug("catalog fetch non-200", "provider", src.Label, "status", resp.StatusCode)
return
}

body, err := io.ReadAll(resp.Body)
if err != nil {
return
}

models := src.Parser(body, src.Label)
if len(models) > 0 {
AddDynamic(models)
slog.Info("catalog loaded", "provider", src.Label, "models", len(models))
}
}

// ── OpenRouter parser (rich metadata) ──────────────────────────

func parseOpenRouterCatalog(body []byte, label string) []ModelInfo {
var catalog struct {
Data []orModel `json:"data"`
}
if err := json.Unmarshal(body, &catalog); err != nil {
slog.Warn("openrouter parse failed", "err", err)
return nil
}

var filtered []orModel
for _, m := range catalog.Data {
if isNotableModel(m.ID) {
filtered = append(filtered, m)
}
}
sort.Slice(filtered, func(i, j int) bool {
return filtered[i].ContextLength > filtered[j].ContextLength
})
if len(filtered) > 150 {
filtered = filtered[:150]
}

out := make([]ModelInfo, 0, len(filtered))
for _, m := range filtered {
ctxK := m.ContextLength / 1000
if ctxK < 1 {
ctxK = 1
}
promptPrice := parseFloat(m.Pricing.Prompt)
completionPrice := parseFloat(m.Pricing.Completion)
perReq := promptPrice*2000 + completionPrice*1000

out = append(out, ModelInfo{
Name:       m.Name,
Tx402ID:    m.ID,
ContextK:   ctxK,
Reasoning:  estimateReasoning(m.ID),
Speed:      estimateSpeed(m.ID, promptPrice),
MinTask:    inferMinTask(estimateReasoning(m.ID)),
Notes:      label,
Tx402Price: perReq,
})
}

slog.Info("openrouter catalog parsed", "total", len(catalog.Data), "notable", len(out))
return out
}

// ── Generic OpenAI-compatible parser ───────────────────────────

func parseOpenAICompat(body []byte, label string) []ModelInfo {
var catalog struct {
Data []json.RawMessage `json:"data"`
}
if err := json.Unmarshal(body, &catalog); err != nil {
return nil
}

out := make([]ModelInfo, 0)
for _, raw := range catalog.Data {
m := parseGenericModel(raw, label)
if m != nil {
out = append(out, *m)
}
}
return out
}

func parseGenericModel(raw json.RawMessage, label string) *ModelInfo {
var m struct {
ID       string `json:"id"`
OwnedBy string `json:"owned_by"`
Metadata *struct {
ContextLength int `json:"context_length"`
MaxTokens     int `json:"max_tokens"`
Pricing       *struct {
InputTokens  float64 `json:"input_tokens"`
OutputTokens float64 `json:"output_tokens"`
} `json:"pricing"`
} `json:"metadata"`
ContextLength int `json:"context_length"`
MaxModelLen   int `json:"max_model_len"`
}
if err := json.Unmarshal(raw, &m); err != nil || m.ID == "" {
return nil
}

idLow := strings.ToLower(m.ID)
for _, skip := range []string{
"embed", "tts", "whisper", "dall-e", "moderation",
"image", "audio", "rerank", "safety", "guard",
} {
if strings.Contains(idLow, skip) {
return nil
}
}

ctxK := 0
if m.Metadata != nil && m.Metadata.ContextLength > 0 {
ctxK = m.Metadata.ContextLength / 1000
} else if m.ContextLength > 0 {
ctxK = m.ContextLength / 1000
} else if m.MaxModelLen > 0 {
ctxK = m.MaxModelLen / 1000
}
if ctxK < 1 {
ctxK = 128
}

var perReq float64
if m.Metadata != nil && m.Metadata.Pricing != nil {
perReq = (m.Metadata.Pricing.InputTokens/1e6)*2000 + (m.Metadata.Pricing.OutputTokens/1e6)*1000
}

reasoning := estimateReasoning(m.ID)
return &ModelInfo{
Name:       m.ID,
Tx402ID:    m.ID,
ContextK:   ctxK,
Reasoning:  reasoning,
Speed:      estimateSpeed(m.ID, perReq),
MinTask:    inferMinTask(reasoning),
Notes:      label,
Tx402Price: perReq,
}
}

// ── Cohere parser (different response format) ──────────────────

func parseCohereCompat(body []byte, label string) []ModelInfo {
var catalog struct {
Models []struct {
Name          string `json:"name"`
ContextLength int    `json:"context_length"`
} `json:"models"`
}
if err := json.Unmarshal(body, &catalog); err != nil {
return nil
}

out := make([]ModelInfo, 0)
for _, m := range catalog.Models {
if m.Name == "" {
continue
}
ctxK := m.ContextLength / 1000
if ctxK < 1 {
ctxK = 128
}
reasoning := estimateReasoning(m.Name)
out = append(out, ModelInfo{
Name:      m.Name,
Tx402ID:   m.Name,
ContextK:  ctxK,
Reasoning: reasoning,
Speed:     estimateSpeed(m.Name, 0),
MinTask:   inferMinTask(reasoning),
Notes:     label,
})
}
return out
}

// ── Shared types and helpers ───────────────────────────────────

type orModel struct {
ID            string `json:"id"`
Name          string `json:"name"`
Description   string `json:"description"`
ContextLength int    `json:"context_length"`
Pricing       struct {
Prompt     string `json:"prompt"`
Completion string `json:"completion"`
} `json:"pricing"`
TopProvider struct {
ContextLength       int  `json:"context_length"`
MaxCompletionTokens int  `json:"max_completion_tokens"`
IsModerated         bool `json:"is_moderated"`
} `json:"top_provider"`
}

func isNotableModel(id string) bool {
id = strings.ToLower(id)

for _, suffix := range []string{":free", ":beta", ":extended", "-online"} {
if strings.HasSuffix(id, suffix) {
return false
}
}

prefixes := []string{
// Western
"openai/", "anthropic/", "google/", "meta-llama/",
"mistralai/", "cohere/", "nvidia/", "microsoft/",
"x-ai/", "perplexity/",
// Chinese
"deepseek/", "qwen/", "alibaba/", "baichuan/",
"zhipu/", "01-ai/", "yi-", "moonshot/", "minimax/",
"stepfun/", "bytedance/", "thudm/", "internlm/",
// Open-source orgs
"nousresearch/", "teknium/", "cognitivecomputations/",
}
for _, p := range prefixes {
if strings.HasPrefix(id, p) {
return true
}
}

keywords := []string{
"claude", "gpt-4", "gpt-5", "gemini", "llama",
"opus", "sonnet", "haiku", "o1-", "o3-", "o4-",
"qwen", "deepseek", "mistral", "mixtral", "command",
"gemma", "phi-", "kimi", "glm", "yi-large", "grok",
}
for _, kw := range keywords {
if strings.Contains(id, kw) {
return true
}
}
return false
}

func estimateReasoning(id string) int {
id = strings.ToLower(id)
switch {
case strings.Contains(id, "opus") ||
matchAny(id, "o1-", "o3-", "o4-", "o1/", "o3/", "o4/") ||
(strings.Contains(id, "gpt-5") && !strings.Contains(id, "mini") && !strings.Contains(id, "nano")):
return 5
case strings.Contains(id, "sonnet") || strings.Contains(id, "r1") ||
(strings.Contains(id, "gpt-4o") && !strings.Contains(id, "mini")) ||
strings.Contains(id, "gemini-2.5-pro") || strings.Contains(id, "gemini-3") ||
strings.Contains(id, "grok-4") || strings.Contains(id, "kimi-k2") ||
strings.Contains(id, "qwen3-235"):
return 4
case strings.Contains(id, "haiku") || strings.Contains(id, "flash") ||
strings.Contains(id, "mini") || strings.Contains(id, "command-r") ||
strings.Contains(id, "mistral-large") || strings.Contains(id, "mixtral"):
return 3
case strings.Contains(id, "nano") || strings.Contains(id, "lite") ||
strings.Contains(id, "8b") || strings.Contains(id, "7b"):
return 2
default:
return 3
}
}

func estimateSpeed(id string, costIndicator float64) int {
id = strings.ToLower(id)
switch {
case strings.Contains(id, "nano") || strings.Contains(id, "flash") ||
strings.Contains(id, "lite") || strings.Contains(id, "mini") ||
strings.Contains(id, "instant"):
return 5
case strings.Contains(id, "haiku") || strings.Contains(id, "8b") ||
strings.Contains(id, "7b"):
return 5
case strings.Contains(id, "opus") ||
matchAny(id, "o1-", "o3-", "o4-", "o1/", "o3/", "o4/"):
return 2
case costIndicator > 0.01:
return 2
default:
return 3
}
}

func matchAny(s string, patterns ...string) bool {
for _, p := range patterns {
if strings.Contains(s, p) {
return true
}
}
return false
}

func inferMinTask(reasoning int) TaskComplexity {
switch {
case reasoning >= 5:
return Expert
case reasoning >= 4:
return Standard
default:
return Trivial
}
}

func parseFloat(s string) float64 {
f, _ := strconv.ParseFloat(s, 64)
return f
}

func truncate(s string, n int) string {
if len(s) <= n {
return s
}
return s[:n-1] + "…"
}

func deriveModelsURL(baseURL string) string {
u := strings.TrimSuffix(baseURL, "/")
u = strings.TrimSuffix(u, "/chat/completions")
u = strings.TrimSuffix(u, "/")
return u + "/models"
}

func extractDomain(url string) string {
url = strings.TrimPrefix(url, "https://")
url = strings.TrimPrefix(url, "http://")
if i := strings.Index(url, "/"); i > 0 {
url = url[:i]
}
// Strip "api." prefix for broader matching.
url = strings.TrimPrefix(url, "api.")
return url
}

func endpointLabel(url string) string {
url = strings.ToLower(url)
labels := map[string]string{
"greenpt.ai":     "GreenPT",
"mistral.ai":     "Mistral",
"together.xyz":   "Together",
"groq.com":       "Groq",
"deepseek.com":   "DeepSeek",
"deepinfra.com":  "DeepInfra",
"fireworks.ai":   "Fireworks",
"siliconflow.cn": "SiliconFlow",
"novita.ai":      "Novita",
"openai.com":     "OpenAI",
"anthropic.com":  "Anthropic",
"googleapis.com": "Google",
"x.ai":           "xAI",
"cohere.com":     "Cohere",
"zhipuai.cn":     "Zhipu",
"moonshot.cn":    "Moonshot",
"baichuan-ai":    "Baichuan",
"01.ai":          "Yi",
"minimax.chat":   "MiniMax",
"volcengine.com": "Bytedance",
"dashscope":      "Alibaba",
"sambanova.ai":   "SambaNova",
"hyperbolic.xyz": "Hyperbolic",
"cerebras.ai":    "Cerebras",
"kluster.ai":     "Kluster",
"tx402.ai":       "tx402",
"x402engine":     "x402engine",
}
for domain, label := range labels {
if strings.Contains(url, domain) {
return label
}
}
parts := strings.Split(url, "/")
for _, p := range parts {
if strings.Contains(p, ".") && !strings.HasPrefix(p, "http") {
return fmt.Sprintf("endpoint(%s)", p)
}
}
return "custom"
}
