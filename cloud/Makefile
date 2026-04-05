# survaiv cloud — build targets
# Works on macOS, Linux, and Windows (with GNU Make / WSL / Git Bash).

APP      := survaiv
VERSION  := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
LDFLAGS  := -s -w -X main.Version=$(VERSION)

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

# Cross-compile for common targets (pure-Go, no CGO needed).
cross:
	CGO_ENABLED=0 GOOS=linux   GOARCH=amd64 go build -ldflags "$(LDFLAGS)" -o $(APP)-linux-amd64 .
	CGO_ENABLED=0 GOOS=linux   GOARCH=arm64 go build -ldflags "$(LDFLAGS)" -o $(APP)-linux-arm64 .
	CGO_ENABLED=0 GOOS=darwin  GOARCH=amd64 go build -ldflags "$(LDFLAGS)" -o $(APP)-darwin-amd64 .
	CGO_ENABLED=0 GOOS=darwin  GOARCH=arm64 go build -ldflags "$(LDFLAGS)" -o $(APP)-darwin-arm64 .
	CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -ldflags "$(LDFLAGS)" -o $(APP)-windows-amd64.exe .
