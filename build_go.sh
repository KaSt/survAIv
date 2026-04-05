#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/cloud"
OUT_DIR="$SCRIPT_DIR/build"

# ── Target registry ───────────────────────────────────────────
# name | GOOS | GOARCH | extra env | description
TARGETS=(
  "linux-amd64|linux|amd64||Server / desktop x86-64"
  "linux-arm64|linux|arm64||Raspberry Pi 4/5 (64-bit)"
  "linux-armv6|linux|arm|GOARM=6|Raspberry Pi Zero / 1 / 1W"
  "linux-armv7|linux|arm|GOARM=7|Raspberry Pi 2/3/4 (32-bit)"
  "linux-mipsle|linux|mipsle|GOMIPS=softfloat|Onion Omega 2+"
  "linux-mips|linux|mips|GOMIPS=softfloat|Onion Omega 1"
  "darwin-amd64|darwin|amd64||macOS Intel"
  "darwin-arm64|darwin|arm64||macOS Apple Silicon"
  "windows-amd64|windows|amd64||Windows x86-64"
)

# ── Functions ─────────────────────────────────────────────────
list_targets() {
  echo "Available targets:"
  echo ""
  printf "  %-18s  %s\n" "TARGET" "DESCRIPTION"
  printf "  %-18s  %s\n" "──────" "───────────"
  for entry in "${TARGETS[@]}"; do
    IFS='|' read -r name _ _ _ desc <<< "$entry"
    printf "  %-18s  %s\n" "$name" "$desc"
  done
  echo ""
  echo "Special: --all builds every target"
}

usage() {
  cat <<EOF
Usage: ./build_go.sh [OPTIONS] [TARGET ...]

Build survaiv cloud agent as a static binary for one or more targets.

Options:
  --all            Build all targets
  --list           List available targets
  --out <dir>      Output directory (default: ./build)
  --strip          Strip debug symbols (default, use --no-strip to disable)
  --no-strip       Keep debug symbols (larger binary, better stack traces)
  --clean          Remove output directory before building
  -h, --help       Show this help

Targets:
  linux-amd64      Server / desktop x86-64
  linux-arm64      Raspberry Pi 4/5 (64-bit)
  linux-armv6      Raspberry Pi Zero / 1 / 1W
  linux-armv7      Raspberry Pi 2/3/4 (32-bit)
  linux-mipsle     Onion Omega 2+
  linux-mips       Onion Omega 1
  darwin-amd64     macOS Intel
  darwin-arm64     macOS Apple Silicon
  windows-amd64    Windows x86-64

Examples:
  ./build_go.sh                          # Build for current platform
  ./build_go.sh linux-arm64              # Build for Pi 4/5
  ./build_go.sh linux-armv6 linux-mipsle # Build for Pi Zero + Omega2+
  ./build_go.sh --all                    # Build everything
  ./build_go.sh --all --out dist         # Build everything into ./dist
EOF
}

build_target() {
  local name="$1" goos="$2" goarch="$3" extra="$4" desc="$5"

  local ext=""
  [[ "$goos" == "windows" ]] && ext=".exe"
  local outfile="$OUT_DIR/survaiv-${name}${ext}"

  local ldflags=""
  [[ "$STRIP" == "true" ]] && ldflags="-s -w"
  [[ -n "$VERSION" ]] && ldflags="$ldflags -X main.Version=${VERSION}"
  ldflags="${ldflags# }"  # trim leading space

  printf "  ⚡ %-18s → %s ... " "$name" "$outfile"

  local env_cmd="CGO_ENABLED=0 GOOS=$goos GOARCH=$goarch"
  [[ -n "$extra" ]] && env_cmd="$env_cmd $extra"

  if eval "$env_cmd GO111MODULE=on go build -ldflags \"$ldflags\" -o \"$outfile\" ." 2>/tmp/survaiv-build-err; then
    local size
    size=$(du -h "$outfile" | cut -f1 | tr -d ' ')
    echo "✅ ${size}"
  else
    echo "❌"
    cat /tmp/survaiv-build-err
    return 1
  fi
}

# ── Parse arguments ───────────────────────────────────────────
BUILD_ALL=false
STRIP=true
CLEAN=false
SELECTED=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --all)       BUILD_ALL=true; shift ;;
    --list)      list_targets; exit 0 ;;
    --out)       OUT_DIR="$2"; shift 2 ;;
    --out=*)     OUT_DIR="${1#*=}"; shift ;;
    --strip)     STRIP=true; shift ;;
    --no-strip)  STRIP=false; shift ;;
    --clean)     CLEAN=true; shift ;;
    -h|--help)   usage; exit 0 ;;
    -*)          echo "Unknown option: $1"; usage; exit 1 ;;
    *)           SELECTED+=("$1"); shift ;;
  esac
done

# ── Resolve targets ──────────────────────────────────────────
if [[ "$BUILD_ALL" == "true" ]]; then
  SELECTED=()
  for entry in "${TARGETS[@]}"; do
    IFS='|' read -r name _ <<< "$entry"
    SELECTED+=("$name")
  done
elif [[ ${#SELECTED[@]} -eq 0 ]]; then
  # Native build — detect current platform
  NATIVE_OS=$(go env GOOS)
  NATIVE_ARCH=$(go env GOARCH)
  SELECTED=("${NATIVE_OS}-${NATIVE_ARCH}")
fi

# Validate selected targets
for sel in "${SELECTED[@]}"; do
  found=false
  for entry in "${TARGETS[@]}"; do
    IFS='|' read -r name _ <<< "$entry"
    [[ "$name" == "$sel" ]] && found=true && break
  done
  # Also allow native platform even if not in registry
  if [[ "$found" == "false" ]]; then
    echo "❌ Unknown target: $sel"
    echo "   Use --list to see available targets"
    exit 1
  fi
done

# ── Build ─────────────────────────────────────────────────────
cd "$BUILD_DIR"

VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo dev)

[[ "$CLEAN" == "true" ]] && rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

echo "🔨 Building survaiv giga ($VERSION)"
echo "   Output: $OUT_DIR"
echo ""

FAILED=0
for sel in "${SELECTED[@]}"; do
  for entry in "${TARGETS[@]}"; do
    IFS='|' read -r name goos goarch extra desc <<< "$entry"
    if [[ "$name" == "$sel" ]]; then
      build_target "$name" "$goos" "$goarch" "$extra" "$desc" || ((FAILED++))
      break
    fi
  done
done

echo ""
if [[ $FAILED -gt 0 ]]; then
  echo "⚠️  ${FAILED} target(s) failed"
  exit 1
else
  echo "✅ ${#SELECTED[@]} target(s) built successfully"
fi
