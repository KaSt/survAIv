# survaiv cloud

Cloud port of the survaiv autonomous prediction market agent. Originally firmware for ESP32, now a Go binary with a Bubbletea TUI and web dashboard.

## Prerequisites

- **Go 1.24+**
- **GCC** (for `mattn/go-sqlite3` CGO compilation)
  - macOS: included with Xcode Command Line Tools (`xcode-select --install`)
  - Linux: `sudo apt install build-essential` (Debian/Ubuntu) or `sudo dnf groupinstall 'Development Tools'` (Fedora)
  - Windows: install [TDM-GCC](https://jmeubank.github.io/tdm-gcc/) or [MSYS2](https://www.msys2.org/) (`pacman -S mingw-w64-x86_64-gcc`)

## Quick Start

```bash
cp .env.example .env   # edit with your LLM endpoint
```

### macOS / Linux

```bash
./build.sh build       # compile
./build.sh run         # build + run (TUI mode)
./build.sh headless    # build + run (dashboard only)
./build.sh test        # run tests
./build.sh clean       # remove binaries
```

### Windows

```bat
build.bat build        & REM compile
build.bat run          & REM build + run (TUI mode)
build.bat headless     & REM build + run (dashboard only)
build.bat test         & REM run tests
build.bat clean        & REM remove binaries
```

### Make (any platform with GNU Make)

```bash
make              # build
make run          # build + run (TUI)
make headless     # build + run (dashboard only)
make test         # run tests
make cross        # cross-compile for linux/darwin amd64+arm64
make clean        # remove binaries
```

### Manual

```bash
CGO_ENABLED=1 go build -o survaiv .
./survaiv              # TUI mode
./survaiv --headless   # dashboard only
./survaiv --version    # print version
```

### Heroku

```bash
git push heroku master
```

The app auto-detects Heroku via `DYNO` env and runs headless. `PORT` is set automatically. Set `DATABASE_URL` to a Postgres connection string if you want Postgres instead of SQLite.

### Docker

```bash
# SQLite (zero config):
docker compose up --build

# With PostgreSQL:
docker compose --profile pg up --build
# Then set in .env:
#   SURVAIV_DATABASE_URL=postgres://survaiv:survaiv@postgres:5432/survaiv?sslmode=disable
```

Dashboard at `http://localhost:8080`. Data persists in Docker volumes.

## Database

SQLite (default) or PostgreSQL. The driver is auto-detected from the connection string:

| Backend | Config | Notes |
|---|---|---|
| SQLite | `SURVAIV_DB_PATH=survaiv.db` | Default. Zero setup. File-based. |
| PostgreSQL | `SURVAIV_DATABASE_URL=postgres://...` | Set `SURVAIV_DATABASE_URL` or `DATABASE_URL`. |

When both are set, `SURVAIV_DATABASE_URL` takes precedence over `SURVAIV_DB_PATH`.

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
| `SURVAIV_DATABASE_URL` | _(empty)_ | PostgreSQL connection string (overrides DB_PATH) |
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
