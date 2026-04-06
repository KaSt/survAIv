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

// FetchAllCatalogs fetches model catalogs from OpenRouter and the user's
// configured LLM endpoint in parallel, merging results into the registry.
func FetchAllCatalogs(llmURL, apiKey string) {
var wg sync.WaitGroup

// Always fetch OpenRouter (public, most comprehensive).
wg.Add(1)
go func() {
defer wg.Done()
fetchOpenRouter()
}()

// Fetch from the configured endpoint if it's not OpenRouter itself.
if llmURL != "" && !strings.Contains(llmURL, "openrouter.ai") {
wg.Add(1)
go func() {
defer wg.Done()
fetchOpenAICompat(llmURL, apiKey, endpointLabel(llmURL))
}()
}

wg.Wait()
}

// ── OpenRouter (public, rich metadata) ─────────────────────────

func fetchOpenRouter() {
resp, err := fetchClient.Get("https://openrouter.ai/api/v1/models")
if err != nil {
slog.Warn("openrouter model fetch failed", "err", err)
return
}
defer resp.Body.Close()

body, err := io.ReadAll(resp.Body)
if err != nil {
slog.Warn("openrouter model read failed", "err", err)
return
}

var catalog struct {
Data []orModel `json:"data"`
}
if err := json.Unmarshal(body, &catalog); err != nil {
slog.Warn("openrouter model parse failed", "err", err)
return
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
if len(filtered) > 120 {
filtered = filtered[:120]
}

models := make([]ModelInfo, 0, len(filtered))
for _, m := range filtered {
ctxK := m.ContextLength / 1000
if ctxK < 1 {
ctxK = 1
}

promptPrice := parseFloat(m.Pricing.Prompt)
completionPrice := parseFloat(m.Pricing.Completion)
perReq := promptPrice*2000 + completionPrice*1000

models = append(models, ModelInfo{
Name:       m.Name,
Tx402ID:    m.ID,
ContextK:   ctxK,
Reasoning:  estimateReasoning(m.ID),
Speed:      estimateSpeed(m.ID, promptPrice),
MinTask:    inferMinTask(estimateReasoning(m.ID)),
Notes:      "OpenRouter",
Tx402Price: perReq,
})
}

AddDynamic(models)
slog.Info("openrouter catalog loaded", "fetched", len(catalog.Data), "notable", len(models))
}

// ── Generic OpenAI-compatible endpoint ─────────────────────────
// Works with Mistral, Together, Groq, DeepSeek, DeepInfra, Fireworks,
// SiliconFlow, Novita, Moonshot, Zhipu, and any OpenAI-compatible provider.

func fetchOpenAICompat(baseURL, apiKey, label string) {
modelsURL := strings.TrimSuffix(baseURL, "/")
modelsURL = strings.TrimSuffix(modelsURL, "/chat/completions")
modelsURL = strings.TrimSuffix(modelsURL, "/")
modelsURL += "/models"

req, err := http.NewRequest("GET", modelsURL, nil)
if err != nil {
return
}
if apiKey != "" {
req.Header.Set("Authorization", "Bearer "+apiKey)
}

resp, err := fetchClient.Do(req)
if err != nil {
slog.Debug("model catalog fetch failed", "provider", label, "err", err)
return
}
defer resp.Body.Close()

if resp.StatusCode != 200 {
slog.Debug("model catalog fetch non-200", "provider", label, "status", resp.StatusCode)
return
}

body, err := io.ReadAll(resp.Body)
if err != nil {
return
}

var catalog struct {
Data []json.RawMessage `json:"data"`
}
if err := json.Unmarshal(body, &catalog); err != nil {
return
}

out := make([]ModelInfo, 0)
for _, raw := range catalog.Data {
m := parseGenericModel(raw, label)
if m != nil {
out = append(out, *m)
}
}

if len(out) > 0 {
AddDynamic(out)
slog.Info("provider catalog loaded", "provider", label, "models", len(out))
}
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
}
if err := json.Unmarshal(raw, &m); err != nil || m.ID == "" {
return nil
}

idLow := strings.ToLower(m.ID)
for _, skip := range []string{"embed", "tts", "whisper", "dall-e", "moderation", "image", "audio", "rerank"} {
if strings.Contains(idLow, skip) {
return nil
}
}

ctxK := 0
if m.Metadata != nil && m.Metadata.ContextLength > 0 {
ctxK = m.Metadata.ContextLength / 1000
} else if m.ContextLength > 0 {
ctxK = m.ContextLength / 1000
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

func endpointLabel(url string) string {
url = strings.ToLower(url)
labels := map[string]string{
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
