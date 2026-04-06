package agent

import (
	"fmt"
	"strings"
	"time"

	"survaiv/internal/types"
	"survaiv/internal/wisdom"
)

const (
	estPromptTokens     = 16000
	estCompletionTokens = 800
	// Reasoning models count reasoning tokens against max_tokens.
	// Cloud has no memory constraints — be generous so thinking
	// models aren't starved of output tokens.
	maxCompletionTokens = 16384
)

// BuildSystemPrompt constructs the system prompt for the LLM.
func BuildSystemPrompt(paperOnly, geoblocked bool, toolUsage int, wis *wisdom.Tracker) string {
	var b strings.Builder

	b.WriteString("You are a frugal market-analysis agent running on a cloud server. ")
	b.WriteString("You do not have direct web access. The server can perform a tiny set of ")
	b.WriteString("Polymarket HTTP tool calls for you, and every LLM call costs USDC from the same ")
	b.WriteString("survival budget.\n")
	b.WriteString("Constraints:\n")
	b.WriteString("1. Preserve capital first. If expected edge is weak or uncertain, hold.\n")
	b.WriteString("1b. Each market snapshot includes a 'description' with resolution criteria. ")
	b.WriteString("Use it to assess whether the outcome is predictable. Combine your general ")
	b.WriteString("knowledge (training data) with the market price to estimate edge.\n")

	if paperOnly || geoblocked {
		b.WriteString("2. Paper trading mode is active. Only use paper_buy_yes, paper_buy_no, paper_close.\n")
		b.WriteString("3. Never suggest live trading.\n")
	} else {
		b.WriteString("2. LIVE TRADING MODE. You are placing REAL trades with REAL money.\n")
		b.WriteString("3. Only recommend a live position when confidence >= 0.80 and edge_bps >= 200.\n")
		b.WriteString("4. Keep size_fraction <= 0.01. This is real capital — be extremely cautious.\n")
	}

	switch toolUsage {
	case 0: // frugal
		b.WriteString("5. Minimize tool calls. Only use a tool if you absolutely cannot decide without it. ")
		b.WriteString("Every tool call costs an extra LLM round.\n")
	case 2: // generous
		b.WriteString("5. Use tool calls freely to gather maximum context. The server has already ")
		b.WriteString("performed proactive news searches based on market categories — check the ")
		b.WriteString("'proactive_research' field in the input for pre-fetched headlines and snippets. ")
		b.WriteString("Use search_news for deeper dives on promising markets. For sports: consider injuries, ")
		b.WriteString("form, coaching changes. For politics: polls, endorsements, policy shifts. ")
		b.WriteString("For geopolitics: diplomatic moves, sanctions, military activity. ")
		b.WriteString("For crypto/finance: regulatory news, on-chain data, earnings.\n")
	default: // balanced
		if paperOnly || geoblocked {
			b.WriteString("5. Tool calls (search_news, search_markets) are free in paper mode — use them ")
			b.WriteString("whenever you need more context to make a good decision. If a market involves ")
			b.WriteString("sports, politics, or current events, search for recent news before deciding.\n")
		} else {
			b.WriteString("5. Prefer zero or one tool call. Tool calls are expensive because they trigger another ")
			b.WriteString("LLM round.\n")
		}
	}
	b.WriteString("6. Return JSON only. No markdown.\n")

	if paperOnly || geoblocked {
		b.WriteString("7. Allowed decision types: hold, tool_call, paper_buy_yes, paper_buy_no, paper_close.\n")
		b.WriteString("8. Only recommend a paper position when confidence >= 0.65 and edge_bps >= 150.\n")
		b.WriteString("9. Keep size_fraction <= 0.02.\n")
	} else {
		b.WriteString("7. Allowed decision types: hold, tool_call, buy_yes, buy_no, close, ")
		b.WriteString("paper_buy_yes, paper_buy_no, paper_close.\n")
		b.WriteString("8. Prefer live trades (buy_yes/buy_no/close) when you have conviction. ")
		b.WriteString("Use paper trades only for exploration or low-confidence ideas.\n")
	}

	switch toolUsage {
	case 0:
		b.WriteString("10. Allowed tools (use sparingly):\n")
	case 2:
		b.WriteString("10. Allowed tools (use freely for better decisions):\n")
	default:
		if paperOnly || geoblocked {
			b.WriteString("10. Allowed tools (use freely in paper mode):\n")
		} else {
			b.WriteString("10. Allowed tools (prefer zero or one per cycle):\n")
		}
	}
	b.WriteString(`  a) search_markets: {"order":"volume24hr","limit":N,"offset":N} — fetch Polymarket listings.`)
	b.WriteByte('\n')
	b.WriteString("  b) search_news: {\"query\":\"<search terms>\"} — web search for recent news/context on a topic. ")
	b.WriteString("Use this when you need current events context to assess a market. Keep queries short and specific.\n")
	b.WriteString("Return one of these JSON shapes exactly:\n")

	b.WriteString(`{"type":"tool_call","tool":"search_markets","arguments":{"order":"volume24hr","limit":5,"offset":0},"rationale":"..."}`)
	b.WriteByte('\n')
	b.WriteString(`{"type":"tool_call","tool":"search_news","arguments":{"query":"FC Barcelona vs Atletico Madrid April 2026"},"rationale":"..."}`)
	b.WriteByte('\n')

	if !paperOnly && !geoblocked {
		b.WriteString(`{"type":"buy_yes","market_id":"...","edge_bps":210,"confidence":0.80,"size_fraction":0.005,"rationale":"..."}`)
		b.WriteByte('\n')
		b.WriteString(`{"type":"buy_no","market_id":"...","edge_bps":210,"confidence":0.80,"size_fraction":0.005,"rationale":"..."}`)
		b.WriteByte('\n')
		b.WriteString(`{"type":"close","market_id":"...","edge_bps":0,"confidence":0.75,"size_fraction":0.0,"rationale":"..."}`)
		b.WriteByte('\n')
	}

	b.WriteString(`{"type":"paper_buy_yes","market_id":"...","edge_bps":210,"confidence":0.70,"size_fraction":0.01,"rationale":"..."}`)
	b.WriteByte('\n')
	b.WriteString(`{"type":"paper_buy_no","market_id":"...","edge_bps":210,"confidence":0.70,"size_fraction":0.01,"rationale":"..."}`)
	b.WriteByte('\n')
	b.WriteString(`{"type":"paper_close","market_id":"...","edge_bps":0,"confidence":0.75,"size_fraction":0.0,"rationale":"..."}`)
	b.WriteByte('\n')
	b.WriteString(`{"type":"hold","market_id":"","edge_bps":0,"confidence":0.0,"size_fraction":0.0,"rationale":"..."}`)
	b.WriteByte('\n')

	b.WriteString(`11. ALWAYS include a "market_ratings" array rating each market you reviewed. `)
	b.WriteString(`Each entry: {"id":"<market_id>","signal":"bullish|bearish|neutral|skip",`)
	b.WriteString(`"edge_bps":<number>,"confidence":<0-1>,"note":"<1 sentence>"}. `)
	b.WriteString("Include it even for hold decisions. Example:\n")
	b.WriteString(`{"type":"hold","market_id":"","edge_bps":0,"confidence":0.0,`)
	b.WriteString(`"size_fraction":0.0,"rationale":"...",`)
	b.WriteString(`"market_ratings":[{"id":"abc","signal":"neutral","edge_bps":30,`)
	b.WriteString(`"confidence":0.40,"note":"Too close to call"}]}`)

	// Inject learned trading wisdom.
	if wis != nil {
		stats := wis.Stats()
		if stats.LearnedRules != "" {
			b.WriteString("\n--- LEARNED TRADING RULES (from verified past outcomes) ---\n")
			b.WriteString(stats.LearnedRules)
		}
	}

	return b.String()
}

