@echo off
REM survaiv cloud — build & run script for Windows
REM Requires: Go 1.24+

cd /d "%~dp0"

for /f "tokens=*" %%i in ('git describe --tags --always --dirty 2^>nul') do set VERSION=%%i
if "%VERSION%"=="" set VERSION=dev
set LDFLAGS=-s -w -X main.Version=%VERSION%

if "%~1"=="" goto build
if "%~1"=="build" goto build
if "%~1"=="run" goto run
if "%~1"=="headless" goto headless
if "%~1"=="test" goto test
if "%~1"=="clean" goto clean
if "%~1"=="deps" goto deps
goto usage

:deps
echo . Downloading dependencies...
go mod download
goto end

:build
echo . Building survaiv (%VERSION%)...
go mod download
go build -ldflags "%LDFLAGS%" -o survaiv.exe .
echo + Built survaiv.exe
goto end

:run
echo . Building ^& running survaiv (TUI mode)...
go mod download
go build -ldflags "%LDFLAGS%" -o survaiv.exe .
survaiv.exe
goto end

:headless
echo . Building ^& running survaiv (headless / dashboard only)...
go mod download
go build -ldflags "%LDFLAGS%" -o survaiv.exe .
survaiv.exe --headless
goto end

:test
echo . Running tests...
go test ./...
goto end

:clean
echo . Cleaning...
del /q survaiv.exe survaiv-*.exe 2>nul
echo + Clean
goto end

:usage
echo Usage: %~nx0 {build^|run^|headless^|test^|clean^|deps}
exit /b 1

:end
