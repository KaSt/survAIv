#pragma once

#include <string>

namespace survaiv {
namespace config {

// Load all config from NVS (falling back to Kconfig defaults).
void Init();

// Save a string value to NVS.
void SetString(const char *key, const std::string &value);
void SetInt(const char *key, int value);

// Getters — return NVS value if present, else Kconfig default.
std::string WifiSsid();
std::string WifiPassword();
std::string OpenaiBaseUrl();
std::string OpenaiModel();
std::string ApiKey();
std::string WalletPrivateKey();
std::string PolygonRpcUrl();
std::string ClobBaseUrl();
std::string LlmProvider();     // "apikey", "x402", or "claw402"
int LoopSeconds();
int StartingBankrollCents();
int ReserveCents();
int MaxOpenPositions();
int MarketLimit();
bool PaperTradingOnly();
int DailyLossLimitCents();
int LiveConfidenceThreshold();
int LiveMinEdgeBps();
int CooldownAfterLossSeconds();

// Returns true if the essential config (WiFi SSID) is set in NVS.
bool HasStoredConfig();

// Reboot the device.
void Reboot();

}  // namespace config
}  // namespace survaiv
