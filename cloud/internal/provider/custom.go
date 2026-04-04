package provider

import "strings"

type customAdapter struct{}

func (a *customAdapter) Name() string           { return "custom" }
func (a *customAdapter) DisplayName() string    { return "Custom endpoint" }
func (a *customAdapter) DefaultBaseURL() string { return "" }
func (a *customAdapter) CatalogURL() string     { return "" }
func (a *customAdapter) Auth() AuthMethod       { return AuthApiKey }
func (a *customAdapter) ModelInBody() bool      { return true }

func (a *customAdapter) MatchesURL(_ string) bool { return false }

func (a *customAdapter) BuildInferenceURL(baseURL, _ string) string {
	u := strings.TrimRight(baseURL, "/")
	if strings.HasSuffix(u, "/chat/completions") {
		return u
	}
	if strings.HasSuffix(u, "/v1") {
		return u + "/chat/completions"
	}
	return u + "/v1/chat/completions"
}

func (a *customAdapter) ParseCatalog(_ []byte) ([]CatalogModel, error) {
	return nil, nil
}
