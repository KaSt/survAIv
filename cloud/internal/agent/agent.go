package agent

import (
	"context"
	"encoding/json"
	"fmt"
	"log/slog"
	"strings"
	"sync"
	"time"

	"survaiv/internal/config"
	"survaiv/internal/dashboard"
	"survaiv/internal/dynconfig"
	"survaiv/internal/httpclient"
	"survaiv/internal/ledger"
	"survaiv/internal/models"
	"survaiv/internal/news"
	"survaiv/internal/polymarket"
	"survaiv/internal/provider"
	"survaiv/internal/types"
	"survaiv/internal/wisdom"
	"survaiv/internal/x402"
)

const (
	simulatedCostPerRequest = 0.0005
	llmFailRetryDelaySec    = 60
	marketFailRetryDelaySec = 30
	maxPositionBps          = 200 // 2% max position size
)

// Agent orchestrates the prediction market analysis cycle.
type Agent struct {
	cfg           *config.Config
	client        *httpclient.Client
	ledger        *ledger.Ledger
	x402          *x402.Payment
	dash          *dashboard.State
	wisdom        *wisdom.Tracker
	cycleCount    int
	startTime     time.Time
	simSpend      float64 // simulated inference spend for paper mode
	dynCfg        *dynconfig.RuntimeConfig
	maxCompletion int
}

// New creates a new Agent.
func New(
	cfg *config.Config,
	client *httpclient.Client,
	ldgr *ledger.Ledger,
	x402mgr *x402.Payment,
	dash *dashboard.State,
	wis *wisdom.Tracker,
) *Agent {
	a := &Agent{
		cfg:       cfg,
		client:    client,
		ledger:    ldgr,
		x402:     x402mgr,
		dash:      dash,
		wisdom:    wis,
		startTime: time.Now(),
	}
	// Register paper-reset callback so the dashboard can reset the ledger.
	dash.SetResetPaperFunc(func() {
		ldgr.ResetPaper(cfg.StartingBankroll, cfg.Reserve)
		dash.UpdateBudget(types.BudgetInfo{
			Cash:    cfg.StartingBankroll,
			Reserve: cfg.Reserve,
			Equity:  cfg.StartingBankroll,
		})
		slog.Info("paper trading reset", "bankroll", cfg.StartingBankroll)
	})
	return a
}

// ResetCycleCount zeroes the session cycle counter.
func (a *Agent) ResetCycleCount() { a.cycleCount = 0 }

