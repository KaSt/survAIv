package provider

import (
	"encoding/json"
	"strconv"
	"strings"
)

type tx402Adapter struct{}

func (a *tx402Adapter) Name() string           { return "tx402" }
func (a *tx402Adapter) DisplayName() string    { return "tx402.ai" }
func (a *tx402Adapter) DefaultBaseURL() string { return "https://tx402.ai/v1" }
func (a *tx402Adapter) CatalogURL() string     { return "https://tx402.ai/v1/models" }
func (a *tx402Adapter) Auth() AuthMethod       { return AuthX402 }
func (a *tx402Adapter) ModelInBody() bool      { return true }

func (a *tx402Adapter) MatchesURL(url string) bool {
	return strings.Contains(url, "tx402.ai")
}

func (a *tx402Adapter) BuildInferenceURL(baseURL, _ string) string {
	u := strings.TrimRight(baseURL, "/")
	if strings.HasSuffix(u, "/v1") {
		return u + "/chat/completions"
	}
	return u + "/v1/chat/completions"
}

func (a *tx402Adapter) ParseCatalog(body []byte) ([]CatalogModel, error) {
	var resp struct {
		Data []struct {
			ID      string `json:"id"`
			Pricing struct {
				EstPerReq string `json:"estimated_per_request"`
			} `json:"pricing"`
			ContextWindow int `json:"context_window"`
		} `json:"data"`
	}
	if err := json.Unmarshal(body, &resp); err != nil {
		return nil, err
	}
	var models []CatalogModel
	for _, d := range resp.Data {
		m := CatalogModel{
			ID:          d.ID,
			DisplayName: stripOrgPrefix(d.ID),
			PricePerReq: parseDollarString(d.Pricing.EstPerReq),
			ContextK:    d.ContextWindow / 1000,
		}
		if m.ContextK == 0 {
			m.ContextK = 128
		}
		models = append(models, m)
	}
	return models, nil
}

func stripOrgPrefix(id string) string {
	if i := strings.LastIndex(id, "/"); i >= 0 {
		return id[i+1:]
	}
	return id
}

func parseDollarString(s string) float64 {
	s = strings.TrimSpace(s)
	s = strings.TrimPrefix(s, "$")
	v, _ := strconv.ParseFloat(s, 64)
	return v
}
