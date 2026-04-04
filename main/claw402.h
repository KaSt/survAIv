#pragma once

#include <cstdint>
#include <string>

namespace survaiv {
namespace claw402 {

// Fear & Greed index snapshot.
struct FearGreed {
  bool valid = false;
  int value = 0;             // 0–100
  std::string classification; // "Extreme Fear", "Fear", "Neutral", "Greed", "Extreme Greed"
  int64_t timestamp = 0;
};

// BTC ETF flow data.
struct EtfInflow {
  bool valid = false;
  double total_net_flow = 0.0;  // in USD (positive = inflow, negative = outflow)
  std::string date;
};

// Single large trade (whale).
struct LargeTrade {
  std::string symbol;
  std::string side;    // "buy" or "sell"
  double amount = 0.0; // trade size in USD
  int64_t timestamp = 0;
};

struct LargeTradesResult {
  bool valid = false;
  LargeTrade trades[8]; // up to 8 recent whale trades
  int count = 0;
};

// News headline.
struct NewsItem {
  std::string title;
  std::string source;
  int64_t timestamp = 0;
};

struct NewsResult {
  bool valid = false;
  NewsItem items[6]; // up to 6 headlines
  int count = 0;
};

// Initialize the claw402 data provider (no-op currently; for future config).
void Init();

// Each call costs $0.001 USDC via x402 payment.
FearGreed FetchFearGreed();
EtfInflow FetchBtcEtfInflow();
LargeTradesResult FetchLargeTrades(const std::string &symbol);
NewsResult FetchNews(const std::string &lang = "en", int limit = 5);

// Total USDC spent on claw402 data calls this session.
double TotalSpentUsdc();

}  // namespace claw402
}  // namespace survaiv
