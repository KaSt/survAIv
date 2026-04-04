# Survaiv

**An autonomous agent that pays for its own LLM inference via [x402](https://x402.org) micropayments and bets on [Polymarket](https://polymarket.com) prediction markets to survive on a tiny USDC bankroll.**

> Give an agent $15–25 USDC and see how long it survives — paying for its own thinking while trying to grow its capital through prediction markets.

Three deployment targets, one codebase philosophy:

| Platform | Hardware | Code | Status |
|----------|----------|------|--------|
| **ESP32-C3** | Seeed XIAO · 400 KB SRAM · 4 MB flash | 8,500 lines C++ | ✅ Primary |
| **ESP32-S3** | N16R8 · 8 MB PSRAM · 16 MB flash | Shares C3 source | ✅ Ready |
| **Cloud / TUI** | Any server, Heroku, local machine | 6,200 lines Go | ✅ Ready |

## How It Works

```
┌───────────────────────────────────────────────────────────────┐
│                     survaiv agent loop                        │
│                                                               │
│  ┌──────────┐   x402 (Base USDC)    ┌─────────────────────┐  │
│  │  Agent    │ ◄───────────────────► │ LLM Provider        │  │
│  │  Loop     │   EIP-3009 payment    │ tx402.ai            │  │
│  │          │                        │ x402engine.app      │  │
│  │          │                        │ claw402.org         │  │
│  │          │                        │ or custom endpoint  │  │
│  └────┬─────┘                        └─────────────────────┘  │
│       │                                                       │
│       ▼                                                       │
│  ┌──────────┐   Polymarket CLOB      ┌─────────────────────┐  │
│  │ Trading   │ ◄───────────────────► │ Polymarket           │  │
│  │ Engine    │   EIP-712 signing     │ (Gamma API + CLOB)   │  │
│  └──────────┘                        └─────────────────────┘  │
│       │                                                       │
│       ▼                                                       │
│  ┌──────────┐                        ┌─────────────────────┐  │
│  │ Dashboard │ ◄──── SSE ──────────► │ Browser / TUI        │  │
│  │ + API     │                       │                      │  │
│  └──────────┘                        └─────────────────────┘  │
└───────────────────────────────────────────────────────────────┘
```

Every cycle the agent:

1. **Scans** top Polymarket markets (with descriptions and resolution criteria).
2. **Thinks** — sends budget, positions, market context, and learned wisdom to the LLM.
3. **Decides** — hold, search deeper, or place a trade (paper or live).
4. **Pays** — inference is paid automatically via x402 micropayments. No API keys needed.
5. **Learns** — tracks outcomes, generates trading rules, injects them into future prompts.
6. **Reports** — real-time dashboard shows everything: P&L, positions, decisions, equity curve.

## Features

### Core
- **x402 micropayments** — wallet-is-auth, no accounts. Supports [tx402.ai](https://tx402.ai), [x402engine.app](https://x402engine.app), [claw402.org](https://claw402.org), and custom endpoints.
- **Dynamic model selection** — built-in catalog of 20+ models across providers. Auto-picks per task complexity and remaining budget.
- **Paper & live trading** — paper mode by default with realistic cost simulation; opt into live CLOB trading with on-device EIP-712 signing.
- **Wisdom learning** — tracks every decision outcome, generates trading rules from verified results, and feeds them back into the prompt. Custom rules (LLM-distilled or hand-crafted) get priority in the byte budget. Export/import knowledge between agents and platforms for training on powerful hardware and deploying on microcontrollers.

### Dashboard
- **Real-time web UI** — equity chart, P&L cards, positions table, market scanner, decision log.
- **SSE streaming** — live updates without polling.
- **Dark / light theme** — toggle with persistence.
- **Settings modal** — LLM endpoint config, backup/restore, OTA updates, knowledge export/import, custom rules editor with byte counter.
- **Market descriptions** — LLM receives resolution criteria and event context, not just prices.

### ESP32 Specific
- **On-device wallet** — secp256k1 key generation with hardware RNG, stored in NVS flash.
- **Captive portal onboarding** — wizard guides through WiFi, trading mode, provider, wallet setup.
- **OTA firmware updates** — dual-partition layout, upload via dashboard.
- **mDNS resolution** — connects to `.local` hostnames for local LLM servers.

### Cloud Specific
- **Bubbletea TUI** — full terminal UI with budget cards, positions, market scanner, decision log, wisdom stats. Dark/light themes.
- **Heroku-ready** — auto-detects via `DYNO` env, runs headless with dashboard HTTP only.
- **SQLite persistence** — config, positions, decisions, equity snapshots, wisdom.
- **4 LLM providers** — tx402, x402engine, claw402, custom (all via provider adapter interface).

## Quick Start

### ESP32-C3

```bash
# Prerequisites: ESP-IDF v5.5+
. $IDF_PATH/export.sh

# Build and flash (auto-detects USB port on macOS)
./flash.sh

# Or with monitor:
./flash.sh -m

# Or with wallet provisioning:
./flash.sh --wallet <64-char-hex-private-key>

# Build without OTA (3.9 MB app partition, more resources):
./flash.sh --no-ota
```

On first boot, connect to the **SURVAIV-SETUP** WiFi AP and follow the captive portal wizard.

### ESP32-S3 N16R8

```bash
. $IDF_PATH/export.sh

cd s3
./flash.sh       # build + flash
./flash.sh -m    # with monitor
```

Same firmware, higher limits: 50 markets (vs 6), 512 KB HTTP bodies, full provider catalog.

### Cloud / TUI

```bash
cd cloud
cp .env.example .env
# Edit .env — at minimum set SURVAIV_OAI_URL and SURVAIV_OAI_MODEL

go build -o survaiv .
./survaiv           # TUI + dashboard
./survaiv --headless  # dashboard only (for servers)
```

Dashboard at `http://localhost:8080`. See [cloud/README.md](cloud/README.md) for full env var reference.

### Generate a Wallet

```bash
# Requires: pip install eth-keys
python3 tools/gen_wallet.py

# As JSON:
python3 tools/gen_wallet.py --json
```

Fund the address on two chains:
- **Base**: USDC for paying LLM inference via x402
- **Polygon**: USDC.e + small MATIC for Polymarket trading + gas

## x402 Payment Protocol

The agent uses the [x402 protocol](https://x402.org) to pay for LLM inference — no API keys, no accounts. Your wallet **is** your authentication.

| Provider | Base URL | Pricing | Notes |
|----------|----------|---------|-------|
| [tx402.ai](https://tx402.ai) | `https://tx402.ai/v1` | $0.0001–$0.002/req | Cheapest. OpenAI-compatible. |
| [x402engine.app](https://x402engine.app) | `https://x402-gateway-production.up.railway.app` | $0.002–$0.09/req | More models (GPT-5.x, Claude, Gemini). |
| [claw402.org](https://claw402.org) | `https://claw402.org/v1` | varies | Additional provider option. |

**Payment flow:**
1. Agent sends inference request → provider returns HTTP 402 with payment details
2. Firmware signs an EIP-3009 `transferWithAuthorization` for the exact USDC amount
3. Agent retries with `X-PAYMENT` header → provider verifies on-chain → returns response

## Model Registry

Built-in catalog of 20+ models with ratings and pricing across providers. `SelectModel()` picks the optimal model per cycle:

| Task | Models Used | Why |
|------|-------------|-----|
| Market scan | GPT-OSS-20B, Llama 3.3 70B | Cheapest, fast for formatting |
| Analysis | DeepSeek V3.2, Qwen3 235B | Strong reasoning at low cost |
| Trade decision | DeepSeek V3.2, Kimi K2.5 | Best value for complex analysis |
| Critical | DeepSeek R1, GPT-5.1 | Top-tier CoT, used sparingly |

In paper mode with a custom endpoint, the registry fuzzy-matches model names to estimate realistic costs.

## Live Trading

When opted in, the agent places real orders on Polymarket's CLOB:

- **On-device crypto**: Keccak-256, secp256k1 ECDSA, EIP-712 typed data signing
- **Two-level CLOB auth**: L1 (EIP-712 → API creds), L2 (HMAC-SHA256 per request)
- **Safety guards**:
  - Confidence ≥ 75% + edge ≥ 200 bps for live trades
  - Daily loss limit with hard stop
  - 30-minute cooldown after losses
  - Max position size as BPS of equity
  - Equity ≤ reserve → no new trades

## Wisdom System

The agent builds a knowledge base from its own track record and can transfer that knowledge between platforms.

### Knowledge Acquisition

```
cycle N: agent decides → "buy YES on market X at 0.35"
         ↓
cycle N+K: market X resolves → YES won → agent was correct
         ↓
wisdom updated: "crypto regulatory markets: 68% win rate, bullish bias profitable"
         ↓
cycle N+K+1: wisdom injected into system prompt → better decisions
```

1. **Track** — every decision (buy/hold) is recorded with market state, confidence, edge estimate, and model used.
2. **Resolve** — periodically checks Polymarket for final outcomes on tracked markets.
3. **Evaluate** — computes accuracy by category (crypto, politics, sports…), buy vs hold performance, edge calibration.
4. **Generate rules** — distills verified outcomes into concise trading rules injected into the system prompt.
5. **Persist** — wisdom survives reboots (NVS on ESP32, SQLite on cloud).

### Custom Rules

Beyond auto-generated stats, agents support **custom rules** — LLM-distilled or hand-crafted insights that get priority in the prompt byte budget:

```
AVOID: sports match outcomes — no edge vs oddsmakers
EDGE: geopolitical binary events — markets overreact to headlines
RULE: >0.85 yes_price = skip, edge too thin at extremes
RULE: crypto regulatory = bearish bias profitable 68%
CAL: weight recent 30d outcomes 2x vs older
S:42/60=70% h:75% b:65% +geopolitical:80% -sports:30%
```

Custom rules are:
- **Persistent** — survive stat regeneration and reboots
- **Priority** — injected before auto-generated stats in the prompt
- **Portable** — included in knowledge exports for cross-platform transfer
- **Editable** — via the dashboard settings UI (textarea with live byte counter)

### Knowledge Transfer

Export the full knowledge state as a JSON file and import it on any platform:

```
Pi + Opus 4 (cloud)          →  export  →  survaiv-knowledge-v2.json
                                               │
ESP32-C3 (paper)             ←  import  ←──────┘  (auto-truncated to 800B budget)
ESP32-S3 (live)              ←  import  ←──────┘  (4 KB budget, full fidelity)
Another cloud agent          ←  import  ←──────┘  (8 KB budget)
```

The export includes:
- **Custom rules** — the high-value distilled insights
- **Wisdom text** — the current prompt injection
- **Stats** — accuracy by category, buy/hold breakdown
- **Decision ring** — last 200 tracked decisions with outcomes
- **Model history** — which models were used and when

On import, custom rules are automatically truncated to the target platform's wisdom budget at line boundaries — no manual trimming needed.

### The Learning Pipeline

The intended workflow for maximum knowledge quality:

1. **Train on powerful hardware** — run the cloud agent on a Pi or server with a strong model (Opus 4, GPT-5, DeepSeek R1) for weeks or months. Paper trading is free with a local model.
2. **Accumulate outcomes** — the agent tracks hundreds of decisions and their resolutions, building statistical confidence.
3. **Distill into rules** — periodically review outcomes and craft (or let the LLM generate) custom rules that capture the highest-signal patterns.
4. **Export knowledge** — download the `survaiv-knowledge-v2.json` from the dashboard.
5. **Import to target** — upload to any agent (ESP32-C3, S3, another cloud instance). The rules are compressed to fit.
6. **Freeze learning** — on resource-constrained devices, freeze learning to prevent the small model from overwriting good rules with noisy ones.
7. **Deploy** — the C3 now carries months of curated wisdom in 800 bytes, making better decisions with a cheap model.

### Knowledge Sharing

Since the export format is a standard JSON file, knowledge can be shared between agents:

- **Fleet deployment** — train one agent, export, import into many devices
- **Community sharing** — publish anonymized knowledge files (decisions contain market IDs and outcomes, not personal data)
- **A/B testing** — run two agents with different custom rules on the same markets, compare P&L
- **Versioning** — keep snapshots of knowledge at different stages; roll back if performance degrades

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/wisdom` | GET | Current wisdom stats, custom rules, budget |
| `/api/wisdom/rules` | POST | Set custom rules (`{"rules":"..."}`) |
| `/api/wisdom/freeze` | POST | Toggle learning (`{"frozen":true}`) |
| `/api/knowledge` | GET | Export full knowledge as JSON |
| `/api/knowledge` | POST | Import knowledge from JSON |

### Platform Wisdom Budgets

| Platform | Budget | Typical Content |
|----------|--------|-----------------|
| C3 (OTA) | 800 bytes | 10–12 terse rules + compact stats |
| C3 (no-OTA) | 2,000 bytes | 25+ rules with context |
| S3 | 4,000 bytes | Full rule set + verbose stats |
| Cloud | 8,000 bytes | Comprehensive rules + category breakdowns |

## Architecture

### ESP32 (C3 / S3) — `main/`

```
main/
├── main.cpp              — Boot → NVS → WiFi → SNTP → wallet → x402 → agent loop
├── agent.cpp/.h          — LLM prompts, decision parsing, trade execution, retry logic
├── model_registry.cpp/.h — 20+ model catalog, dynamic selection, price lookup
├── wisdom.cpp/.h         — Decision tracking, outcome verification, rule generation
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
├── http.cpp/.h           — HTTP client with timeout/retry
├── json_util.cpp/.h      — cJSON helpers
├── wifi.cpp/.h           — WiFi STA + AP management
└── types.h               — Shared structs

s3/                       — ESP32-S3 build variant (shares main/ source)
├── CMakeLists.txt        — S3 project file
├── partitions.csv        — 16 MB flash layout (2×7 MB OTA slots)
├── sdkconfig.defaults    — S3 + PSRAM config
└── flash.sh              — S3 build/flash script
```

### Cloud — `cloud/`

```
cloud/
├── main.go               — Entry point, signal handling, agent loop
├── internal/agent/       — LLM prompts, parsing, cycle orchestration
├── internal/dashboard/   — Thread-safe state, HTTP handlers, embedded HTML
├── internal/wisdom/      — Outcome tracking, rule generation, import/export
├── internal/tui/         — Bubbletea terminal UI (8 files)
├── internal/config/      — Env vars + SQLite config
├── internal/db/          — SQLite open + migrations
├── internal/httpclient/  — HTTP client with LLM retry (120s timeout, 3 retries)
├── internal/ledger/      — Budget & position accounting
├── internal/polymarket/  — Market + geoblock API
├── internal/models/      — LLM model registry + selection
├── internal/provider/    — Provider adapters (tx402, x402engine, claw402, custom)
├── internal/x402/        — Micropayment signatures
├── internal/wallet/      — Ethereum wallet management
├── internal/crypto/      — EIP-712, Keccak-256, secp256k1
└── internal/types/       — Shared type definitions
```

## Platform Differences

| Feature | C3 (OTA) | C3 (no-OTA) | S3 | Cloud |
|---------|----------|-------------|----|----|
| Markets per scan | 6 | 12 | 50 | unlimited |
| HTTP body size | 64 KB | 128 KB | 512 KB | unlimited |
| Wisdom budget | 800 B | 2,000 B | 4,000 B | 8,000 B |
| Model registry | 40 dynamic | 80 dynamic | 200 dynamic | unlimited |
| OTA updates | ✅ | — | ✅ | N/A |
| x402 providers | tx402 only* | tx402 only* | all 4 | all 4 |
| Persistence | NVS flash | NVS flash | NVS flash | SQLite |
| UI | Web dashboard | Web dashboard | Web dashboard | TUI + web |
| Deployment | USB flash | USB flash | USB flash | `go build` / Heroku |

\* x402engine catalog too large for C3's 400 KB SRAM.

## Budget Assessment

Using tx402.ai with DeepSeek V3.2 ($0.0005/request):

| Bankroll | LLM Rounds | Days at 96 cycles/day |
|----------|------------|----------------------|
| $15 USDC | ~30,000 | ~312 days |
| $25 USDC | ~50,000 | ~520 days |

The real constraint is trading P&L, not inference cost.

## License

MIT
