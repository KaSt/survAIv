#!/usr/bin/env bash
# survaiv cloud — build & run script for macOS / Linux
set -euo pipefail
cd "$(dirname "$0")"

VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo dev)
LDFLAGS="-s -w -X main.Version=${VERSION}"
export CGO_ENABLED=1

case "${1:-build}" in
  deps)
    echo "▸ Downloading dependencies…"
    go mod download
    ;;
  build)
    echo "▸ Building survaiv (${VERSION})…"
    go mod download
    go build -ldflags "${LDFLAGS}" -o survaiv .
    echo "✓ Built ./survaiv"
    ;;
  run)
    echo "▸ Building & running survaiv (TUI mode)…"
    go mod download
    go build -ldflags "${LDFLAGS}" -o survaiv .
    exec ./survaiv
    ;;
  headless)
    echo "▸ Building & running survaiv (headless / dashboard only)…"
    go mod download
    go build -ldflags "${LDFLAGS}" -o survaiv .
    exec ./survaiv --headless
    ;;
  test)
    echo "▸ Running tests…"
    go test ./...
    ;;
  clean)
    echo "▸ Cleaning…"
    rm -f survaiv survaiv-*
    echo "✓ Clean"
    ;;
  *)
    echo "Usage: $0 {build|run|headless|test|clean|deps}"
    exit 1
    ;;
esac
