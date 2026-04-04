# ── Build stage ──────────────────────────────────────────────────
FROM golang:1.24-bookworm AS builder

WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download

COPY . .

ARG VERSION=docker
RUN CGO_ENABLED=1 go build -ldflags "-s -w -X main.Version=${VERSION}" -o /survaiv .

# ── Runtime stage ────────────────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /survaiv /usr/local/bin/survaiv

# Default env — override via docker-compose or -e flags.
ENV SURVAIV_PORT=8080
ENV SURVAIV_DB_PATH=/data/survaiv.db

VOLUME /data
EXPOSE 8080

ENTRYPOINT ["survaiv", "--headless"]
