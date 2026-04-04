#include "ledger.h"

#include <algorithm>
#include <cmath>

namespace survaiv {

BudgetLedger::BudgetLedger(double starting_usdc, double reserve_usdc)
    : cash_usdc_(starting_usdc), reserve_usdc_(reserve_usdc) {}

double BudgetLedger::Equity(const std::vector<Position> &positions,
                            const std::vector<MarketSnapshot> &markets) const {
  return cash_usdc_ + PositionsMarkToMarket(positions, markets);
}

double BudgetLedger::AvailableCashForNewSpend(
    const std::vector<Position> &positions,
    const std::vector<MarketSnapshot> &markets) const {
  double available = Equity(positions, markets) - reserve_usdc_ -
                     PositionsMarkToMarket(positions, markets);
  return std::max(0.0, available);
}

bool BudgetLedger::CanSpendOnInference(
    double usdc, const std::vector<Position> &positions,
    const std::vector<MarketSnapshot> &markets) const {
  return AvailableCashForNewSpend(positions, markets) >= usdc;
}

void BudgetLedger::DebitInference(double usdc) {
  cash_usdc_ = std::max(0.0, cash_usdc_ - usdc);
  llm_spend_usdc_ += usdc;
}

bool BudgetLedger::OpenPaperPosition(const MarketSnapshot &market,
                                     const std::string &side, double size_usdc) {
  double price = side == "yes" ? market.yes_price : market.no_price;
  if (price <= 0.01 || price >= 0.99 || size_usdc <= 0.0 || cash_usdc_ < size_usdc) {
    return false;
  }

  Position position;
  position.market_id = market.id;
  position.question = market.question;
  position.side = side;
  position.entry_price = price;
  position.stake_usdc = size_usdc;
  position.shares = size_usdc / price;
  position.is_live = false;

  cash_usdc_ -= size_usdc;
  positions_.push_back(position);
  return true;
}

bool BudgetLedger::ClosePaperPosition(const std::string &market_id,
                                      const std::vector<MarketSnapshot> &markets) {
  auto it = std::find_if(positions_.begin(), positions_.end(),
                         [&](const Position &p) { return p.market_id == market_id; });
  if (it == positions_.end()) {
    return false;
  }

  const MarketSnapshot *market = FindMarket(markets, market_id);
  if (market == nullptr) {
    return false;
  }

  double exit_price = it->side == "yes" ? market->yes_price : market->no_price;
  double proceeds = it->shares * exit_price;
  double pnl = proceeds - it->stake_usdc;
  cash_usdc_ += proceeds;
  realized_paper_pnl_usdc_ += pnl;
  if (pnl < 0.0) {
    daily_loss_usdc_ += std::fabs(pnl);
  }
  positions_.erase(it);
  return true;
}

double BudgetLedger::PositionsMarkToMarket(
    const std::vector<Position> &positions,
    const std::vector<MarketSnapshot> &markets) {
  double total = 0.0;
  for (const auto &position : positions) {
    const MarketSnapshot *market = FindMarket(markets, position.market_id);
    if (market == nullptr) {
      continue;
    }
    double price = position.side == "yes" ? market->yes_price : market->no_price;
    total += position.shares * price;
  }
  return total;
}

void BudgetLedger::MarkPositionLive(const std::string &market_id,
                                    const std::string &order_id) {
  for (auto &pos : positions_) {
    if (pos.market_id == market_id && !pos.is_live) {
      pos.is_live = true;
      pos.order_id = order_id;
      return;
    }
  }
}

void BudgetLedger::ResetPaper(double starting_usdc, double reserve_usdc) {
  cash_usdc_ = starting_usdc;
  reserve_usdc_ = reserve_usdc;
  llm_spend_usdc_ = 0.0;
  realized_paper_pnl_usdc_ = 0.0;
  daily_loss_usdc_ = 0.0;
  positions_.clear();
}

}  // namespace survaiv
