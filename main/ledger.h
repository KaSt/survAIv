#pragma once

#include <vector>

#include "types.h"

namespace survaiv {

class BudgetLedger {
 public:
  BudgetLedger(double starting_usdc, double reserve_usdc);

  double Cash() const { return cash_usdc_; }
  double Reserve() const { return reserve_usdc_; }
  double LlmSpend() const { return llm_spend_usdc_; }

  double Equity(const std::vector<Position> &positions,
                const std::vector<MarketSnapshot> &markets) const;

  double AvailableCashForNewSpend(const std::vector<Position> &positions,
                                  const std::vector<MarketSnapshot> &markets) const;

  bool CanSpendOnInference(double usdc, const std::vector<Position> &positions,
                           const std::vector<MarketSnapshot> &markets) const;

  void DebitInference(double usdc);

  bool OpenPaperPosition(const MarketSnapshot &market, const std::string &side,
                         double size_usdc);

  bool ClosePaperPosition(const std::string &market_id,
                          const std::vector<MarketSnapshot> &markets);

  const std::vector<Position> &Positions() const { return positions_; }
  int OpenPositionCount() const { return static_cast<int>(positions_.size()); }
  double RealizedPaperPnl() const { return realized_paper_pnl_usdc_; }
  double DailyLossUsdc() const { return daily_loss_usdc_; }
  void ResetDailyLoss() { daily_loss_usdc_ = 0.0; }

  // Reset all paper trading state back to initial bankroll.
  void ResetPaper(double starting_usdc, double reserve_usdc);

  // Mark a position as live (backed by a real CLOB order).
  void MarkPositionLive(const std::string &market_id, const std::string &order_id);

 private:
  static double PositionsMarkToMarket(const std::vector<Position> &positions,
                                      const std::vector<MarketSnapshot> &markets);

  double cash_usdc_ = 0.0;
  double reserve_usdc_ = 0.0;
  double llm_spend_usdc_ = 0.0;
  double realized_paper_pnl_usdc_ = 0.0;
  double daily_loss_usdc_ = 0.0;
  std::vector<Position> positions_;
};

}  // namespace survaiv
