package agent

import (
	"encoding/json"
	"strings"
	"time"

	"survaiv/internal/types"
)

// extractFirstJSONObject scans text for the first valid JSON object,
// preferring one that contains a "type" key (common in LLM responses).
// Falls back to the first valid JSON object, or the original text.
func extractFirstJSONObject(text string) string {
	text = strings.TrimSpace(text)
	// Fast path: text is already valid JSON.
	if len(text) > 0 && text[0] == '{' {
		var m map[string]interface{}
		if json.Unmarshal([]byte(text), &m) == nil {
			return text
		}
	}

	var firstValid string
	for i := 0; i < len(text); i++ {
		if text[i] != '{' {
			continue
		}
		depth := 0
		inStr := false
		esc := false
		end := -1
		for j := i; j < len(text); j++ {
			ch := text[j]
			if esc {
				esc = false
				continue
			}
			if ch == '\\' && inStr {
				esc = true
				continue
			}
			if ch == '"' {
				inStr = !inStr
				continue
			}
			if inStr {
				continue
			}
			if ch == '{' {
				depth++
			} else if ch == '}' {
				depth--
				if depth == 0 {
					end = j
					break
				}
			}
		}
		if end > i {
			candidate := text[i : end+1]
			var m map[string]interface{}
			if json.Unmarshal([]byte(candidate), &m) == nil {
				if _, hasType := m["type"]; hasType {
					return candidate
				}
				if firstValid == "" {
					firstValid = candidate
				}
			}
		}
	}
	if firstValid != "" {
		return firstValid
	}
	return text
}

// ParseDecision parses an LLM JSON response into a Decision.
// Returns a default hold decision on malformed input.
func ParseDecision(text string) types.Decision {
	text = extractFirstJSONObject(stripCodeFence(text))

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
	text = extractFirstJSONObject(stripCodeFence(text))

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
	text = extractFirstJSONObject(stripCodeFence(text))

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

// ExtractMessageContent pulls the assistant message text from an LLM response,
// with fallbacks for reasoning_content, streaming delta, and text completions.
func ExtractMessageContent(body []byte) string {
	var raw struct {
		Choices []struct {
			Message *struct {
				Content          *string `json:"content"`
				ReasoningContent *string `json:"reasoning_content"`
			} `json:"message"`
			Delta *struct {
				Content *string `json:"content"`
			} `json:"delta"`
			Text *string `json:"text"`
		} `json:"choices"`
	}
	if err := json.Unmarshal(body, &raw); err != nil || len(raw.Choices) == 0 {
		return ""
	}
	c := raw.Choices[0]
	if c.Message != nil {
		if c.Message.Content != nil && *c.Message.Content != "" {
			return *c.Message.Content
		}
		if c.Message.ReasoningContent != nil && *c.Message.ReasoningContent != "" {
			return *c.Message.ReasoningContent
		}
	}
	if c.Delta != nil && c.Delta.Content != nil && *c.Delta.Content != "" {
		return *c.Delta.Content
	}
	if c.Text != nil && *c.Text != "" {
		return *c.Text
	}
	return ""
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