// RunCycle executes one agent cycle. Returns retry delay in seconds (0 = no retry).
func (a *Agent) RunCycle(ctx context.Context) int {
	// Count every cycle attempt, even if it fails to fetch markets or call LLM.
	a.cycleCount++
	a.dash.SetCycleCount(a.cycleCount)

	// Resolve dynamic config for active model.
	dc := dynconfig.ForModel(a.cfg.OaiModel)
	a.dynCfg = dc
	a.maxCompletion = dc.MaxCompletion
	a.dash.SetEfficiency(dc)

	marketLimit := a.cfg.MarketLimit
	if dc.MarketLimit > marketLimit {
		marketLimit = dc.MarketLimit
	}

	// 1+2. Fetch geoblock status and markets (parallel when possible).
	var geo types.GeoblockStatus
	var markets []types.MarketSnapshot

	if dc.CanParallelize() {
		var wg sync.WaitGroup
		wg.Add(2)
		go func() {
			defer wg.Done()
			geo = polymarket.FetchGeoblockStatus(ctx, a.client)
		}()
		go func() {
			defer wg.Done()
			markets = polymarket.FetchMarkets(ctx, a.client, marketLimit, 0, "liquidity")
		}()
		wg.Wait()
	} else {
		geo = polymarket.FetchGeoblockStatus(ctx, a.client)
		markets = polymarket.FetchMarkets(ctx, a.client, marketLimit, 0, "liquidity")
	}

	if len(markets) == 0 {
		slog.Warn("no markets fetched this cycle")
		a.dash.UpdateBudget(types.BudgetInfo{
			Cash:    a.ledger.Cash(),
			Reserve: a.ledger.Reserve(),
			Equity:  a.ledger.Cash(),
		})
		a.dash.SetAgentStatus("waiting for markets")
		return marketFailRetryDelaySec
	}

	// 3. Calculate equity and update dashboard.
	positions := a.ledger.Positions()
	equity := a.ledger.Equity(markets)
	budget := a.ledger.BudgetInfo(markets)

	a.dash.UpdateBudget(budget)
	a.dash.UpdatePositions(positions)
	a.dash.UpdateMarketSnapshots(markets)
	a.dash.SetGeoblock(geo.Blocked, geo.Country)
	a.dash.SetAgentStatus("running")
	a.dash.SetUptimeSeconds(int64(time.Since(a.startTime).Seconds()))

	slog.Info("ledger state",
		"cash", a.ledger.Cash(), "reserve", a.ledger.Reserve(),
		"equity", equity, "llm_spend", a.ledger.LlmSpend(),
		"positions", len(positions))

	// 4. Check if inference is affordable.
	estimatedCost := EstimatedChatCostUsdc()
	if !a.ledger.CanSpendOnInference(estimatedCost, markets) {
		slog.Warn("inference reserve reached",
			"cash", a.ledger.Cash(), "reserve", a.ledger.Reserve())
		return 0
	}

	paperOnly := a.cfg.PaperOnly || geo.Blocked

	// Hard stop: equity at or below reserve.
	if equity <= a.ledger.Reserve() {
		slog.Warn("HARD STOP: equity <= reserve, no new trades",
			"equity", equity, "reserve", a.ledger.Reserve())
		return 0
	}

	// 5. Build prompts.
	systemPrompt := BuildSystemPrompt(paperOnly, geo.Blocked, a.cfg.ToolUsageLevel(), a.wisdom)
	userPrompt := BuildUserPrompt(geo, budget, markets, positions, paperOnly, a.cfg.DailyLossLimit)

	// 5b. Proactive research: in Generous mode, auto-search news based on
	// market categories before the first LLM call so it starts with context.
	if a.cfg.ToolUsageLevel() >= 2 {
		provider := news.Provider(a.cfg.NewsProvider())
		research := ProactiveResearch(ctx, markets, provider, a.cfg.NewsAPIKey())
		if extra := BuildResearchContext(research); extra != "" {
			userPrompt = userPrompt[:len(userPrompt)-1] + extra + "}"
		}
		// Push headlines to dashboard ticker.
		var titles []string
		for _, r := range research {
			for _, n := range r.Results {
				if n.Title != "" {
					titles = append(titles, n.Title)
				}
			}
		}
		a.dash.PushHeadlines(titles)
	}

	// 6. Select model.
	baseURL := a.cfg.OaiURL
	useX402 := a.x402 != nil && a.x402.IsConfigured()
	cash := a.ledger.Cash()
	estCycles := int(96.0 * (cash / a.cfg.StartingBankroll))
	if estCycles < 4 {
		estCycles = 4
	}

	var modelID string
	if useX402 {
		sel := models.SelectModel(baseURL, models.Standard, cash, estCycles)
		if sel.Model != nil {
			modelID = sel.ModelID
			slog.Info("model selected", "name", sel.Model.Name, "price", sel.Price)
			a.dash.SetActiveModel(sel.Model.Name, sel.Price)
		}
	}
	// Show the configured model when x402 doesn't override it.
	if modelID == "" && a.cfg.OaiModel != "" {
		a.dash.SetActiveModel(a.cfg.OaiModel, 0)
	}

	// 7. Call LLM.
	responseText, usage, ok := a.ChatCompletion(ctx, systemPrompt, userPrompt, modelID)
	if !ok {
		a.dash.SetAgentStatus("llm_error")
		a.dash.SetNextRetrySec(llmFailRetryDelaySec)
		a.dash.PushEvent("state", string(a.dash.ToJSON()))
		return llmFailRetryDelaySec
	}
	a.dash.ClearError()
	a.spendForUsage(usage)

	// 8. Handle tool calls (capped by tool usage slider).
	maxToolCalls := 1 // balanced
	switch a.cfg.ToolUsageLevel() {
	case 0:
		maxToolCalls = 0 // frugal: skip tool calls entirely
	case 2:
		maxToolCalls = 3 // generous: proactive research + up to 3 LLM-driven rounds
	}

	var toolsUsed []string
	for toolRound := 0; toolRound < maxToolCalls; toolRound++ {
		toolCall := ParseToolCall(responseText)
		if !toolCall.Valid {
			break
		}

		canSpend := paperOnly || a.cfg.ToolUsageLevel() >= 2 || a.ledger.CanSpendOnInference(estimatedCost, markets)

		if toolCall.Tool == "search_markets" {
			toolMarkets := polymarket.FetchMarkets(ctx, a.client, toolCall.Limit, toolCall.Offset, toolCall.Order)
			if len(toolMarkets) == 0 || !canSpend {
				slog.Info("tool call skipped", "tool", "search_markets", "round", toolRound, "reason", "no_markets_or_budget")
				break
			}
			var followModel string
			if useX402 {
				sel := models.SelectModel(baseURL, models.Complex, a.ledger.Cash(), estCycles)
				if sel.Model != nil {
					followModel = sel.ModelID
				}
			}
			followUpPrompt := BuildFollowUpPrompt(userPrompt, toolMarkets)
			if dc.CanParallelize() && a.wisdom != nil {
				var wg sync.WaitGroup
				var text string
				var u2 types.UsageStats
				var ok2 bool
				wg.Add(2)
				go func() {
					defer wg.Done()
					text, u2, ok2 = a.ChatCompletion(ctx, systemPrompt, followUpPrompt, followModel)
				}()
				go func() {
					defer wg.Done()
					a.wisdom.CheckOutcomes(ctx)
				}()
				wg.Wait()
				if ok2 {
					a.spendForUsage(u2)
					responseText = text
					markets = toolMarkets
				} else {
					break
				}
			} else {
				if text, u2, ok2 := a.ChatCompletion(ctx, systemPrompt, followUpPrompt, followModel); ok2 {
					a.spendForUsage(u2)
					responseText = text
					markets = toolMarkets
				} else {
					break
				}
			}
		} else if toolCall.Tool == "search_news" && toolCall.Query != "" {
			newsProvider := news.Provider(a.cfg.NewsProvider())
			results, err := news.Search(newsProvider, a.cfg.NewsAPIKey(), toolCall.Query, 5)
			if err != nil {
				slog.Warn("news search failed", "error", err)
				break
			}
			if len(results) == 0 {
				break
			}
			// Push to dashboard ticker.
			var titles []string
			for _, r := range results {
				if r.Title != "" {
					titles = append(titles, r.Title)
				}
			}
			a.dash.PushHeadlines(titles)
			var followModel string
			if useX402 {
				sel := models.SelectModel(baseURL, models.Complex, a.ledger.Cash(), estCycles)
				if sel.Model != nil {
					followModel = sel.ModelID
				}
			}
			followUp := BuildNewsFollowUp(userPrompt, news.BuildNewsJSON(results))
			if dc.CanParallelize() && a.wisdom != nil {
				var wg sync.WaitGroup
				var text string
				var u2 types.UsageStats
				var ok2 bool
				wg.Add(2)
				go func() {
					defer wg.Done()
					text, u2, ok2 = a.ChatCompletion(ctx, systemPrompt, followUp, followModel)
				}()
				go func() {
					defer wg.Done()
					a.wisdom.CheckOutcomes(ctx)
				}()
				wg.Wait()
				if ok2 {
					a.spendForUsage(u2)
					responseText = text
				} else {
					break
				}
			} else {
				if text, u2, ok2 := a.ChatCompletion(ctx, systemPrompt, followUp, followModel); ok2 {
					a.spendForUsage(u2)
					responseText = text
				} else {
					break
				}
			}
		} else {
			slog.Warn("unknown tool call", "tool", toolCall.Tool, "round", toolRound)
			break
		}

		slog.Info("tool call completed", "tool", toolCall.Tool, "round", toolRound)
		toolsUsed = append(toolsUsed, toolCall.Tool)
	}

	// 9. Parse decision.
	decision := ParseDecision(responseText)
	if decision.Rationale == "invalid_json" {
		truncated := responseText
		if len(truncated) > 400 {
			truncated = truncated[:400]
		}
		slog.Warn("failed to parse LLM decision", "output", truncated)
	}
	equity = a.ledger.Equity(markets)
	maxPositionUsdc := equity * (maxPositionBps / 10000.0)

	slog.Info("decision",
		"type", decision.Type, "market", decision.MarketID,
		"edge", decision.EdgeBps, "confidence", decision.Confidence,
		"size_fraction", decision.SizeFraction, "rationale", decision.Rationale)

	// 10. Record decision for dashboard.
	now := time.Now().Unix()
	rec := types.DecisionRecord{
		Epoch:      now,
		Type:       decision.Type,
		MarketID:   decision.MarketID,
		Side:       decision.Side,
		Confidence: decision.Confidence,
		EdgeBps:    decision.EdgeBps,
		SizeUsdc:   equity * decision.SizeFraction,
		Rationale:  decision.Rationale,
		ToolsUsed:  toolsUsed,
	}
	if dm := types.FindMarket(markets, decision.MarketID); dm != nil {
		rec.MarketQuestion = dm.Question
	}

	a.dash.PushDecision(rec)

	// Parse and push market ratings.
	scouted := ParseMarketRatings(responseText, markets)
	if len(scouted) > 0 {
		a.dash.SetScoutedMarkets(scouted)
		if scoutedJSON, err := json.Marshal(scouted); err == nil {
			a.dash.PushEvent("scouted", string(scoutedJSON))
		}
	}

	a.dash.PushEvent("state", string(a.dash.ToJSON()))

	// 11. Track decisions for wisdom learning.
	a.trackWisdom(decision, scouted, markets, modelID)

	// Record equity snapshot.
	a.dash.RecordEquity(types.EquitySnapshot{Epoch: now, Equity: equity, Cash: a.ledger.Cash()})

	// 12. Execute decision.
	return a.executeDecision(decision, markets, paperOnly, equity, maxPositionUsdc)
}

