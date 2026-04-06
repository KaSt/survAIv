package models

// TaskComplexity indicates how hard the current agent task is.
type TaskComplexity int

const (
	Trivial  TaskComplexity = 0
	Standard TaskComplexity = 1
	Complex  TaskComplexity = 2
	Expert   TaskComplexity = 3
)

// ModelInfo describes a known LLM model.
type ModelInfo struct {
	Name        string
	Tx402ID     string  // ID on tx402.ai ("" if unavailable)
	EngineID    string  // ID on x402engine.app ("" if unavailable)
	Tx402Price  float64 // $/request on tx402 (or estimated per-request cost)
	EnginePrice float64 // $/request on x402engine
	PromptPPT   float64 // price per prompt token ($/token)
	CompletePPT float64 // price per completion token ($/token)
	Reasoning   int     // 1-5
	Speed       int     // 1-5
	ContextK    int     // thousands of tokens
	MinTask     TaskComplexity
	Notes       string
}

// CostForTokens computes actual cost from token counts.
// Falls back to Tx402Price (per-request estimate) if per-token pricing unavailable.
func (m ModelInfo) CostForTokens(promptTokens, completionTokens int) float64 {
	if m.PromptPPT > 0 || m.CompletePPT > 0 {
		return m.PromptPPT*float64(promptTokens) + m.CompletePPT*float64(completionTokens)
	}
	if m.Tx402Price > 0 {
		return m.Tx402Price
	}
	return 0
}

