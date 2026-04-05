# Survaiv

> **⚠️** This is a research experiment, not financial advice. Expect to lose your bankroll. [Full disclaimer ↓](#disclaimer)

**An autonomous agent that pays for its own LLM inference via [x402](https://x402.org) micropayments and bets on [Polymarket](https://polymarket.com) prediction markets to survive on a tiny USDC bankroll.**

> Give an agent $15–25 USDC and see how long it survives — paying for its own thinking while trying to grow its capital through prediction markets. A social experiment in AI autonomy, not a trading strategy.

Seven deployment targets, one codebase philosophy:

| Target | Tier | SoC | CPU | PSRAM | Flash | Display | Source | Status |
|--------|------|-----|-----|-------|-------|---------|--------|--------|
| [**ESP32-C3 SuperMini**](https://www.espboards.dev/esp32/esp32-c3-super-mini/) | `pico` | ESP32-C3 | RISC-V · 1 core · 160 MHz | — | 4 MB | — | ~9,800 lines C++ | ✅ Primary |
| [**ESP32-S3 N16R8**](https://www.espressif.com/en/products/socs/esp32-s3) | `core` | ESP32-S3 (WROOM-1) | Xtensa LX7 · 2 cores · 240 MHz | 8 MB | 16 MB | — | Shares C3 source | ✅ Ready |
| [**LilyGO T-QT Pro**](https://lilygo.cc/products/t-qt-pro) | `nano` | ESP32-S3 (FN4R2) | Xtensa LX7 · 2 cores · 240 MHz | 2 MB | 4 MB | 0.85" LCD | Shares C3 source | ✅ Ready |
| [**M5Stack AtomS3**](https://docs.m5stack.com/en/core/AtomS3) | `atom` | ESP32-S3 | Xtensa LX7 · 2 cores · 240 MHz | 8 MB | 8 MB | 0.85" LCD | Shares C3 source | ✅ Ready |
| [**M5StickC PLUS2**](https://docs.m5stack.com/en/core/M5StickC%20PLUS2) | `spark` | ESP32-PICO-V3-02 | Xtensa LX6 · 2 cores · 240 MHz | 2 MB | 8 MB | 1.14" LCD | Shares C3 source | ✅ Ready |
| [**C3 SuperMini OLED**](https://www.espboards.dev/esp32/esp32-c3-oled-042/) | `dot` | ESP32-C3 | RISC-V · 1 core · 160 MHz | — | 4 MB | 0.42" OLED | Shares C3 source | ✅ Ready |
| **Cloud / TUI** | `giga` | Any | — | — | — | — | ~7,500 lines Go | ✅ Ready |

## How It Works

```
┌───────────────────────────────────────────────────────────────┐
│                     survaiv agent loop                        │
│                                                               │
│  ┌──────────┐   x402 (Base USDC)     ┌─────────────────────┐  │
│  │  Agent   │ ◄ ──────────────────►  │ LLM Provider        │  │
│  │  Loop    │    EIP-3009 payment    │ tx402.ai            │  │
│  │          │                        │ x402engine.app      │  │
│  │          │                        │ claw402.org         │  │
│  │          │                        │ or custom endpoint  │  │
│  └────┬─────┘                        └─────────────────────┘  │
│       │                                                       │
│       ▼                                                       │
│  ┌──────────┐   Polymarket CLOB      ┌─────────────────────┐  │
│  │ Trading  │  ◄───────────────────► │ Polymarket          │  │
│  │ Engine   │    EIP-712 signing     │ (Gamma API + CLOB)  │  │
│  └──────────┘                        └─────────────────────┘  │
│       │                                                       │
│       ▼                                                       │
│  ┌─────────-─┐                        ┌─────────────────────┐ │
│  │ Dashboard │ ◄──── SSE ──────────►  │ Browser / TUI       │ │
│  │ + API     │                        │                     │ │
│  └─-─────────┘                        └─────────────────────┘ │
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
- **Dynamic runtime config** — cloud version auto-detects host resources (CPU cores, memory) and model context window, then adapts prompt budget, completion limits, market coverage, and parallelism. See [Efficiency Score](#efficiency-score).
- **Reasoning model awareness** — detects reasoning models (e.g. DeepSeek R1, gpt-oss:20b) and sends `reasoning_effort: "low"` with separate token budgets so chain-of-thought doesn't starve the response.
- **Parallel execution** — on multi-core cloud hosts (≥4 cores), geoblock + market fetch run concurrently; tool call LLM requests overlap with wisdom outcome checks.
- **Paper & live trading** — paper mode by default with realistic cost simulation; opt into live CLOB trading with on-device EIP-712 signing.
- **Wisdom learning** — tracks every decision outcome, generates trading rules from verified results, and feeds them back into the prompt. Custom rules (LLM-distilled or hand-crafted) get priority in the byte budget. Export/import knowledge between agents and platforms for training on powerful hardware and deploying on microcontrollers.
- **API authentication** — PIN-based claim system with session tokens. All API endpoints (including GET and SSE) are auth-guarded once claimed. First user to enter the PIN displayed on serial console owns the agent.

### Dashboard
- **Real-time web UI** — equity chart, P&L chart (profit/loss over cycles), positions table, market scanner, decision log, system stats.
- **Next-cycle countdown** — discrete `m:ss` timer next to the status badge counts down to the next agent cycle.
- **System stats** — per-core CPU usage with color-coded bar graphs, RAM usage bar, minimum free heap. CPU measured via FreeRTOS idle hooks (ESP32) or `getrusage` (cloud).
- **SSE streaming** — live updates without polling.
- **Dark / light theme** — toggle with persistence.
- **Settings modal** — LLM endpoint config, tool usage slider, backup/restore, OTA updates, knowledge export/import, custom rules editor with byte counter, agent efficiency score with platform comparison.
- **Tool usage slider** — 3-position control (Frugal / Balanced / Generous) that tunes how aggressively the agent uses search tool calls. Frugal minimizes LLM rounds; Generous searches news for every actionable market. Persisted across reboots.
- **Custom favicon** — pixel art brain-bot icon embedded as base64 PNG (273 bytes on ESP32, 301 bytes on cloud).
- **Market descriptions** — LLM receives resolution criteria and event context, not just prices.
- **News ticker** — scrolling headline bar showing latest news collected during research. Auto-speeds based on headline count, pauses on hover, deduplicates, updates in real-time via SSE.
- **Platform badge** — shows firmware version with OTA/NO-OTA indicator on ESP32 builds.

### ESP32 Specific
- **Startup banner** — logs firmware version, build date, IDF version, chip model, core count, OTA status, and free heap to serial on boot.
- **On-device wallet** — secp256k1 key generation with hardware RNG, stored in NVS flash.
- **Captive portal onboarding** — wizard guides through WiFi, trading mode, provider, wallet setup.
- **OTA firmware updates** — dual-partition layout, upload via dashboard.
- **mDNS resolution** — connects to `.local` hostnames for local LLM servers.
- **On-board LCD display** — boards with screens (T-QT Pro, AtomS3, StickC PLUS2) show live stats: equity, P&L, positions, cycle count, and mode badge. 30-second auto-dim with button wake. Powered by [LovyanGFX](https://github.com/lovyan03/LovyanGFX).

### Cloud Specific
- **Bubbletea TUI** — full terminal UI with budget cards, positions, market scanner, decision log, wisdom stats. Dark/light themes.
- **Config file support** — TOML-style `survaiv.toml` with auto-detection (`./survaiv.toml` → `~/.config/survaiv/config.toml`), explicit `--config` flag, or env vars. See `survaiv.toml.example`.
- **Dynamic runtime config** — auto-adapts prompt budget, completion limits, and market limit based on detected model context window and host hardware.
- **Bind address control** — `--listen 127.0.0.1` for local-only, `--port 9090` for custom port. Also settable via config file or env vars.
- **CPU core limiting** — `--cores 4` or `--cores 50%` to cap GOMAXPROCS. Clamps to actual core count with safe defaults.
- **Parallel execution** — on ≥4 CPU cores, independent tasks (geoblock + market fetch, tool calls + wisdom checks) run concurrently.
- **Docker & Compose** — `Dockerfile` + `docker-compose.yml` with optional PostgreSQL profile.
- **SQLite or PostgreSQL** — SQLite by default (zero config), PostgreSQL via `SURVAIV_DATABASE_URL`. Auto-detected from DSN.
- **Heroku-ready** — auto-detects via `DYNO` env, runs headless with dashboard HTTP only.
- **4 LLM providers** — tx402, x402engine, claw402, custom (all via provider adapter interface).

## Quick Start

### ESP32 (All Boards)

> **🌐 No toolchain?** Flash directly from your browser via [Web Flasher](https://kast.github.io/survAIv/) — just plug in a board and click.

```bash
# Prerequisites: ESP-IDF v5.5+
. $IDF_PATH/export.sh

# Unified flash script — builds any board from the repo root
./flash.sh                              # pico — C3 (default, OTA)
./flash.sh --board s3                   # core — S3 N16R8
./flash.sh --board tqt                  # nano — T-QT Pro (LCD)
./flash.sh --board atoms3               # atom — AtomS3 (LCD)
./flash.sh --board stickc2              # spark — StickC PLUS2 (LCD)
./flash.sh --board c3oled               # dot — C3 SuperMini OLED
./flash.sh --list                       # list all supported boards

# Common options (work with any board):
./flash.sh --board <name> --no-ota      # disable OTA: larger app partition
./flash.sh --board <name> -m            # open serial monitor after flash
./flash.sh --board <name> --wallet <key> # provision wallet private key
./flash.sh --help                       # full usage

# Or flash from board subdirectory directly:
cd tqt && ./flash.sh                    # builds from board dir
cd atoms3 && ./flash.sh --no-ota -m     # no-OTA + monitor
```

On first boot, connect to the **SURVAIV-SETUP** WiFi AP and follow the captive portal wizard.

### Boards with Displays

Four boards include on-device screens showing live agent stats:

| Board | Tier | SoC | CPU | Screen | Resolution | Interface | Buttons |
|-------|------|-----|-----|--------|------------|-----------|---------|
| [LilyGO T-QT Pro](https://lilygo.cc/products/t-qt-pro) | `nano` | ESP32-S3 (FN4R2) | Xtensa LX7 · 2 cores | 0.85" GC9107 | 128×128 | SPI | 2 (GPIO0, GPIO47) |
| [M5Stack AtomS3](https://docs.m5stack.com/en/core/AtomS3) | `atom` | ESP32-S3 | Xtensa LX7 · 2 cores | 0.85" GC9107 | 128×128 | SPI | 1 (GPIO41) |
| [M5StickC PLUS2](https://docs.m5stack.com/en/core/M5StickC%20PLUS2) | `spark` | ESP32-PICO-V3-02 | Xtensa LX6 · 2 cores | 1.14" ST7789V2 | 135×240 | SPI | 2 (GPIO37, GPIO39) |
| [C3 SuperMini OLED](https://www.espboards.dev/esp32/esp32-c3-oled-042/) | `dot` | ESP32-C3 | RISC-V · 1 core | 0.42" SSD1306 | 72×40 | I2C | 1 (GPIO9 BOOT) |

```bash
# From repo root:
./flash.sh --board tqt          # nano — LilyGO T-QT Pro
./flash.sh --board atoms3       # atom — M5Stack AtomS3
./flash.sh --board stickc2      # spark — M5StickC PLUS2
./flash.sh --board c3oled       # dot — ESP32-C3 SuperMini OLED

# Or from board subdirectory:
cd tqt && ./flash.sh            # auto-fetches LovyanGFX on first build
./flash.sh --no-ota -m          # no-OTA + monitor
```

The screen auto-dims after 30 seconds of inactivity; press any button to wake it.
The C3 OLED uses a compact 5-line monochrome layout (mode, equity, P&L, positions, status).

### Cloud / TUI

```bash
cd cloud
cp .env.example .env
# Edit .env — at minimum set SURVAIV_OAI_URL and SURVAIV_OAI_MODEL
# Or use a config file:
cp survaiv.toml.example survaiv.toml
# Edit survaiv.toml

# macOS / Linux:
./build.sh build       # compile
./build.sh run         # build + run (TUI mode)
./build.sh headless    # dashboard only

# Windows:
build.bat build
build.bat run

# Or with Make:
make run

# Or manually:
go build -o survaiv .
./survaiv                              # TUI + dashboard on 0.0.0.0:8080
./survaiv --headless                   # dashboard only (for servers)
./survaiv --listen 127.0.0.1           # local-only access
./survaiv --port 9090                  # custom port
./survaiv --cores 4                    # limit to 4 CPU cores
./survaiv --cores 50%                  # use half of available cores
./survaiv --config /path/to/config.toml
./survaiv --version                    # print version
```

Dashboard at `http://localhost:8080`. See [cloud/README.md](cloud/README.md) for full env var reference and build options.

### Docker

```bash
cd cloud

# SQLite (zero config):
docker compose up --build

# With PostgreSQL:
docker compose --profile pg up --build
# Set SURVAIV_DATABASE_URL in .env to connect survaiv to the Postgres container
```

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

> **Note:** Prices shown are approximate at the time of writing. Refer to each provider's official page for current pricing.

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
5. **Persist** — wisdom survives reboots (NVS on ESP32, SQLite/PostgreSQL on cloud).

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
Pi + Opus 4 (giga/cloud)          →  export  →  survaiv-knowledge-v2.json
                                               │
ESP32-C3 (pico, paper)             ←  import  ←──────┘  (auto-truncated to 800B budget)
C3 SuperMini OLED (dot)        ←  import  ←──────┘  (800B budget, same as pico)
T-QT Pro / AtomS3 (nano/atom)  ←  import  ←──────┘  (2–4 KB budget)
ESP32-S3 N16R8 (core, live)        ←  import  ←──────┘  (4 KB budget, full fidelity)
Another cloud agent (giga)     ←  import  ←──────┘  (8 KB budget)
```

The export includes:
- **Scope metadata** — total decisions tracked, unique markets, categories (crypto, politics…), time range of training data
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
5. **Import to target** — upload to any agent (pico, nano, atom, spark, core, dot, another giga instance). The rules are compressed to fit.
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

| Platform | Tier | Budget | Typical Content |
|----------|------|--------|-----------------|
| C3 (OTA) | `pico` | 800 bytes | 10–12 terse rules + compact stats |
| C3 (no-OTA) | `pico` | 2,000 bytes | 25+ rules with context |
| C3 OLED | `dot` | 800 bytes | 10–12 terse rules + compact stats |
| T-QT Pro | `nano` | 2,000 bytes | 25+ rules with context |
| StickC PLUS2 | `spark` | 2,000 bytes | 25+ rules with context |
| AtomS3 | `atom` | 4,000 bytes | Full rule set + verbose stats |
| S3 N16R8 | `core` | 4,000 bytes | Full rule set + verbose stats |
| Cloud | `giga` | 8,000 bytes | Comprehensive rules + category breakdowns |

## Efficiency Score

The dashboard settings modal shows an **agent efficiency score** (0–100) that quantifies how well the current platform can run the agent, with a visual comparison across all supported boards.

### How It's Computed

The score is the sum of five weighted criteria, each measuring a different dimension of agent capability:

| Criterion | Weight | What It Measures | How It Scales |
|-----------|--------|------------------|---------------|
| **Context capacity** | 0–30 | Prompt budget available for market data + wisdom | `30 × min(prompt_budget / 32000, 1.0)` |
| **Parallelism** | 0–20 | Concurrent task execution (goroutines / cores) | `20 × min(workers / 4, 1.0)` |
| **Memory headroom** | 0–15 | Available RAM for HTTP buffers, JSON parsing | 15 if >1 GB, 10 if >256 MB, 5 if >64 MB, 0 otherwise |
| **Market coverage** | 0–20 | Markets analyzed per cycle (more = better selection) | `20 × min(market_limit / 50, 1.0)` |
| **Wisdom capacity** | 0–15 | Bytes available for learned trading rules | `15 × min(wisdom_budget / 8192, 1.0)` |

**Total = Context + Parallelism + Memory + Coverage + Wisdom** (max 100)

### Platform Comparison

| Platform | Tier | Context | Parallelism | Memory | Coverage | Wisdom | **Total** |
|----------|------|---------|-------------|--------|----------|--------|-----------|
| ESP32-C3 (OTA) | `pico` | 2 | 0 | 0 | 2 | 1 | **~12** |
| ESP32-C3 (no-OTA) | `pico` | 4 | 0 | 0 | 2 | 4 | **~19** |
| C3 SuperMini OLED | `dot` | 2 | 0 | 0 | 2 | 1 | **~12** |
| T-QT Pro | `nano` | 4 | 5 | 5 | 5 | 4 | **~26** |
| StickC PLUS2 | `spark` | 4 | 0 | 5 | 5 | 4 | **~22** |
| AtomS3 | `atom` | 4 | 5 | 0 | 5 | 7 | **~25** |
| ESP32-S3 N16R8 | `core` | 8 | 5 | 10 | 10 | 7 | **~40** |
| Cloud (4-core, 128K model) | `giga` | 30 | 10 | 10–15 | 8 | 15 | **~75** |
| Cloud (8-core, 128K model) | `giga` | 30 | 20 | 15 | 8 | 15 | **~88** |
| Cloud (8-core, 1M model) | `giga` | 30 | 20 | 15 | 20 | 15 | **~100** |

### How Context Adapts to the Model

On the cloud version, the dynamic config auto-detects the model's context window from the built-in catalog and computes:

| Model Context | Prompt Budget | Max Completion | Markets/Cycle |
|---------------|---------------|----------------|---------------|
| Unknown | 16,000 tokens | 1,000 tokens | 10 |
| 32K | 19,200 tokens | 2,000 tokens | 12 |
| 128K | 32,000 tokens | 4,000 tokens | 21 |
| 256K+ | 32,000 tokens (capped) | 4,000 tokens | 21 |
| 1M+ (Llama 4 Maverick) | 32,000 tokens (capped) | 4,000 tokens | 21 |

**Formula:** `prompt_budget = min(context_K × 1000 × 0.6, 32000)`, floor at 2,000.

ESP32 boards use fixed token budgets (determined by available SRAM and flash layout) — the dynamic config is a cloud-only feature. On constrained boards, the prompt is pre-trimmed by the firmware to fit.

### Dashboard Visualization

The settings modal displays:
- A **color-coded bar** (green ≥70, yellow ≥40, red <40) with the numeric score
- **Breakdown** of all five criteria
- **Platform comparison grid** with mini progress bars for every supported board

## Security

All API endpoints are protected by a PIN-based authentication system:

1. **First boot** — the agent generates a random PIN in `adjective-animal-number` format (e.g. `swift-newt-6750`) and prints it to the serial console (ESP32) or stdout (cloud).
2. **Claim** — the first user to enter the PIN in the dashboard owns the agent and receives a session token.
3. **Session token** — all subsequent API requests must include the token via `X-Auth-Token` header (or `Authorization: Bearer` for cloud). SSE uses a `?token=` query parameter since `EventSource` can't set custom headers.
4. **Unclaimed agents** — if no one has claimed the agent yet, all endpoints are open (for first-boot UX).

Protected endpoints include all state, positions, history, equity, scouted markets, wisdom, knowledge export, backup, wallet generation, and SSE streams.

## Architecture

### ESP32 — `main/`

```
main/
├── main.cpp              — Boot → banner → NVS → WiFi → SNTP → wallet → x402 → agent loop
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
└── flash.sh              — S3 build/flash script (--no-ota supported)

tqt/                      — nano · LilyGO T-QT Pro (ESP32-S3, 128×128 GC9107 LCD)
atoms3/                   — atom · M5Stack AtomS3 (ESP32-S3, 128×128 GC9107 LCD)
stickc2/                  — spark · M5StickC PLUS2 (ESP32, 135×240 ST7789V2 LCD)
c3oled/                   — dot · ESP32-C3 SuperMini (72×40 SSD1306 OLED)
└── Each: CMakeLists.txt, sdkconfig.defaults[.no_ota], partitions[_no_ota].csv, flash.sh

boards/
├── screen/               — LovyanGFX display driver component
│   ├── screen.cpp        — Per-board pin configs, layout rendering, backlight/button handling
│   └── include/screen.h  — ScreenData struct, public API
└── LovyanGFX/            — Auto-cloned on first display-board build (gitignored)
```

### Cloud — `cloud/`

```
cloud/
├── main.go               — Entry point, flags (--config, --listen, --port), signal handling
├── internal/agent/       — LLM prompts, parsing, cycle orchestration, parallel fetch
├── internal/dashboard/   — Thread-safe state, HTTP handlers, embedded HTML
├── internal/dynconfig/   — Dynamic runtime config, efficiency scoring
├── internal/wisdom/      — Outcome tracking, rule generation, import/export
├── internal/tui/         — Bubbletea terminal UI (8 files)
├── internal/config/      — Config file (TOML) + env vars + DB-backed overrides
├── internal/db/          — SQLite + PostgreSQL (auto-detected), migrations, compat layer
├── internal/httpclient/  — HTTP client with LLM retry (120s timeout, 3 retries)
├── internal/ledger/      — Budget & position accounting
├── internal/models/      — LLM model registry + selection
├── internal/news/        — News search (Tavily, Brave) for market context
├── internal/polymarket/  — Market + geoblock API
├── internal/provider/    — Provider adapters (tx402, x402engine, claw402, custom)
├── internal/x402/        — Micropayment signatures
├── internal/wallet/      — Ethereum wallet management
├── internal/crypto/      — EIP-712, Keccak-256, secp256k1
└── internal/types/       — Shared type definitions
```

## Platform Differences

| Feature | pico (OTA) | pico (no-OTA) | core | nano | atom | spark | dot | giga |
|---------|----------|-------------|----|----|----|----|-----|-----|
| Prompt budget | 2,000 tok | 4,000 tok | 8,000 tok | 4,000 tok | 4,000 tok | 4,000 tok | 2,000 tok | adaptive (up to 32K) |
| Max completion | 2,000 tok | 2,000 tok | 2,000 tok | 2,000 tok | 2,000 tok | 2,000 tok | 2,000 tok | adaptive (1K–4K) |
| Markets per scan | 6 | 12 | 50 | 12 | 12 | 12 | 6 | adaptive (up to 50) |
| HTTP body size | 64 KB | 128 KB | 512 KB | 128 KB | 128 KB | 128 KB | 64 KB | unlimited |
| Wisdom budget | 800 B | 2,000 B | 4,000 B | 2,000 B | 4,000 B | 2,000 B | 800 B | 8,000 B |
| On-board display | — | — | — | 128×128 LCD | 128×128 LCD | 135×240 LCD | 72×40 OLED | N/A |
| OTA updates | ✅ | — | ✅ | ✅ | ✅ | ✅ | ✅ | N/A |
| x402 providers | tx402 only* | tx402 only* | all 4 | tx402 only* | all 4 | all 4 | tx402 only* | all 4 |
| Persistence | NVS flash | NVS flash | NVS flash | NVS flash | NVS flash | NVS flash | NVS flash | SQLite / PostgreSQL |
| UI | Web dashboard | Web dashboard | Web dashboard | LCD + web | LCD + web | LCD + web | OLED + web | TUI + web |
| Deployment | USB flash | USB flash | USB flash | USB flash | USB flash | USB flash | USB flash | `go build` / Docker / Heroku |

\* x402engine catalog too large for boards with ≤4 MB flash / no PSRAM.

## Budget Assessment

Using tx402.ai with DeepSeek V3.2 ($0.0005/request, as of April 2025 — check [tx402.ai](https://tx402.ai) for current prices):

| Bankroll | LLM Rounds | Days at 96 cycles/day |
|----------|------------|----------------------|
| $15 USDC | ~30,000 | ~312 days |
| $25 USDC | ~50,000 | ~520 days |

The real constraint is trading P&L, not inference cost.

## Disclaimer

This software is provided for **educational and research purposes only**. Survaiv is an experiment in autonomous AI agent behaviour — specifically, how an agent allocates scarce resources (money) when it must pay for its own cognition and bear the consequences of its decisions.

**This is not financial advice.** This project does not promote, endorse, or guarantee any trading strategy, investment approach, or money-making scheme. Prediction markets carry inherent risk, and autonomous agents make mistakes.

Key points:

- **Expect to lose your entire bankroll.** The default $15–25 USDC budget is intentionally tiny to keep the experiment low-stakes.
- **Paper mode is the default** — no real money is at risk unless you explicitly opt in to live trading and fund a wallet.
- **Live trading is capped** by configurable guardrails: daily loss limits, confidence thresholds, position size limits, and a hard reserve floor.
- **No guarantees of any kind** — the agent may make poor decisions, encounter bugs, or lose connectivity at critical moments.
- **You are solely responsible** for any funds you choose to deposit and any trades the agent executes on your behalf.
- **Check your local laws** — prediction market trading may be restricted or prohibited in your jurisdiction.

The authors and contributors of this project accept no liability for financial losses incurred through the use of this software.

## License

MIT