// ChatCompletion calls the LLM. Returns content, usage, success.
func (a *Agent) ChatCompletion(ctx context.Context, systemPrompt, userPrompt, modelOverride string) (string, types.UsageStats, bool) {
	model := modelOverride
	if model == "" {
		model = a.cfg.OaiModel
	}
	baseURL := a.cfg.OaiURL
	apiKey := a.cfg.ApiKey
	useX402 := a.x402 != nil && a.x402.IsConfigured()

	// Resolve provider adapter.
	adapter := provider.FindByURL(baseURL)

	// Build request body.
	reqBody := buildLLMRequestBody(adapter, model, systemPrompt, userPrompt, a.maxCompletion)

	headers := map[string]string{
		"Content-Type": "application/json",
	}
	if !useX402 && apiKey != "" {
		headers["Authorization"] = "Bearer " + apiKey
	}

	url := strings.TrimRight(baseURL, "/") + "/v1/chat/completions"
	if strings.HasSuffix(strings.TrimRight(baseURL, "/"), "/v1") {
		url = strings.TrimRight(baseURL, "/") + "/chat/completions"
	}
	if adapter != nil {
		url = adapter.BuildInferenceURL(baseURL, model)
	}

	slog.Info("LLM call", "model", model, "url", url, "body_size", len(reqBody))

	// LLMPost handles retry internally.
	resp, err := a.client.LLMPost(ctx, url, reqBody, headers)

	// Handle x402 payment required.
	if resp != nil && resp.StatusCode == 402 && useX402 {
		slog.Info("x402: received 402, constructing payment...")
		payment, payErr := a.x402.MakePayment(resp.StatusCode, resp.Body, headersToMap(resp.Headers))
		if payErr != nil {
			slog.Error("x402 payment failed", "err", payErr)
			return "", types.UsageStats{}, false
		}
		headers["X-PAYMENT"] = payment
		resp, err = a.client.LLMPost(ctx, url, reqBody, headers)
		a.dash.SetInferenceSpend(a.x402.TotalSpentUsdc())
	}

	if err != nil {
		slog.Error("LLM request failed", "err", err)
		a.dash.SetLastError(fmt.Sprintf("LLM unreachable: %v", err))
		return "", types.UsageStats{}, false
	}

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		errMsg := fmt.Sprintf("LLM error (status=%d)", resp.StatusCode)
		slog.Error(errMsg, "body", string(resp.Body[:min(len(resp.Body), 200)]))
		a.dash.SetLastError(errMsg)
		return "", types.UsageStats{}, false
	}

	// Track simulated cost in paper mode.
	if !useX402 && a.cfg.PaperOnly {
		matchedPrice := models.LookupPrice(model)
		if matchedPrice > 0 {
			a.simSpend += matchedPrice
		} else {
			a.simSpend += simulatedCostPerRequest
		}
		a.dash.SetInferenceSpend(a.simSpend)
	}

	usage := ParseUsage(resp.Body)
	content := stripCodeFence(ExtractMessageContent(resp.Body))

	slog.Info("LLM response",
		"prompt_tokens", usage.PromptTokens,
		"completion_tokens", usage.CompletionTokens,
		"chars", len(content))

	if content == "" {
		return "", usage, false
	}
	return content, usage, true
}

