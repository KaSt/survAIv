package agent

import (
	"encoding/json"
	"strings"
	"time"

	"survaiv/internal/types"
)

// ParseDecision parses an LLM JSON response into a Decision.
// Returns a default hold decision on malformed input.
func ParseDecision(text string) types.Decision {
	text = stripCodeFence(text)

	var raw struct {
		Type         string  `json:"type"`
		MarketID     string  `json:"market_id"`
		EdgeBps      float64 `json:"edge_bps"`
		Confidence   float64 `json:"confidence"`
		SizeFraction float64 `json:"size_fraction"`
		Rationale    string  `json:"rationale"`
	}

	if err := json.Unmarshal([]byte(text), &raw); err != nil {
		return types.Decision{Type: "hold", Rationale: "invalid_json"}
	}

	d := types.Decision{
		Type:         raw.Type,
		MarketID:     raw.MarketID,
		EdgeBps:      raw.EdgeBps,
		Confidence:   raw.Confidence,
		SizeFraction: raw.SizeFraction,
		Rationale:    raw.Rationale,
	}

	switch d.Type {
	case "paper_buy_yes", "buy_yes":
		d.Side = "yes"
	case "paper_buy_no", "buy_no":
		d.Side = "no"
	}

	if d.Type == "" {
		d.Type = "hold"
		d.Rationale = "invalid_json"
	}

	return d
}

// ParseToolCall parses an LLM JSON response for a tool_call.
func ParseToolCall(text string) types.ToolCall {
	text = stripCodeFence(text)

	var raw struct {
		Type      string          `json:"type"`
		Tool      string          `json:"tool"`
		Arguments json.RawMessage `json:"arguments"`
	}

	if err := json.Unmarshal([]byte(text), &raw); err != nil {
		return types.ToolCall{}
	}

	if raw.Type != "tool_call" {
		return types.ToolCall{}
	}
	if raw.Tool != "search_markets" && raw.Tool != "search_news" {
		return types.ToolCall{}
	}

	tc := types.ToolCall{
		Valid: true,
		Tool:  raw.Tool,
	}

	if raw.Tool == "search_markets" {
		var args struct {
			Order  string  `json:"order"`
			Limit  float64 `json:"limit"`
			Offset float64 `json:"offset"`
		}
		if err := json.Unmarshal(raw.Arguments, &args); err == nil {
			if args.Order != "" {
				tc.Order = args.Order
			}
			if args.Limit > 0 {
				tc.Limit = int(args.Limit)
				if tc.Limit > 12 {
					tc.Limit = 12
				}
			}
			if args.Offset >= 0 {
				tc.Offset = int(args.Offset)
			}
		}
	} else if raw.Tool == "search_news" {
		var args struct {
			Query string `json:"query"`
		}
		if err := json.Unmarshal(raw.Arguments, &args); err == nil {
			tc.Query = args.Query
		}
	}

	return tc
}

// ParseMarketRatings extracts the market_ratings array from the LLM response.
func ParseMarketRatings(text string, markets []types.MarketSnapshot) []types.ScoutedMarket {
	text = stripCodeFence(text)

	var raw struct {
		Ratings []struct {
			ID         string  `json:"id"`
			Signal     string  `json:"signal"`
			EdgeBps    float64 `json:"edge_bps"`
			Confidence float64 `json:"confidence"`
			Note       string  `json:"note"`
		} `json:"market_ratings"`
	}

	if err := json.Unmarshal([]byte(text), &raw); err != nil || len(raw.Ratings) == 0 {
		return nil
	}

	now := time.Now().Unix()
	result := make([]types.ScoutedMarket, 0, len(raw.Ratings))

	for _, r := range raw.Ratings {
		sm := types.ScoutedMarket{
			Epoch:      now,
			MarketID:   r.ID,
			Signal:     r.Signal,
			EdgeBps:    r.EdgeBps,
			Confidence: r.Confidence,
			Note:       r.Note,
		}

		if ms := types.FindMarket(markets, r.ID); ms != nil {
			sm.Question = ms.Question
			sm.YesPrice = ms.YesPrice
			sm.Volume = ms.Volume
			sm.Liquidity = ms.Liquidity
		}

		if sm.Signal == "" {
			sm.Signal = "neutral"
		}

		result = append(result, sm)
	}

	return result
}

// ParseUsage extracts token usage from an LLM response.
func ParseUsage(body []byte) types.UsageStats {
	var raw struct {
		Usage struct {
			PromptTokens     int `json:"prompt_tokens"`
			CompletionTokens int `json:"completion_tokens"`
		} `json:"usage"`
	}
	json.Unmarshal(body, &raw)
	return types.UsageStats{
		PromptTokens:     raw.Usage.PromptTokens,
		CompletionTokens: raw.Usage.CompletionTokens,
	}
}

// ExtractMessageContent pulls the assistant message text from an LLM response.
func ExtractMessageContent(body []byte) string {
	var raw struct {
		Choices []struct {
			Message struct {
				Content string `json:"content"`
			} `json:"message"`
		} `json:"choices"`
	}
	if err := json.Unmarshal(body, &raw); err != nil || len(raw.Choices) == 0 {
		return ""
	}
	return raw.Choices[0].Message.Content
}

// stripCodeFence removes markdown code fence wrappers if present.
func stripCodeFence(s string) string {
	s = strings.TrimSpace(s)
	if strings.HasPrefix(s, "```json") {
		s = strings.TrimPrefix(s, "```json")
	} else if strings.HasPrefix(s, "```") {
		s = strings.TrimPrefix(s, "```")
	}
	if strings.HasSuffix(s, "```") {
		s = strings.TrimSuffix(s, "```")
	}
	return strings.TrimSpace(s)
}
