package dynconfig

import (
	"runtime"
	"strings"

	"survaiv/internal/models"
)

type RuntimeConfig struct {
	Platform        string
	CPUCores        int
	PromptBudget    int
	MaxCompletion   int
	MarketLimit     int
	WisdomBudget    int
	ParallelWorkers int
	EfficiencyScore int
	ModelContextK   int
}

type EfficiencyBreakdown struct {
	Context     int `json:"context"`
	Parallelism int `json:"parallelism"`
	Memory      int `json:"memory"`
	Coverage    int `json:"coverage"`
	Wisdom      int `json:"wisdom"`
}

func (rc *RuntimeConfig) CanParallelize() bool {
	return rc.CPUCores >= 4
}

func (rc *RuntimeConfig) WorkerCount() int {
	w := rc.CPUCores / 2
	if w > 4 {
		w = 4
	}
	if w < 1 {
		w = 1
	}
	return w
}

func ForModel(modelName string) *RuntimeConfig {
	rc := &RuntimeConfig{
		Platform:     "cloud",
		CPUCores:     runtime.NumCPU(),
		WisdomBudget: 8192,
	}

	rc.ModelContextK = lookupContextK(modelName)

	if rc.ModelContextK > 0 {
		budget := int(float64(rc.ModelContextK) * 1000 * 0.6)
		if budget > 32000 {
			budget = 32000
		}
		rc.PromptBudget = budget
	} else {
		rc.PromptBudget = 16000
	}
	if rc.PromptBudget < 2000 {
		rc.PromptBudget = 2000
	}

	// Cloud has no memory constraints — be generous with completion tokens.
	// Reasoning models (Claude, o1, etc.) use thinking tokens from this budget.
	if rc.ModelContextK >= 128 {
		rc.MaxCompletion = 16384
	} else if rc.ModelContextK >= 32 {
		rc.MaxCompletion = 8192
	} else {
		// Unknown or small model — still generous on cloud.
		rc.MaxCompletion = 8192
	}

	ml := rc.PromptBudget / 1500
	if ml > 50 {
		ml = 50
	}
	if ml < 4 {
		ml = 4
	}
	rc.MarketLimit = ml

	rc.ParallelWorkers = rc.WorkerCount()
	rc.EfficiencyScore = computeEfficiency(rc)

	return rc
}

func (rc *RuntimeConfig) Breakdown() EfficiencyBreakdown {
	return computeBreakdown(rc)
}

func lookupContextK(modelName string) int {
	if modelName == "" {
		return 0
	}
	query := strings.ToLower(modelName)

	all := models.All()
	for _, m := range all {
		if m.ContextK > 0 {
			if matchesModel(query, m) {
				return m.ContextK
			}
		}
	}
	return 0
}

func matchesModel(query string, m models.ModelInfo) bool {
	q := normalize(query)
	if m.Tx402ID != "" && normalize(stripOrg(m.Tx402ID)) == q {
		return true
	}
	if m.EngineID != "" && normalize(m.EngineID) == q {
		return true
	}
	if m.Tx402ID != "" {
		s := normalize(stripOrg(m.Tx402ID))
		if strings.Contains(s, q) || strings.Contains(q, s) {
			return true
		}
	}
	if m.EngineID != "" {
		s := normalize(m.EngineID)
		if strings.Contains(s, q) || strings.Contains(q, s) {
			return true
		}
	}
	return false
}

func normalize(s string) string {
	s = strings.ToLower(s)
	s = strings.Map(func(r rune) rune {
		if r == ':' || r == '_' || r == '.' {
			return '-'
		}
		return r
	}, s)
	return s
}

func stripOrg(id string) string {
	if i := strings.LastIndex(id, "/"); i >= 0 {
		return id[i+1:]
	}
	return id
}

func computeBreakdown(rc *RuntimeConfig) EfficiencyBreakdown {
	var bd EfficiencyBreakdown

	// Context capacity (0-30)
	ratio := float64(rc.PromptBudget) / 32000.0
	if ratio > 1.0 {
		ratio = 1.0
	}
	bd.Context = int(30.0 * ratio)

	// Parallelism (0-20)
	wRatio := float64(rc.ParallelWorkers) / 4.0
	if wRatio > 1.0 {
		wRatio = 1.0
	}
	bd.Parallelism = int(20.0 * wRatio)

	// Memory headroom (0-15)
	bd.Memory = estimateMemoryScore()

	// Market coverage (0-20)
	mRatio := float64(rc.MarketLimit) / 50.0
	if mRatio > 1.0 {
		mRatio = 1.0
	}
	bd.Coverage = int(20.0 * mRatio)

	// Wisdom capacity (0-15)
	wisRatio := float64(rc.WisdomBudget) / 8192.0
	if wisRatio > 1.0 {
		wisRatio = 1.0
	}
	bd.Wisdom = int(15.0 * wisRatio)

	return bd
}

func computeEfficiency(rc *RuntimeConfig) int {
	bd := computeBreakdown(rc)
	return bd.Context + bd.Parallelism + bd.Memory + bd.Coverage + bd.Wisdom
}

func estimateMemoryScore() int {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	avail := m.Sys - m.HeapInuse
	switch {
	case avail > 1<<30: // > 1GB
		return 15
	case avail > 256<<20: // > 256MB
		return 10
	case avail > 64<<20: // > 64MB
		return 5
	default:
		return 0
	}
}

type PlatformBaseline struct {
	Name  string
	Key   string
	Score int
}

func KnownPlatforms() []PlatformBaseline {
	return []PlatformBaseline{
		{Name: "ESP32-C3 OTA", Key: "esp32_c3_ota", Score: 12},
		{Name: "ESP32-C3", Key: "esp32_c3", Score: 22},
		{Name: "ESP32-S3", Key: "esp32_s3", Score: 38},
	}
}
