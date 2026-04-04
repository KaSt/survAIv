#pragma once

#include <cstdint>
#include <string>

namespace survaiv {
namespace wisdom {

struct TrackedDecision {
  int64_t epoch = 0;
  std::string market_id;
  std::string question;
  std::string category;
  std::string decision_type;  // "hold", "buy_yes", "buy_no", "paper_buy_yes", etc.
  std::string signal;         // "bullish", "bearish", "neutral"
  double yes_price_at_decision = 0.0;
  double confidence = 0.0;
  double edge_bps = 0.0;
  bool resolved = false;
  bool outcome_yes = false;
  double final_yes_price = 0.0;
  bool checked = false;
  int64_t last_check_epoch = 0;
};

struct WisdomStats {
  uint16_t total = 0;
  uint16_t correct = 0;
  uint16_t holds_correct = 0;
  uint16_t holds_total = 0;
  uint16_t buys_correct = 0;
  uint16_t buys_total = 0;
  struct CategoryStat {
    char name[16] = {};
    uint16_t total = 0;
    uint16_t correct = 0;
  };
  CategoryStat categories[8] = {};
};

void Init();

void TrackDecision(const std::string &market_id, const std::string &question,
                   const std::string &category, const std::string &decision_type,
                   const std::string &signal, double yes_price, double confidence,
                   double edge_bps);

void CheckOutcomes();
void EvaluateAndUpdateWisdom();
std::string GetWisdom();
std::string StatsJson();

}  // namespace wisdom
}  // namespace survaiv