// BuildUserPrompt constructs the user prompt with current state.
func BuildUserPrompt(
	geo types.GeoblockStatus,
	budget types.BudgetInfo,
	markets []types.MarketSnapshot,
	positions []types.Position,
	paperOnly bool,
	dailyLossLimit float64,
) string {
	var b strings.Builder

	b.WriteString(fmt.Sprintf(
		`{"paper_trading_only":%t,"geoblock":{"blocked":%t,"country":"%s","region":"%s"},`,
		paperOnly || geo.Blocked, geo.Blocked, jsonEscape(geo.Country), jsonEscape(geo.Region)))

	b.WriteString(fmt.Sprintf(
		`"budget":{"cash_usdc":%.4f,"reserve_usdc":%.4f,"estimated_llm_round_cost_usdc":0.0005,`+
			`"cumulative_llm_spend_usdc":%.4f,"equity_usdc":%.4f,`+
			`"realized_paper_pnl_usdc":%.4f,"daily_loss_usdc":%.4f,"daily_loss_limit_usdc":%.2f},`,
		budget.Cash, budget.Reserve, budget.LlmSpend, budget.Equity,
		budget.RealizedPnl, budget.DailyLoss, dailyLossLimit))

	// Open positions.
	b.WriteString(`"open_positions":[`)
	for i, p := range positions {
		if i > 0 {
			b.WriteByte(',')
		}
		// Calculate current price from markets.
		currentPrice := p.EntryPrice
		var unrealizedPnl float64
		if m := types.FindMarket(markets, p.MarketID); m != nil {
			if p.Side == "yes" {
				currentPrice = m.YesPrice
			} else {
				currentPrice = m.NoPrice
			}
			unrealizedPnl = (currentPrice - p.EntryPrice) * p.Shares
		}
		b.WriteString(fmt.Sprintf(
			`{"market_id":"%s","question":"%s","side":"%s","entry_price":%.4f,"current_price":%.4f,`+
				`"unrealized_pnl":%.4f,"stake_usdc":%.4f,"is_live":%t}`,
			jsonEscape(p.MarketID), jsonEscape(p.Question), p.Side,
			p.EntryPrice, currentPrice, unrealizedPnl, p.StakeUsdc, p.IsLive))
	}
	b.WriteString(`],`)

	// Market snapshots.
	b.WriteString(`"market_snapshots":[`)
	for i, m := range markets {
		if i > 0 {
			b.WriteByte(',')
		}
		b.WriteString(fmt.Sprintf(
			`{"id":"%s","question":"%s","description":"%s","yes_price":%.4f,"no_price":%.4f,`+
				`"volume":%.2f,"liquidity":%.2f,"category":"%s","end_date":"%s"}`,
			jsonEscape(m.ID), jsonEscape(m.Question), jsonEscape(m.Description),
			m.YesPrice, m.NoPrice, m.Volume, m.Liquidity,
			jsonEscape(m.Category), m.EndDate))
	}
	b.WriteString("]}")

	return b.String()
}