func (a *Agent) spendForUsage(usage types.UsageStats) {
	pt := usage.PromptTokens
	if pt <= 0 {
		pt = estPromptTokens
	}
	ct := usage.CompletionTokens
	if ct <= 0 {
		ct = estCompletionTokens
	}
	_ = pt
	_ = ct
	a.ledger.DebitInference(EstimatedChatCostUsdc())
}

func (a *Agent) executeDecision(
	decision types.Decision,
	markets []types.MarketSnapshot,
	paperOnly bool,
	equity, maxPositionUsdc float64,
) int {
	// Handle close.
	if decision.Type == "paper_close" || decision.Type == "close" {
		pnl, ok := a.ledger.ClosePaperPosition(decision.MarketID, markets)
		if ok {
			slog.Info("closed position", "market", decision.MarketID, "pnl", pnl)
		}
		return 0
	}

	// Handle paper buy.
	if !isPaperBuy(decision.Type) || decision.MarketID == "" ||
		decision.Confidence < 0.65 || decision.EdgeBps < 150.0 {
		return 0
	}

	market := types.FindMarket(markets, decision.MarketID)
	if market == nil {
		slog.Info("market not in snapshot", "market_id", decision.MarketID)
		return 0
	}

	sizeFraction := decision.SizeFraction
	if sizeFraction > 0.02 {
		sizeFraction = 0.02
	}
	sizeUsdc := equity * sizeFraction
	if sizeUsdc > maxPositionUsdc {
		sizeUsdc = maxPositionUsdc
	}
	if sizeUsdc < 0 {
		sizeUsdc = 0
	}

	if a.ledger.OpenPaperPosition(*market, decision.Side, sizeUsdc) {
		slog.Info("opened paper position",
			"side", decision.Side, "market", market.Question, "size", sizeUsdc)
	}
	return 0
}

