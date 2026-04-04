package wisdom

// defaultTracker is the package-level singleton for convenience functions.
var defaultTracker *Tracker

// SetDefault sets the package-level default tracker.
func SetDefault(t *Tracker) {
	defaultTracker = t
}

// GetWisdom returns the current wisdom text from the default tracker.
func GetWisdom() string {
	if defaultTracker == nil {
		return ""
	}
	defaultTracker.mu.RLock()
	defer defaultTracker.mu.RUnlock()
	return defaultTracker.wisdom
}

// StatsJSON returns wisdom stats as JSON from the default tracker.
// This is a package-level wrapper; the method (t *Tracker) StatsJSON() still works.
func StatsJSON() string {
	if defaultTracker == nil {
		return "{}"
	}
	return defaultTracker.StatsJSON()
}

// GetCustomRules returns the current custom rules from the default tracker.
func GetCustomRules() string {
	if defaultTracker == nil {
		return ""
	}
	return defaultTracker.GetCustomRules()
}

// SetCustomRules updates custom rules on the default tracker.
func SetCustomRules(rules string) {
	if defaultTracker != nil {
		defaultTracker.SetCustomRules(rules)
	}
}

// ExportKnowledge exports knowledge from the default tracker.
func ExportKnowledge() ([]byte, error) {
	if defaultTracker == nil {
		return nil, nil
	}
	return defaultTracker.ExportKnowledge()
}

// ImportKnowledge imports knowledge into the default tracker.
func ImportKnowledge(data []byte) error {
	if defaultTracker == nil {
		return nil
	}
	return defaultTracker.ImportKnowledge(data)
}

// SetFrozen enables or disables learning on the default tracker.
func SetFrozen(frozen bool) {
	if defaultTracker != nil {
		defaultTracker.SetFrozen(frozen)
	}
}