// BuildNewsFollowUp appends news search results to the user prompt.
func BuildNewsFollowUp(userPrompt string, newsJSON string) string {
	return userPrompt + "\n" +
		`{"tool_result":{"tool":"search_news","results":` + newsJSON + `}}`
}

// BuildFollowUpPrompt appends tool results to the user prompt.
func BuildFollowUpPrompt(userPrompt string, toolMarkets []types.MarketSnapshot) string {
	var b strings.Builder
	b.WriteString(userPrompt)
	b.WriteString("\n")
	b.WriteString(`{"tool_result":{"tool":"search_markets","markets":[`)

	for i, m := range toolMarkets {
		if i > 0 {
			b.WriteByte(',')
		}
		b.WriteString(fmt.Sprintf(
			`{"id":"%s","question":"%s","description":"%s","yes_price":%.4f,"no_price":%.4f,`+
				`"volume":%.2f,"liquidity":%.2f,"category":"%s","end_date":"%s"}`,
			jsonEscape(m.ID), jsonEscape(m.Question), jsonEscape(m.Description),
			m.YesPrice, m.NoPrice, m.Volume, m.Liquidity,
			jsonEscape(m.Category), m.EndDate))
	}

	b.WriteString("]}}")
	return b.String()
}

// EstimatedChatCostUsdc returns a rough cost estimate.
func EstimatedChatCostUsdc() float64 {
	return 0.0005
}

// FormatUptime returns a human-readable uptime string.
func FormatUptime(start time.Time) string {
	d := time.Since(start)
	h := int(d.Hours())
	m := int(d.Minutes()) % 60
	return fmt.Sprintf("%dh %dm", h, m)
}

func jsonEscape(s string) string {
	s = strings.ReplaceAll(s, `\`, `\\`)
	s = strings.ReplaceAll(s, `"`, `\"`)
	s = strings.ReplaceAll(s, "\n", `\n`)
	s = strings.ReplaceAll(s, "\r", `\r`)
	s = strings.ReplaceAll(s, "\t", `\t`)
	return s
}
