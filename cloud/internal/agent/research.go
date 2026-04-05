package agent

import (
	"context"
	"log/slog"
	"strings"
	"sync"

	"survaiv/internal/news"
	"survaiv/internal/types"
)

// researchQueries maps Polymarket categories (lowercase) to search query
// templates. Each template uses {question} as a placeholder for the market's
// question text. Keep queries short — search APIs work best with 3-8 words.
var researchQueries = map[string][]string{
	"sports": {
		"{teams} injury report latest",
		"{teams} recent form results",
		"{teams} coach manager news",
		"{teams} head to head record",
	},
	"football": {
		"{teams} injury report lineup",
		"{teams} recent form standings",
		"{teams} manager tactics news",
	},
	"soccer": {
		"{teams} injury report lineup",
		"{teams} recent form standings",
		"{teams} coach transfer news",
	},
	"basketball": {
		"{teams} injury report roster",
		"{teams} recent game results streak",
	},
	"mma": {
		"{fighter} fight record recent",
		"{fighter} training camp injury news",
		"{fighter} odds betting analysis",
	},
	"politics": {
		"{topic} latest polls approval rating",
		"{topic} policy announcement news",
		"{topic} political analysis prediction",
	},
	"elections": {
		"{topic} election polls latest",
		"{topic} campaign fundraising news",
		"{topic} voter sentiment swing states",
	},
	"geopolitics": {
		"{topic} diplomatic relations latest",
		"{topic} sanctions trade policy",
		"{topic} military tensions news",
	},
	"crypto": {
		"{topic} price prediction analysis",
		"{topic} regulatory SEC news",
		"{topic} on-chain metrics whale activity",
	},
	"finance": {
		"{topic} earnings forecast analyst",
		"{topic} market sentiment outlook",
	},
	"science": {
		"{topic} latest research findings",
		"{topic} expert analysis prediction",
	},
	"tech": {
		"{topic} product launch timeline",
		"{topic} company announcement news",
	},
	"entertainment": {
		"{topic} latest news prediction",
		"{topic} industry analysis betting odds",
	},
	"weather": {
		"{topic} forecast prediction models",
		"{topic} weather outlook latest",
	},
}

// maxResearchPerMarket caps how many searches we run per market to limit cost.
const maxResearchPerMarket = 2

// maxResearchTotal caps total searches across all markets in a single cycle.
const maxResearchTotal = 6

// maxResultsPerQuery caps results per search API call.
const maxResultsPerQuery = 3

// ResearchResult holds search results for a specific market.
type ResearchResult struct {
	MarketID string
	Query    string
	Results  []news.Result
}

// ProactiveResearch runs category-based news searches for each market and
// returns aggregated results. Only called in Generous mode.
func ProactiveResearch(
	ctx context.Context,
	markets []types.MarketSnapshot,
	provider news.Provider,
	apiKey string,
) []ResearchResult {
	if apiKey == "" {
		slog.Debug("proactive research skipped: no news API key")
		return nil
	}

	// Build query list: pick up to maxResearchPerMarket queries per market.
	type queryItem struct {
		marketID string
		query    string
	}
	var queries []queryItem

	for _, m := range markets {
		cat := strings.ToLower(m.Category)
		templates := pickTemplates(cat, m.Question)
		topic := extractTopic(m.Question)

		for _, tmpl := range templates {
			if len(queries) >= maxResearchTotal {
				break
			}
			q := expandTemplate(tmpl, topic, m.Question)
			queries = append(queries, queryItem{marketID: m.ID, query: q})
		}
	}

	if len(queries) == 0 {
		return nil
	}

	slog.Info("proactive research", "queries", len(queries), "markets", len(markets))

	// Execute searches in parallel (bounded).
	var mu sync.Mutex
	var wg sync.WaitGroup
	sem := make(chan struct{}, 3) // max 3 concurrent searches
	var results []ResearchResult

	for _, qi := range queries {
		qi := qi
		wg.Add(1)
		go func() {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()

			res, err := news.Search(provider, apiKey, qi.query, maxResultsPerQuery)
			if err != nil {
				slog.Debug("research search failed", "query", qi.query, "error", err)
				return
			}
			if len(res) == 0 {
				return
			}

			mu.Lock()
			results = append(results, ResearchResult{
				MarketID: qi.marketID,
				Query:    qi.query,
				Results:  res,
			})
			mu.Unlock()
		}()
	}
	wg.Wait()

	slog.Info("proactive research complete", "results", len(results))
	return results
}

