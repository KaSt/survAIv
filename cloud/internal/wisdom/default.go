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
