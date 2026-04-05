#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── Board registry ────────────────────────────────────────────
# Each board: dir | target | label | display info
declare -A BOARD_DIR BOARD_TARGET BOARD_LABEL BOARD_DISPLAY
BOARD_DIR[c3]="."           ; BOARD_TARGET[c3]="esp32c3"  ; BOARD_LABEL[c3]="ESP32-C3 (Seeed XIAO)"         ; BOARD_DISPLAY[c3]=""
BOARD_DIR[s3]="s3"          ; BOARD_TARGET[s3]="esp32s3"  ; BOARD_LABEL[s3]="ESP32-S3 N16R8"                ; BOARD_DISPLAY[s3]=""
BOARD_DIR[tqt]="tqt"        ; BOARD_TARGET[tqt]="esp32s3" ; BOARD_LABEL[tqt]="LilyGO T-QT Pro"              ; BOARD_DISPLAY[tqt]="128×128 GC9107"
BOARD_DIR[atoms3]="atoms3"  ; BOARD_TARGET[atoms3]="esp32s3"; BOARD_LABEL[atoms3]="M5Stack AtomS3"           ; BOARD_DISPLAY[atoms3]="128×128 GC9107"
BOARD_DIR[stickc2]="stickc2"; BOARD_TARGET[stickc2]="esp32"; BOARD_LABEL[stickc2]="M5StickC PLUS2"           ; BOARD_DISPLAY[stickc2]="135×240 ST7789V2"
BOARD_DIR[c3oled]="c3oled"  ; BOARD_TARGET[c3oled]="esp32c3"; BOARD_LABEL[c3oled]="ESP32-C3 SuperMini OLED"  ; BOARD_DISPLAY[c3oled]="72×40 SSD1306"
ALL_BOARDS=(c3 s3 tqt atoms3 stickc2 c3oled)

# ── Parse arguments ───────────────────────────────────────────
BOARD=""
WALLET_KEY=""
MONITOR=false
NO_OTA=false
POSITIONAL=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --board)    BOARD="$2"; shift 2 ;;
    --board=*)  BOARD="${1#*=}"; shift ;;
    --c3|--C3)  BOARD="c3"; shift ;;
    --s3|--S3)  BOARD="s3"; shift ;;
    --wallet)   WALLET_KEY="$2"; shift 2 ;;
    --wallet=*) WALLET_KEY="${1#*=}"; shift ;;
    -m|--monitor) MONITOR=true; shift ;;
    --no-ota)   NO_OTA=true; shift ;;
    --ota)      NO_OTA=false; shift ;;
    --list)
      echo "Available boards:"
      for b in "${ALL_BOARDS[@]}"; do
        disp=""
        [[ -n "${BOARD_DISPLAY[$b]}" ]] && disp=" (${BOARD_DISPLAY[$b]})"
        printf "  %-10s  %-34s  %s%s\n" "$b" "${BOARD_LABEL[$b]}" "${BOARD_TARGET[$b]}" "$disp"
      done
      exit 0
      ;;
    -h|--help)
      echo "Usage: ./flash.sh [OPTIONS] [PORT] [BAUD]"
      echo ""
      echo "Options:"
      echo "  --board <name>   Board to build (default: c3). Use --list for all boards"
      echo "  --c3             Shortcut for --board c3"
      echo "  --s3             Shortcut for --board s3"
      echo "  --no-ota         Disable OTA: single large app partition (USB-only updates)"
      echo "  --ota            Enable OTA: dual partitions (default)"
      echo "  --wallet <hex>   Provision wallet private key (64 hex chars)"
      echo "  -m, --monitor    Open serial monitor after flash"
      echo "  --list           List all supported boards"
      echo "  -h, --help       Show this help"
      echo ""
      echo "Examples:"
      echo "  ./flash.sh                              # C3 with OTA"
      echo "  ./flash.sh --board c3 --no-ota          # C3 without OTA"
      echo "  ./flash.sh --board tqt -m               # T-QT Pro + monitor"
      echo "  ./flash.sh --board stickc2 --no-ota     # StickC PLUS2 without OTA"
      echo "  ./flash.sh --board c3oled                # C3 SuperMini OLED"
      echo "  ./flash.sh --board s3 --wallet <key> -m  # S3 + wallet + monitor"
      exit 0
      ;;
    *) POSITIONAL+=("$1"); shift ;;
  esac
done

# Default board
[[ -z "$BOARD" ]] && BOARD="c3"

# Validate board
if [[ -z "${BOARD_DIR[$BOARD]+x}" ]]; then
  echo "❌ Unknown board: $BOARD"
  echo "   Use --list to see available boards"
  exit 1
fi

TARGET="${BOARD_TARGET[$BOARD]}"
BUILD_DIR="${BOARD_DIR[$BOARD]}"
LABEL="${BOARD_LABEL[$BOARD]}"
DISPLAY="${BOARD_DISPLAY[$BOARD]}"

PORT="${POSITIONAL[0]:-/dev/ttyUSB0}"
BAUD="${POSITIONAL[1]:-460800}"

# Auto-detect port on macOS.
if [[ ! -e "$PORT" && "$(uname)" == "Darwin" ]]; then
  for p in /dev/cu.usbserial-* /dev/cu.usbmodem* /dev/cu.SLAB_USB* /dev/cu.wchusbserial* /dev/tty.usbserial-* /dev/tty.usbmodem*; do
    if [[ -e "$p" ]]; then PORT="$p"; break; fi
  done
fi

echo "╔═══════════════════════════════════╗"
echo "║  ⟁ SURVAIV — build & flash       ║"
echo "╚═══════════════════════════════════╝"
echo "  Board  : $LABEL"
[[ -n "$DISPLAY" ]] && echo "  Display: $DISPLAY"
echo "  Target : $TARGET"
if $NO_OTA; then
  echo "  OTA    : disabled (single app partition)"
else
  echo "  OTA    : enabled (dual partitions)"
fi
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

# Move to board build directory.
cd "$SCRIPT_DIR/$BUILD_DIR"

idf.py set-target "$TARGET"

# ── Build with optional no-OTA overlay ──────────────────────────
BUILD_ARGS=()
if $NO_OTA; then
  if [[ ! -f "sdkconfig.defaults.no_ota" ]]; then
    echo "❌ No-OTA overlay not found: $BUILD_DIR/sdkconfig.defaults.no_ota"
    exit 1
  fi
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
  echo "   ./flash.sh --board $BOARD -m"
fi
