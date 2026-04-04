package provider

import (
	"encoding/json"
	"strings"
)

type x402engineAdapter struct{}

func (a *x402engineAdapter) Name() string           { return "x402engine" }
func (a *x402engineAdapter) DisplayName() string    { return "x402engine.app" }
func (a *x402engineAdapter) DefaultBaseURL() string { return "https://x402-gateway-production.up.railway.app" }
func (a *x402engineAdapter) CatalogURL() string     { return "https://x402engine.app/.well-known/x402.json" }
func (a *x402engineAdapter) Auth() AuthMethod       { return AuthX402 }
func (a *x402engineAdapter) ModelInBody() bool      { return false }

func (a *x402engineAdapter) MatchesURL(url string) bool {
	return strings.Contains(url, "x402-gateway") || strings.Contains(url, "x402engine")
}

func (a *x402engineAdapter) BuildInferenceURL(baseURL, modelID string) string {
	u := strings.TrimRight(baseURL, "/")
	return u + "/api/llm/" + modelID
}

func (a *x402engineAdapter) ParseCatalog(body []byte) ([]CatalogModel, error) {
	var resp struct {
		Categories struct {
			Compute []struct {
				ID    string `json:"id"`
				Name  string `json:"name"`
				Price string `json:"price"`
			} `json:"compute"`
		} `json:"categories"`
	}
	if err := json.Unmarshal(body, &resp); err != nil {
		return nil, err
	}
	var models []CatalogModel
	for _, c := range resp.Categories.Compute {
		if !strings.HasPrefix(c.ID, "llm-") {
			continue
		}
		shortID := c.ID[4:] // strip "llm-" prefix
		m := CatalogModel{
			ID:          shortID,
			DisplayName: c.Name,
			PricePerReq: parseDollarString(c.Price),
			ContextK:    128,
		}
		if m.DisplayName == "" {
			m.DisplayName = shortID
		}
		models = append(models, m)
	}
	return models, nil
}
