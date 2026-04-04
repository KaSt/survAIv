package config

import (
	"bufio"
	"log/slog"
	"os"
	"path/filepath"
	"strings"
)

// parseConfigFile reads a TOML-like config file into a flat key→value map.
// Supported syntax:
//
//	# comment
//	key = "value"   (quotes stripped)
//	key = value     (bare values trimmed)
//	[section]       (ignored — reserved for future use)
func parseConfigFile(path string) map[string]string {
	f, err := os.Open(path)
	if err != nil {
		slog.Warn("config file not readable", "path", path, "err", err)
		return nil
	}
	defer f.Close()

	vals := make(map[string]string)
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || line[0] == '#' || line[0] == '[' {
			continue
		}
		k, v, ok := parseKeyValue(line)
		if ok {
			vals[k] = v
		}
	}

	slog.Info("config file loaded", "path", path, "keys", len(vals))
	return vals
}

func parseKeyValue(line string) (string, string, bool) {
	eq := strings.IndexByte(line, '=')
	if eq < 1 {
		return "", "", false
	}
	key := strings.TrimSpace(line[:eq])
	val := strings.TrimSpace(line[eq+1:])

	// Strip surrounding quotes.
	if len(val) >= 2 && val[0] == '"' && val[len(val)-1] == '"' {
		val = val[1 : len(val)-1]
	}
	if len(val) >= 2 && val[0] == '\'' && val[len(val)-1] == '\'' {
		val = val[1 : len(val)-1]
	}
	return key, val, true
}

// findConfigFile searches standard locations for a config file.
// Priority: explicit path > ./survaiv.toml > ~/.config/survaiv/config.toml
func findConfigFile(explicit string) string {
	if explicit != "" {
		if _, err := os.Stat(explicit); err == nil {
			return explicit
		}
		slog.Warn("specified config file not found", "path", explicit)
		return ""
	}

	// Current directory.
	if _, err := os.Stat("survaiv.toml"); err == nil {
		return "survaiv.toml"
	}

	// XDG / home config directory.
	if home, err := os.UserHomeDir(); err == nil {
		p := filepath.Join(home, ".config", "survaiv", "config.toml")
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}

	return ""
}
