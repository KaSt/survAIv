# survaiv cloud

Cloud port of the survaiv autonomous prediction market agent. Originally firmware for ESP32, now a Go binary with a Bubbletea TUI and web dashboard.

## Prerequisites

- **Go 1.23+**
- **GCC** (for `mattn/go-sqlite3` CGO compilation)

## Setup

```bash
cp .env.example .env
# Edit .env with your LLM endpoint and optional wallet key
```

## Run

```bash
cd cloud
go build -o survaiv .
./survaiv
```

### Headless mode (no TUI, dashboard only)

```bash
./survaiv --headless
```

### Heroku

```bash
git push heroku master
```

The app auto-detects Heroku via `DYNO` env and runs headless. `PORT` is set automatically.

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `SURVAIV_OAI_URL` | `https://tx402.ai/v1` | LLM API base URL |
| `SURVAIV_OAI_MODEL` | `deepseek/deepseek-v3.2` | Default LLM model ID |
| `SURVAIV_API_KEY` | _(empty)_ | API key for LLM provider |
| `SURVAIV_WALLET_KEY` | _(empty)_ | Ethereum private key for x402 payments |
| `SURVAIV_PAPER_ONLY` | `true` | Paper trading mode |
| `SURVAIV_LOOP_SECONDS` | `900` | Agent cycle interval |
| `SURVAIV_STARTING_BANKROLL` | `25.0` | Starting USDC bankroll |
| `SURVAIV_RESERVE` | `5.0` | Reserve USDC (untouchable) |
| `SURVAIV_MAX_POSITIONS` | `3` | Max concurrent positions |
| `SURVAIV_MARKET_LIMIT` | `10` | Markets to fetch per cycle |
| `SURVAIV_DAILY_LOSS_LIMIT` | `5.0` | Max daily loss in USDC |
| `SURVAIV_DB_PATH` | `survaiv.db` | SQLite database path |
| `SURVAIV_PORT` / `PORT` | `8080` | Dashboard HTTP port |

## TUI Keybindings

| Key | Action |
|---|---|
| `q` | Quit |
| `t` | Toggle dark/light theme |
| `j` / `k` | Scroll decision log |
| `r` | Force refresh |

## Dashboard

The web dashboard is always available at `http://localhost:<port>`. It provides:

- Real-time budget, equity, and P&L cards
- Equity chart with historical snapshots
- Open positions table
- Market scanner with signal indicators
- Decision log with rationale
- Wisdom/learning stats with export/import
- Settings modal for LLM config (paper mode)
- SSE for real-time updates

## Architecture

```
main.go                  → entry point, wiring
internal/agent/          → LLM prompts, parsing, cycle orchestration
internal/dashboard/      → thread-safe state, HTTP handlers, embedded HTML
internal/wisdom/         → outcome tracking, rule generation, import/export
internal/tui/            → Bubbletea terminal UI
internal/config/         → env + SQLite config
internal/db/             → SQLite open + migrations
internal/httpclient/     → HTTP client with retry
internal/ledger/         → budget & position accounting
internal/polymarket/     → market + geoblock API
internal/models/         → LLM model registry + selection
internal/provider/       → LLM provider adapters
internal/x402/           → micropayment signatures
internal/wallet/         → Ethereum wallet
internal/crypto/         → EIP-712, keccak, signing
internal/types/          → shared type definitions
```
