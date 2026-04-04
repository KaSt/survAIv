#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# ── Parse arguments ───────────────────────────────────────────
WALLET_KEY=""
MONITOR=false
POSITIONAL=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --wallet) WALLET_KEY="$2"; shift 2 ;;
    --wallet=*) WALLET_KEY="${1#*=}"; shift ;;
    -m|--monitor) MONITOR=true; shift ;;
    *) POSITIONAL+=("$1"); shift ;;
  esac
done

PORT="${POSITIONAL[0]:-/dev/ttyUSB0}"
BAUD="${POSITIONAL[1]:-460800}"
TARGET="esp32s3"

# Auto-detect port on macOS.
if [[ ! -e "$PORT" && "$(uname)" == "Darwin" ]]; then
  for p in /dev/cu.usbserial-* /dev/cu.usbmodem* /dev/cu.SLAB_USB* /dev/cu.wchusbserial* /dev/tty.usbserial-* /dev/tty.usbmodem*; do
    if [[ -e "$p" ]]; then PORT="$p"; break; fi
  done
fi

echo "╔═══════════════════════════════════╗"
echo "║  ⟁ SURVAIV — build & flash       ║"
echo "╚═══════════════════════════════════╝"
echo "  Target : $TARGET"
echo "  Port   : $PORT"
echo "  Baud   : $BAUD"
if [[ -n "$WALLET_KEY" ]]; then
  echo "  Wallet : will provision after flash"
fi
echo ""

# Check IDF environment.
if ! command -v idf.py &>/dev/null; then
  echo "❌ idf.py not found. Source ESP-IDF first:"
  echo "   . \$IDF_PATH/export.sh"
  exit 1
fi

idf.py set-target "$TARGET"

echo "── Building ──────────────────────"
idf.py build

echo ""
echo "── Flashing ──────────────────────"
idf.py -p "$PORT" -b "$BAUD" flash

# ── Wallet provisioning via NVS ────────────────────────────────
if [[ -n "$WALLET_KEY" ]]; then
  echo ""
  echo "── Wallet provisioning ───────────"

  # Validate hex key.
  if [[ ! "$WALLET_KEY" =~ ^[0-9a-fA-F]{64}$ ]]; then
    echo "❌ Invalid private key — must be exactly 64 hex characters"
    exit 1
  fi

  TMPDIR=$(mktemp -d)
  NVS_CSV="$TMPDIR/wallet_nvs.csv"
  NVS_BIN="$TMPDIR/wallet_nvs.bin"

  cat > "$NVS_CSV" <<EOF
key,type,encoding,value
survaiv,namespace,,
wallet_key,data,hex2bin,$WALLET_KEY
EOF

  if python3 -m esp_idf_nvs_partition_gen generate "$NVS_CSV" "$NVS_BIN" 0x6000 2>/dev/null; then
    # Write to NVS partition at offset 0x9000.
    python3 -m esptool --port "$PORT" --baud "$BAUD" write_flash 0x9000 "$NVS_BIN"
    echo "✅ Wallet key provisioned to NVS"
  else
    echo "⚠  NVS partition gen not available — wallet will be set via onboarding wizard"
    echo "   Install with: pip install esp-idf-nvs-partition-gen"
  fi

  rm -rf "$TMPDIR"
fi

echo ""
if $MONITOR; then
  echo "── Monitor (Ctrl+] to quit) ─────"
  idf.py -p "$PORT" monitor
else
  echo "✅ Flash complete. Run with -m to monitor:"
  echo "   ./flash.sh -m"
fi