// pickTemplates selects query templates for a given category, with fallback
// to generic templates. Returns at most maxResearchPerMarket templates.
func pickTemplates(category, question string) []string {
	// Try exact category match first.
	if ts, ok := researchQueries[category]; ok {
		return capSlice(ts, maxResearchPerMarket)
	}

	// Heuristic: detect sports from question keywords.
	qLower := strings.ToLower(question)
	sportKeywords := []string{" vs ", " vs. ", "o/u ", "over/under",
		"championship", "ufc", "nba", "nfl", "mlb", "nhl",
		"premier league", "la liga", "serie a", "bundesliga",
		"champions league", "world cup"}
	for _, kw := range sportKeywords {
		if strings.Contains(qLower, kw) {
			return capSlice(researchQueries["sports"], maxResearchPerMarket)
		}
	}

	// Detect politics/elections.
	politicsKeywords := []string{"president", "election", "senate", "congress",
		"governor", "democrat", "republican", "parliament", "prime minister",
		"vote", "ballot", "nominee", "cabinet", "impeach"}
	for _, kw := range politicsKeywords {
		if strings.Contains(qLower, kw) {
			return capSlice(researchQueries["politics"], maxResearchPerMarket)
		}
	}

	// Detect geopolitics.
	geoKeywords := []string{"war", "invasion", "sanctions", "nato",
		"treaty", "ceasefire", "nuclear", "missile", "leadership change",
		"regime", "diplomatic"}
	for _, kw := range geoKeywords {
		if strings.Contains(qLower, kw) {
			return capSlice(researchQueries["geopolitics"], maxResearchPerMarket)
		}
	}

	// Detect crypto.
	cryptoKeywords := []string{"bitcoin", "ethereum", "btc", "eth", "crypto",
		"token", "defi", "blockchain"}
	for _, kw := range cryptoKeywords {
		if strings.Contains(qLower, kw) {
			return capSlice(researchQueries["crypto"], maxResearchPerMarket)
		}
	}

	// Fallback: use the question itself as a single search.
	return []string{"{topic} latest news prediction"}
}

// extractTopic pulls a concise topic from the market question.
// Strips common prefixes like "Will", "Is", "Does", "Can" and trims trailing "?".
func extractTopic(question string) string {
	q := strings.TrimSpace(question)
	q = strings.TrimSuffix(q, "?")

	// Strip leading question words.
	for _, prefix := range []string{
		"Will ", "Is ", "Does ", "Can ", "Has ", "Are ", "Do ",
		"Did ", "Should ", "Would ", "Could ",
	} {
		if strings.HasPrefix(q, prefix) {
			q = q[len(prefix):]
			break
		}
	}

	// For sports "X vs Y" questions, keep both team names.
	if idx := strings.Index(q, ":"); idx > 0 {
		q = q[:idx] // drop sub-question like ": O/U 2.5"
	}

	// Cap length for search API.
	if len(q) > 80 {
		q = q[:80]
	}
	return strings.TrimSpace(q)
}

// expandTemplate replaces {topic}, {teams}, {fighter} placeholders with the
// extracted topic string.
func expandTemplate(tmpl, topic, fullQuestion string) string {
	r := strings.NewReplacer(
		"{topic}", topic,
		"{teams}", topic,
		"{fighter}", topic,
		"{question}", fullQuestion,
	)
	return r.Replace(tmpl)
}

// BuildResearchContext formats proactive research results as a JSON string
// suitable for injection into the user prompt.
func BuildResearchContext(results []ResearchResult) string {
	if len(results) == 0 {
		return ""
	}
	var b strings.Builder
	b.WriteString(`,"proactive_research":[`)
	first := true
	for _, r := range results {
		for _, nr := range r.Results {
			if !first {
				b.WriteByte(',')
			}
			first = false
			b.WriteString(`{"market_id":"`)
			b.WriteString(jsonEscape(r.MarketID))
			b.WriteString(`","query":"`)
			b.WriteString(jsonEscape(r.Query))
			b.WriteString(`","title":"`)
			b.WriteString(jsonEscape(nr.Title))
			b.WriteString(`","snippet":"`)
			snippet := nr.Snippet
			if len(snippet) > 300 {
				snippet = snippet[:300]
			}
			b.WriteString(jsonEscape(snippet))
			b.WriteString(`"}`)
		}
	}
	b.WriteByte(']')
	return b.String()
}

func capSlice(s []string, max int) []string {
	if len(s) <= max {
		return s
	}
	return s[:max]
}
