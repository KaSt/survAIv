#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "esp_err.h"

namespace survaiv {

struct HttpResponse {
  int status_code = -1;
  std::string body;
  std::map<std::string, std::string> headers;
  esp_err_t err = ESP_OK;
};

struct HttpContext {
  HttpResponse *response = nullptr;
  bool truncated = false;  // Set by event handler when body accumulation is stopped.
};

struct MarketSnapshot {
  std::string id;
  std::string question;
  std::string slug;
  std::string category;
  std::string end_date;
  std::string clob_token_yes;
  std::string clob_token_no;
  double liquidity = 0.0;
  double volume = 0.0;
  double yes_price = 0.0;
  double no_price = 0.0;
};

struct GeoblockStatus {
  bool blocked = true;
  std::string country;
  std::string region;
  std::string ip;
};

struct UsageStats {
  int prompt_tokens = 0;
  int completion_tokens = 0;
};

struct ToolCall {
  bool valid = false;
  std::string tool;
  std::string order = "volume24hr";
  int limit = 6;
  int offset = 0;
};

struct Decision {
  std::string type = "hold";
  std::string market_id;
  std::string side;
  double edge_bps = 0.0;
  double confidence = 0.0;
  double size_fraction = 0.0;
  std::string rationale;
};

struct Position {
  std::string market_id;
  std::string question;
  std::string side;
  double entry_price = 0.0;
  double shares = 0.0;
  double stake_usdc = 0.0;
  bool is_live = false;
  std::string order_id;
};

struct ScoutedMarket {
  int64_t epoch = 0;
  std::string market_id;
  std::string question;
  std::string signal;    // "bullish", "bearish", "neutral", "skip"
  double edge_bps = 0.0;
  double confidence = 0.0;
  std::string note;      // One-line agent reasoning
  double yes_price = 0.0;
  double volume = 0.0;
  double liquidity = 0.0;
};

inline const MarketSnapshot *FindMarket(const std::vector<MarketSnapshot> &markets,
                                        const std::string &market_id) {
  auto it = std::find_if(markets.begin(), markets.end(),
                         [&](const MarketSnapshot &m) { return m.id == market_id; });
  return it == markets.end() ? nullptr : &(*it);
}

}  // namespace survaiv