func (a *Agent) trackWisdom(
	decision types.Decision,
	scouted []types.ScoutedMarket,
	markets []types.MarketSnapshot,
	modelID string,
) {
	if a.wisdom == nil {
		return
	}

	if decision.Type == "hold" && len(scouted) > 0 {
		for _, s := range scouted {
			a.wisdom.RecordDecision(types.DecisionRecord{
				Epoch:          time.Now().Unix(),
				Type:           "hold",
				MarketID:       s.MarketID,
				MarketQuestion: s.Question,
				Side:           "hold",
				Confidence:     s.Confidence,
				EdgeBps:        s.EdgeBps,
			})
		}
	} else if decision.MarketID != "" {
		a.wisdom.RecordDecision(types.DecisionRecord{
			Epoch:          time.Now().Unix(),
			Type:           decision.Type,
			MarketID:       decision.MarketID,
			MarketQuestion: func() string {
				if m := types.FindMarket(markets, decision.MarketID); m != nil {
					return m.Question
				}
				return ""
			}(),
			Side:       decision.Side,
			Confidence: decision.Confidence,
			EdgeBps:    decision.EdgeBps,
		})
	}
}

func buildLLMRequestBody(adapter provider.Adapter, model, systemPrompt, userPrompt string, maxTokens int) []byte {
	if maxTokens <= 0 {
		maxTokens = maxCompletionTokens
	}
	body := map[string]interface{}{
		"temperature":      0.2,
		"max_tokens":       maxTokens,
		"reasoning_effort": "low",
		"response_format":  map[string]string{"type": "json_object"},
		"messages": []map[string]string{
			{"role": "system", "content": systemPrompt},
			{"role": "user", "content": userPrompt},
		},
	}

	if adapter == nil || adapter.ModelInBody() {
		body["model"] = model
	}

	data, _ := json.Marshal(body)
	return data
}

func isPaperBuy(t string) bool {
	return t == "paper_buy_yes" || t == "paper_buy_no" ||
		t == "buy_yes" || t == "buy_no"
}

func headersToMap(h map[string][]string) map[string]string {
	m := make(map[string]string, len(h))
	for k, v := range h {
		if len(v) > 0 {
			m[k] = v[0]
		}
	}
	return m
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
