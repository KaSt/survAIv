#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# ── Parse arguments ───────────────────────────────────────────
WALLET_KEY=""
MONITOR=false
NO_OTA=false
POSITIONAL=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --wallet) WALLET_KEY="$2"; shift 2 ;;
    --wallet=*) WALLET_KEY="${1#*=}"; shift ;;
    -m|--monitor) MONITOR=true; shift ;;
    --no-ota) NO_OTA=true; shift ;;
    --ota) NO_OTA=false; shift ;;
    -h|--help)
      echo "Usage: ./flash.sh [OPTIONS] [PORT] [BAUD]"
      echo ""
      echo "Board: spark · M5StickC PLUS2 (135×240 ST7789V2, 8 MB flash)"
      echo ""
      echo "Options:"
      echo "  --no-ota         Disable OTA: single ~7.9 MB app partition"
      echo "  --ota            Enable OTA: dual 3.9 MB partitions (default)"
      echo "  --wallet <hex>   Provision wallet private key (64 hex chars)"
      echo "  -m, --monitor    Open serial monitor after flash"
      echo "  -h, --help       Show this help"
      exit 0
      ;;
    *) POSITIONAL+=("$1"); shift ;;
  esac
done

PORT="${POSITIONAL[0]:-/dev/ttyUSB0}"
BAUD="${POSITIONAL[1]:-460800}"
TARGET="esp32"

# Auto-detect port on macOS.
if [[ ! -e "$PORT" && "$(uname)" == "Darwin" ]]; then
  for p in /dev/cu.usbserial-* /dev/cu.usbmodem* /dev/cu.SLAB_USB* /dev/cu.wchusbserial* /dev/tty.usbserial-* /dev/tty.usbmodem*; do
    if [[ -e "$p" ]]; then PORT="$p"; break; fi
  done
fi

echo "╔═══════════════════════════════════╗"
echo "║  ⟁ SURVAIV spark — M5StickC PLUS2  ║"
echo "╚═══════════════════════════════════╝"
echo "  Target : $TARGET"
echo "  Board  : spark · M5StickC PLUS2 (135×240 ST7789V2)"
if $NO_OTA; then
  echo "  OTA    : disabled (single app partition)"
else
  echo "  OTA    : enabled (dual 3.9 MB partitions)"
fi
echo "  Port   : $PORT"
echo "  Baud   : $BAUD"
if [[ -n "$WALLET_KEY" ]]; then
  echo "  Wallet : will provision after flash"
fi
echo ""

if ! command -v idf.py &>/dev/null; then
  echo "❌ idf.py not found. Source ESP-IDF first:"
  echo "   . \$IDF_PATH/export.sh"
  exit 1
fi

idf.py set-target "$TARGET"

BUILD_ARGS=()
if $NO_OTA; then
  BUILD_ARGS+=(-D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.no_ota")
  rm -f sdkconfig
fi

echo "── Building ──────────────────────"
idf.py "${BUILD_ARGS[@]}" build

echo ""
echo "── Flashing ──────────────────────"
idf.py -p "$PORT" -b "$BAUD" flash

# ── Wallet provisioning via NVS ────────────────────────────────
if [[ -n "$WALLET_KEY" ]]; then
  echo ""
  echo "── Wallet provisioning ───────────"

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
