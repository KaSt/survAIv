package provider

// AuthMethod describes how a provider authenticates requests.
type AuthMethod int

const (
	AuthApiKey AuthMethod = iota
	AuthX402
	AuthNone
)

// CatalogModel is a model entry parsed from a provider's discovery endpoint.
type CatalogModel struct {
	ID          string
	DisplayName string
	PricePerReq float64
	ContextK    int
}

// Adapter describes how to talk to one LLM inference provider.
type Adapter interface {
	Name() string
	DisplayName() string
	DefaultBaseURL() string
	CatalogURL() string
	Auth() AuthMethod
	ModelInBody() bool
	MatchesURL(url string) bool
	BuildInferenceURL(baseURL, modelID string) string
	ParseCatalog(body []byte) ([]CatalogModel, error)
}

// DataAdapter describes a non-LLM data API using x402.
type DataAdapter struct {
	AdapterName     string
	AdapterDisplay  string
	BaseURL         string
	CatalogEndpoint string
	PricePerCall    float64
	AuthMethod      AuthMethod
	URLMatcher      func(string) bool
}

func (d *DataAdapter) Name() string       { return d.AdapterName }
func (d *DataAdapter) DisplayName() string { return d.AdapterDisplay }
func (d *DataAdapter) MatchesURL(url string) bool {
	if d.URLMatcher != nil {
		return d.URLMatcher(url)
	}
	return false
}
