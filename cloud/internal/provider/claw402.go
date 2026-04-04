package provider

import "strings"

// Claw402Data is the data adapter for claw402.ai.
var Claw402Data = &DataAdapter{
	AdapterName:     "claw402",
	AdapterDisplay:  "claw402.ai",
	BaseURL:         "https://claw402.ai/api/v1",
	CatalogEndpoint: "https://claw402.ai/api/v1/catalog",
	PricePerCall:    0.001,
	AuthMethod:      AuthX402,
	URLMatcher:      func(url string) bool { return strings.Contains(url, "claw402") },
}
