package news

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"time"
)

// Result is a single news/web search result.
type Result struct {
	Title   string `json:"title"`
	Snippet string `json:"snippet"`
	URL     string `json:"url"`
}

// Provider identifies the search backend.
type Provider string

const (
	ProviderTavily Provider = "tavily"
	ProviderBrave  Provider = "brave"
)

// Search performs a web search using the specified provider.
// Returns up to limit results. Returns an error on failure or missing API key.
func Search(provider Provider, apiKey, query string, limit int) ([]Result, error) {
	if apiKey == "" {
		return nil, fmt.Errorf("no news API key configured")
	}
	if limit <= 0 {
		limit = 3
	}
	switch provider {
	case ProviderBrave:
		return searchBrave(apiKey, query, limit)
	default:
		return searchTavily(apiKey, query, limit)
	}
}

func searchTavily(apiKey, query string, limit int) ([]Result, error) {
	body := map[string]interface{}{
		"query":          query,
		"api_key":        apiKey,
		"search_depth":   "basic",
		"include_answer": false,
		"max_results":    limit,
	}
	data, _ := json.Marshal(body)

	req, err := http.NewRequest("POST", "https://api.tavily.com/search", bytes.NewReader(data))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/json")

	client := &http.Client{Timeout: 15 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("tavily request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("tavily returned %d", resp.StatusCode)
	}

	raw, _ := io.ReadAll(resp.Body)

	var parsed struct {
		Results []struct {
			Title   string `json:"title"`
			Content string `json:"content"`
			URL     string `json:"url"`
		} `json:"results"`
	}
	if err := json.Unmarshal(raw, &parsed); err != nil {
		return nil, fmt.Errorf("tavily parse error: %w", err)
	}

	results := make([]Result, 0, len(parsed.Results))
	for _, r := range parsed.Results {
		snippet := r.Content
		if len(snippet) > 500 {
			snippet = snippet[:500]
		}
		results = append(results, Result{
			Title:   r.Title,
			Snippet: snippet,
			URL:     r.URL,
		})
		if len(results) >= limit {
			break
		}
	}
	return results, nil
}

func searchBrave(apiKey, query string, limit int) ([]Result, error) {
	u := fmt.Sprintf(
		"https://api.search.brave.com/res/v1/web/search?q=%s&count=%d&text_decorations=false&result_filter=news",
		url.QueryEscape(query), limit,
	)

	req, err := http.NewRequest("GET", u, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("X-Subscription-Token", apiKey)
	req.Header.Set("Accept", "application/json")

	client := &http.Client{Timeout: 15 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("brave request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("brave returned %d", resp.StatusCode)
	}

	raw, _ := io.ReadAll(resp.Body)

	var parsed struct {
		News *struct {
			Results []struct {
				Title       string `json:"title"`
				Description string `json:"description"`
				URL         string `json:"url"`
			} `json:"results"`
		} `json:"news"`
		Web *struct {
			Results []struct {
				Title       string `json:"title"`
				Description string `json:"description"`
				URL         string `json:"url"`
			} `json:"results"`
		} `json:"web"`
	}
	if err := json.Unmarshal(raw, &parsed); err != nil {
		return nil, fmt.Errorf("brave parse error: %w", err)
	}

	var results []Result
	addResults := func(items []struct {
		Title       string `json:"title"`
		Description string `json:"description"`
		URL         string `json:"url"`
	}) {
		for _, r := range items {
			if len(results) >= limit {
				break
			}
			snippet := r.Description
			if len(snippet) > 500 {
				snippet = snippet[:500]
			}
			results = append(results, Result{
				Title:   r.Title,
				Snippet: snippet,
				URL:     r.URL,
			})
		}
	}

	if parsed.News != nil {
		addResults(parsed.News.Results)
	}
	if len(results) < limit && parsed.Web != nil {
		addResults(parsed.Web.Results)
	}
	return results, nil
}

// BuildNewsJSON marshals news results into a JSON string for inclusion in LLM prompts.
func BuildNewsJSON(results []Result) string {
	data, _ := json.Marshal(results)
	return string(data)
}