// hardcoded is the built-in model catalog, cross-referenced from providers.
var hardcoded = []ModelInfo{
	// ── Available on BOTH providers ──
	{
		Name: "DeepSeek V3.2", Tx402ID: "deepseek/deepseek-v3.2", EngineID: "deepseek-v3.2",
		Tx402Price: 0.0005, EnginePrice: 0.005,
		Reasoning: 4, Speed: 4, ContextK: 163, MinTask: Standard,
		Notes: "Best value for trading. Strong reasoning at rock-bottom price on tx402.",
	},
	{
		Name: "DeepSeek R1", Tx402ID: "deepseek/deepseek-r1-0528", EngineID: "deepseek-r1",
		Tx402Price: 0.002038, EnginePrice: 0.01,
		Reasoning: 5, Speed: 2, ContextK: 164, MinTask: Expert,
		Notes: "Top-tier chain-of-thought. Use sparingly for high-stakes decisions.",
	},
	{
		Name: "Qwen3 235B", Tx402ID: "qwen/qwen3-235b-a22b-2507", EngineID: "qwen",
		Tx402Price: 0.000335, EnginePrice: 0.004,
		Reasoning: 4, Speed: 3, ContextK: 131, MinTask: Standard,
		Notes: "Large MoE. Great analysis at very low cost on tx402.",
	},
	{
		Name: "Llama 4 Maverick", Tx402ID: "meta-llama/llama-4-maverick", EngineID: "llama-4-maverick",
		Tx402Price: 0.000511, EnginePrice: 0.003,
		Reasoning: 3, Speed: 4, ContextK: 1050, MinTask: Standard,
		Notes: "Huge context window. Good for multi-market analysis.",
	},
	{
		Name: "Llama 3.3 70B", Tx402ID: "meta-llama/llama-3.3-70b-instruct", EngineID: "llama",
		Tx402Price: 0.00026, EnginePrice: 0.002,
		Reasoning: 2, Speed: 5, ContextK: 131, MinTask: Trivial,
		Notes: "Fast and cheap. Good for simple market scans and status checks.",
	},
	{
		Name: "DeepSeek V3", Tx402ID: "deepseek/deepseek-chat-v3.1", EngineID: "deepseek",
		Tx402Price: 0.000625, EnginePrice: 0.005,
		Reasoning: 4, Speed: 3, ContextK: 164, MinTask: Standard,
		Notes: "Predecessor to V3.2. Still strong, slightly more verbose.",
	},
	{
		Name: "Kimi K2.5", Tx402ID: "moonshotai/kimi-k2.5", EngineID: "kimi",
		Tx402Price: 0.002063, EnginePrice: 0.03,
		Reasoning: 4, Speed: 3, ContextK: 262, MinTask: Complex,
		Notes: "Moonshot's flagship. Long context, strong reasoning. Expensive on engine.",
	},
	{
		Name: "Qwen 2.5 72B", Tx402ID: "qwen/qwen-2.5-72b-instruct",
		Tx402Price: 0.000207,
		Reasoning: 2, Speed: 4, ContextK: 33, MinTask: Trivial,
		Notes: "tx402 only. Cheap fallback for simple tasks.",
	},
	{
		Name: "MiniMax M2", Tx402ID: "minimax/minimax-m2", EngineID: "minimax",
		Tx402Price: 0.000782, EnginePrice: 0.01,
		Reasoning: 3, Speed: 3, ContextK: 196, MinTask: Standard,
		Notes: "Balanced mid-range. Decent at analysis.",
	},
	// ── tx402.ai exclusives ──
	{
		Name: "GPT-OSS 120B", Tx402ID: "openai/gpt-oss-120b",
		Tx402Price: 0.00015,
		Reasoning: 2, Speed: 4, ContextK: 131, MinTask: Trivial,
		Notes: "tx402 only. Ultra-cheap for basic scanning.",
	},
	{
		Name: "GPT-OSS 20B", Tx402ID: "openai/gpt-oss-20b",
		Tx402Price: 0.000107,
		Reasoning: 1, Speed: 5, ContextK: 131, MinTask: Trivial,
		Notes: "tx402 only. Cheapest available. Only for trivial formatting.",
	},
	{
		Name: "Qwen3 Coder 30B", Tx402ID: "qwen/qwen3-coder-30b-a3b-instruct",
		Tx402Price: 0.000194,
		Reasoning: 2, Speed: 5, ContextK: 262, MinTask: Trivial,
		Notes: "tx402 only. Code-focused but useful for structured JSON output.",
	},
	// ── x402engine.app exclusives ──
	{
		Name: "GPT-5 Nano", EngineID: "gpt-5-nano",
		EnginePrice: 0.002,
		Reasoning: 2, Speed: 5, ContextK: 131, MinTask: Trivial,
		Notes: "Engine only. Fast, cheap for simple checks.",
	},
	{
		Name: "Grok 4 Fast", EngineID: "grok-4-fast",
		EnginePrice: 0.004,
		Reasoning: 3, Speed: 5, ContextK: 131, MinTask: Standard,
		Notes: "Engine only. Quick reasoning from xAI.",
	},
	{
		Name: "Gemini 2.5 Flash", EngineID: "gemini-flash",
		EnginePrice: 0.009,
		Reasoning: 3, Speed: 5, ContextK: 131, MinTask: Standard,
		Notes: "Engine only. Google's fast model. Good balance.",
	},
	{
		Name: "GPT-4o Mini", EngineID: "gpt-4o-mini",
		EnginePrice: 0.003,
		Reasoning: 2, Speed: 5, ContextK: 128, MinTask: Trivial,
		Notes: "Engine only. OpenAI's small model. Cheap and fast.",
	},
	{
		Name: "Mistral Large 3", EngineID: "mistral",
		EnginePrice: 0.006,
		Reasoning: 3, Speed: 4, ContextK: 131, MinTask: Standard,
		Notes: "Engine only. European model. Good general reasoning.",
	},
	{
		Name: "Claude Haiku 4.5", EngineID: "claude-haiku",
		EnginePrice: 0.02,
		Reasoning: 3, Speed: 5, ContextK: 200, MinTask: Standard,
		Notes: "Engine only. Anthropic's fast tier. Precise but pricey.",
	},
	{
		Name: "GPT-5.1", EngineID: "gpt-5.1",
		EnginePrice: 0.035,
		Reasoning: 5, Speed: 3, ContextK: 128, MinTask: Expert,
		Notes: "Engine only. Frontier reasoning. Very expensive — use only when critical.",
	},
	{
		Name: "Gemini 3.1 Flash Lite", EngineID: "gemini-3.1-flash-lite",
		EnginePrice: 0.003,
		Reasoning: 2, Speed: 5, ContextK: 131, MinTask: Trivial,
		Notes: "Engine only. Google's cheapest. Good for simple scans.",
	},
}
