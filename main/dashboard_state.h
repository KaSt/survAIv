#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "types.h"

namespace survaiv {

// Maximum entries kept in ring buffers.
constexpr int kMaxDecisionHistory = 50;
constexpr int kMaxEquityHistory = 200;
constexpr int kMaxScoutedMarkets = 20;

struct DecisionRecord {
  int64_t epoch = 0;
  std::string type;
  std::string market_id;
  std::string market_question;
  std::string side;
  double confidence = 0.0;
  double edge_bps = 0.0;
  double size_usdc = 0.0;
  std::string rationale;
};

struct EquitySnapshot {
  int64_t epoch = 0;
  double equity = 0.0;
  double cash = 0.0;
};

// Thread-safe shared state between agent loop and web server.
class DashboardState {
 public:
  DashboardState();

  // Called by the agent loop after each cycle.
  void UpdateBudget(double cash, double reserve, double equity,
                    double llm_spend, double realized_pnl, double daily_loss);
  void UpdatePositions(const std::vector<Position> &positions);
  void UpdateMarkets(const std::vector<MarketSnapshot> &markets);
  void PushDecision(const DecisionRecord &record);
  void SetAgentStatus(const std::string &status);
  void SetGeoblock(bool blocked, const std::string &country);
  void SetWalletInfo(const std::string &address, double usdc_balance);
  void SetLiveMode(bool enabled);
  void SetInferenceSpend(double usdc);
  void SetActiveModel(const std::string &model_name, double price_per_req);
  void SetScoutedMarkets(const std::vector<ScoutedMarket> &scouted);
  void IncrementCycleCount();

  // Called by the web server to serialize state as JSON.
  std::string ToJson() const;
  std::string PositionsJson() const;
  std::string DecisionHistoryJson() const;
  std::string EquityHistoryJson() const;
  std::string ScoutedMarketsJson() const;

  // Read current inference spend.
  double InferenceSpentUsdc() const;

  // SSE event generation — returns JSON for the latest state update.
  std::string SseStateEvent() const;

 private:
  mutable SemaphoreHandle_t mutex_;

  // Budget.
  double cash_ = 0.0;
  double reserve_ = 0.0;
  double equity_ = 0.0;
  double llm_spend_ = 0.0;
  double realized_pnl_ = 0.0;
  double daily_loss_ = 0.0;

  // Positions.
  std::vector<Position> positions_;

  // Markets.
  std::vector<MarketSnapshot> markets_;

  // Decision history (ring buffer).
  std::vector<DecisionRecord> decisions_;

  // Equity history (ring buffer).
  std::vector<EquitySnapshot> equity_history_;

  // Scouted markets from last cycle.
  std::vector<ScoutedMarket> scouted_markets_;

  // Agent status.
  std::string agent_status_ = "initializing";
  int cycle_count_ = 0;
  int64_t boot_epoch_ = 0;

  // Geoblock.
  bool geoblocked_ = false;
  std::string geo_country_;

  // Wallet.
  std::string wallet_address_;
  double usdc_balance_ = -1.0;
  bool live_mode_ = false;
  double inference_spent_usdc_ = 0.0;
  std::string active_model_;
  double model_price_ = 0.0;
};

// Global dashboard state instance.
DashboardState &GetDashboardState();

}  // namespace survaiv
