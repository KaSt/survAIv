# survaiv cloud — build targets
# Works on macOS, Linux, and Windows (with GNU Make / WSL / Git Bash).

APP      := survaiv
VERSION  := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
LDFLAGS  := -s -w -X main.Version=$(VERSION)

# CGO is required for go-sqlite3.
export CGO_ENABLED := 1

.PHONY: all build run test vet clean deps cross

all: build

deps:
	go mod download

build: deps
	go build -ldflags "$(LDFLAGS)" -o $(APP) .

run: build
	./$(APP)

headless: build
	./$(APP) --headless

test:
	go test ./...

vet:
	go vet ./...

clean:
	rm -f $(APP) $(APP).exe $(APP)-*

# Cross-compile for common targets (CGO cross-compilation needs appropriate toolchain).
cross:
	GOOS=linux   GOARCH=amd64 go build -ldflags "$(LDFLAGS)" -o $(APP)-linux-amd64 .
	GOOS=linux   GOARCH=arm64 go build -ldflags "$(LDFLAGS)" -o $(APP)-linux-arm64 .
	GOOS=darwin  GOARCH=amd64 go build -ldflags "$(LDFLAGS)" -o $(APP)-darwin-amd64 .
	GOOS=darwin  GOARCH=arm64 go build -ldflags "$(LDFLAGS)" -o $(APP)-darwin-arm64 .
	@echo "Note: Windows cross-compile requires a mingw-w64 CC for CGO (sqlite3)."
	@echo "  Install with: brew install mingw-w64  (macOS) or apt install gcc-mingw-w64 (Linux)"
	@echo "  Then: CC=x86_64-w64-mingw32-gcc GOOS=windows GOARCH=amd64 go build -ldflags '$(LDFLAGS)' -o $(APP)-windows-amd64.exe ."
