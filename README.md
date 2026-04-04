# Survaiv

**An autonomous ESP32-C3 agent that pays for its own LLM inference via [x402](https://x402.org) micropayments and bets on [Polymarket](https://polymarket.com) prediction markets to survive on a tiny USDC bankroll.**

> A social experiment: give an agent $15–25 USDC and see how long it survives — paying for its own "thinking" while trying to grow its capital through prediction markets.

## How It Works

```
┌──────────────────────────────────────────────────────────────┐
│  Seeed XIAO ESP32-C3  (RISC-V 160 MHz · 400 KB SRAM · 4 MB)│
│                                                              │
│  ┌─────────┐   x402 (Base USDC)    ┌────────────────────┐   │
│  │  Agent   │ ◄──────────────────► │ LLM Provider        │   │
│  │  Loop    │                       │ tx402.ai            │   │
│  │          │   EIP-712 (Polygon)   │ x402engine.app      │   │
│  │          │ ◄──────────────────► │ or custom endpoint   │   │
│  └────┬─────┘                       └────────────────────┘   │
│       │                                                      │
│       ▼                                                      │
│  ┌─────────┐   Polymarket CLOB      ┌────────────────────┐   │
│  │ Trading  │ ◄──────────────────► │ Polymarket           │   │
│  │ Engine   │                       │ (Gamma API + CLOB)   │   │
│  └─────────┘                       └────────────────────┘   │
│       │                                                      │
│       ▼                                                      │
│  ┌─────────┐   WiFi AP / captive    ┌────────────────────┐   │
│  │ Web      │ ◄──────────────────► │ Browser (dashboard)  │   │
│  │ Server   │                       │                      │   │
│  └─────────┘                       └────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

Every cycle the agent:

1. Fetches top Polymarket markets by volume.
2. Sends its budget, positions, and market data to the LLM.
3. The LLM decides: **hold**, **search more**, or **place a trade**.
4. LLM inference is paid automatically via x402 — no API keys needed for x402 providers.
5. Risk guards enforce loss limits, position caps, and cooldowns.

## Features

- **x402 micropayments** — wallet-is-auth, no accounts needed. Supports both [tx402.ai](https://tx402.ai) and [x402engine.app](https://x402engine.app).
- **Dynamic model selection** — built-in catalog of 21 models cross-referenced across providers. Auto-picks the best model per task complexity and remaining budget.
- **Paper & live trading** — Paper mode by default with realistic cost simulation; opt into live CLOB trading with on-device EIP-712 signing.
- **On-device wallet** — secp256k1 key generation with hardware RNG, stored in NVS flash. Same key works on Base (for x402) and Polygon (for Polymarket).
- **Web dashboard** — real-time SPA over WiFi: P&L, positions, decisions, equity history, inference spend.
- **Onboarding wizard** — captive portal guides through WiFi, trading mode, LLM provider, wallet setup.
- **OTA updates** — dual-partition layout for over-the-air firmware updates via the dashboard.
- **Backup & restore** — export/import all NVS config (WiFi, wallet, provider) as JSON.

## Supported Hardware

| Board | Status |
|-------|--------|
| [Seeed XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) | ✅ Primary target |
| Other ESP32-C3 boards (4 MB flash) | Should work |

## Quick Start

### Prerequisites

- [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/)
- Python 3.11+ (for `keccak_256` in wallet generation)
- A funded wallet on **Base** (USDC for x402 inference) and **Polygon** (USDC.e for trading)

### Build & Flash

```bash
# Source ESP-IDF environment
. $IDF_PATH/export.sh

# Build and flash (auto-detects USB port on macOS)
./flash.sh

# Or with wallet provisioning:
./flash.sh --wallet <64-char-hex-private-key>
```

### Generate a Wallet

```bash
# Requires: pip install eth-keys
python3 tools/gen_wallet.py

# Or as JSON:
python3 tools/gen_wallet.py --json
```

The same address needs funding on two chains:
- **Base**: USDC for paying LLM inference via x402
- **Polygon**: USDC.e + small MATIC for Polymarket trading + gas

### First Boot

1. The board starts in AP mode: **SURVAIV-SETUP**
2. Connect and follow the captive portal wizard:
   - **Step 1**: WiFi credentials
   - **Step 2**: Trading mode (Real ⟁ or Paper 📝)
   - **Step 3**: LLM provider (tx402.ai / x402engine.app / custom API)
   - **Step 4**: Wallet (auto-generated on device, or skip in paper mode)
   - **Step 5**: Review & launch
3. After setup, the dashboard is available at the device's IP on your local network.

## x402 Payment Protocol

The agent uses the [x402 protocol](https://x402.org) to pay for LLM inference — no API keys, no accounts. Your wallet **is** your authentication.

**Supported providers:**

| Provider | Base URL | Pricing | Notes |
|----------|----------|---------|-------|
| [tx402.ai](https://tx402.ai) | `https://tx402.ai/v1` | $0.0001–$0.002/req | Cheapest. OpenAI-compatible. |
| [x402engine.app](https://x402engine.app) | `https://x402-gateway-production.up.railway.app` | $0.002–$0.09/req | More models (GPT-5.x, Claude, Gemini). |

**How payment works:**
1. Agent sends request → provider returns HTTP 402
2. Firmware signs an EIP-3009 `transferWithAuthorization` for the exact USDC amount
3. Agent retries with `X-PAYMENT` header → provider verifies on-chain → returns response

## Model Registry

The firmware includes a built-in catalog of 21 models with ratings and real pricing from both providers. The `SelectModel()` function picks the optimal model per cycle:

| Task | Models Used | Why |
|------|-------------|-----|
| Market scan | GPT-OSS-20B, Llama 3.3 70B | Cheapest, fast enough for formatting |
| Analysis | DeepSeek V3.2, Qwen3 235B | Strong reasoning at low cost |
| Trade decision | DeepSeek V3.2, Kimi K2.5 | Best value for complex analysis |
| Critical edge cases | DeepSeek R1, GPT-5.1 | Top-tier CoT, used sparingly |

In paper mode with a custom endpoint, the registry fuzzy-matches the model name against known providers to estimate realistic costs.

## Live Trading

When opted in, the agent places real orders on Polymarket's CLOB:

- **On-device crypto**: Keccak-256, secp256k1 ECDSA, EIP-712 typed data signing
- **Two-level CLOB auth**: L1 (EIP-712 → API creds), L2 (HMAC-SHA256 per request)
- **Safety guards**:
  - Confidence ≥ 75% + edge ≥ 200 bps required for live trades
  - Daily loss limit with hard stop
  - 30-minute cooldown after losses
  - Max position size as BPS of equity
  - Equity ≤ reserve → no new trades

### Pre-flight Checklist

Before live trading, the wallet needs:
1. **MATIC** (small amount) on Polygon for approval gas
2. **USDC.e** on Polygon for trading bankroll
3. Two approval transactions:
   ```
   USDC.e.approve(CTF_EXCHANGE, MAX_UINT256)
   ConditionalTokens.setApprovalForAll(CTF_EXCHANGE, true)
   ```

The firmware checks approval status on boot and logs instructions if missing.

## Architecture

```
main/
├── main.cpp              — Boot: NVS → WiFi → SNTP → wallet → x402 → agent loop
├── agent.cpp/.h          — LLM prompts, decision parsing, trade execution
├── model_registry.cpp/.h — 21-model catalog, dynamic selection, price lookup
├── x402.cpp/.h           — x402 payment protocol (EIP-3009 signing)
├── crypto.cpp/.h         — Keccak-256, secp256k1, EIP-712, HMAC-SHA256
├── wallet.cpp/.h         — NVS key storage, on-device key generation
├── clob.cpp/.h           — Polymarket CLOB L1/L2 auth, order placement
├── polymarket.cpp/.h     — Gamma API market fetching, geoblock check
├── ledger.cpp/.h         — Budget tracking, P&L, position management
├── config.cpp/.h         — Runtime config (NVS with Kconfig fallback)
├── dashboard_state.cpp/.h — Thread-safe state for dashboard SSE
├── webserver.cpp/.h      — HTTP server: dashboard, API, SSE, OTA, backup
├── onboard.cpp/.h        — Captive portal, DNS server, setup wizard
├── web_assets.h          — Embedded HTML/CSS/JS (dashboard + onboarding)
├── http.cpp/.h           — HTTP client wrapper
├── json_util.cpp/.h      — cJSON helpers
├── wifi.cpp/.h           — WiFi STA + AP management
└── types.h               — Shared structs

tools/
└── gen_wallet.py         — Offline wallet generator

flash.sh                  — Build + flash + optional wallet provisioning
partitions.csv            — Dual-OTA partition layout (4 MB flash)
```

## Flash Partition Layout

| Partition | Offset | Size | Purpose |
|-----------|--------|------|---------|
| nvs | 0x9000 | 24 KB | Wallet keys + config (survives OTA) |
| otadata | 0xF000 | 8 KB | OTA boot selection |
| phy_init | 0x11000 | 4 KB | WiFi calibration |
| ota_0 | 0x20000 | 1.75 MB | Firmware slot A |
| ota_1 | 0x1E0000 | 1.75 MB | Firmware slot B |

## Budget Assessment

Using tx402.ai with DeepSeek V3.2 ($0.0005/request):

| Bankroll | LLM Rounds | Days at 96 cycles/day |
|----------|------------|----------------------|
| $15 USDC | ~30,000 | ~312 days |
| $25 USDC | ~50,000 | ~520 days |

The real constraint is trading P&L, not inference cost.

## License

MIT
