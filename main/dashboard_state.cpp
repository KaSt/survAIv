#include "dashboard_state.h"

#include <cstdio>
#include <ctime>
#include <sstream>

#include "esp_ota_ops.h"
#include "json_util.h"

namespace survaiv {

DashboardState::DashboardState() {
  mutex_ = xSemaphoreCreateMutex();
  time_t now;
  time(&now);
  boot_epoch_ = static_cast<int64_t>(now);
}

void DashboardState::UpdateBudget(double cash, double reserve, double equity,
                                  double llm_spend, double realized_pnl,
                                  double daily_loss) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  cash_ = cash;
  reserve_ = reserve;
  equity_ = equity;
  llm_spend_ = llm_spend;
  realized_pnl_ = realized_pnl;
  daily_loss_ = daily_loss;

  // Record equity snapshot.
  EquitySnapshot snap;
  time_t now;
  time(&now);
  snap.epoch = static_cast<int64_t>(now);
  snap.equity = equity;
  snap.cash = cash;
  if (static_cast<int>(equity_history_.size()) >= kMaxEquityHistory) {
    equity_history_.erase(equity_history_.begin());
  }
  equity_history_.push_back(snap);
  xSemaphoreGive(mutex_);
}

void DashboardState::UpdatePositions(const std::vector<Position> &positions) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  positions_ = positions;
  xSemaphoreGive(mutex_);
}

void DashboardState::UpdateMarkets(const std::vector<MarketSnapshot> &markets) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  markets_ = markets;
  xSemaphoreGive(mutex_);
}

void DashboardState::PushDecision(const DecisionRecord &record) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (static_cast<int>(decisions_.size()) >= kMaxDecisionHistory) {
    decisions_.erase(decisions_.begin());
  }
  decisions_.push_back(record);
  xSemaphoreGive(mutex_);
}

void DashboardState::SetAgentStatus(const std::string &status) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  agent_status_ = status;
  xSemaphoreGive(mutex_);
}

void DashboardState::SetGeoblock(bool blocked, const std::string &country) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  geoblocked_ = blocked;
  geo_country_ = country;
  xSemaphoreGive(mutex_);
}

void DashboardState::SetWalletInfo(const std::string &address, double usdc_balance) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  wallet_address_ = address;
  usdc_balance_ = usdc_balance;
  xSemaphoreGive(mutex_);
}

void DashboardState::SetLiveMode(bool enabled) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  live_mode_ = enabled;
  xSemaphoreGive(mutex_);
}

void DashboardState::SetInferenceSpend(double usdc) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  inference_spent_usdc_ = usdc;
  xSemaphoreGive(mutex_);
}

void DashboardState::SetActiveModel(const std::string &model_name, double price_per_req) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  active_model_ = model_name;
  model_price_ = price_per_req;
  xSemaphoreGive(mutex_);
}

double DashboardState::InferenceSpentUsdc() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  double val = inference_spent_usdc_;
  xSemaphoreGive(mutex_);
  return val;
}

void DashboardState::IncrementCycleCount() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  cycle_count_++;
  xSemaphoreGive(mutex_);
}

std::string DashboardState::ToJson() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);

  time_t now;
  time(&now);
  int64_t uptime = static_cast<int64_t>(now) - boot_epoch_;

  std::ostringstream o;
  o << "{\"status\":\"" << JsonEscape(agent_status_) << "\""
    << ",\"cycle_count\":" << cycle_count_
    << ",\"uptime_seconds\":" << uptime
    << ",\"live_mode\":" << (live_mode_ ? "true" : "false")
    << ",\"geoblocked\":" << (geoblocked_ ? "true" : "false")
    << ",\"geo_country\":\"" << JsonEscape(geo_country_) << "\""
    << ",\"wallet\":\"" << JsonEscape(wallet_address_) << "\""
    << ",\"usdc_balance\":" << usdc_balance_
    << ",\"budget\":{"
    << "\"cash\":" << cash_
    << ",\"reserve\":" << reserve_
    << ",\"equity\":" << equity_
    << ",\"llm_spend\":" << llm_spend_
    << ",\"realized_pnl\":" << realized_pnl_
    << ",\"daily_loss\":" << daily_loss_
    << "}"
    << ",\"inference_spent_usdc\":" << inference_spent_usdc_
    << ",\"active_model\":\"" << JsonEscape(active_model_) << "\""
    << ",\"model_price\":" << model_price_
    << ",\"open_positions\":" << static_cast<int>(positions_.size());

  const esp_app_desc_t *app = esp_app_get_description();
  o << ",\"firmware\":\"" << app->version << " (" << app->date << ")\"";

  o << "}";

  xSemaphoreGive(mutex_);
  return o.str();
}

std::string DashboardState::PositionsJson() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);

  std::ostringstream o;
  o << "[";
  for (size_t i = 0; i < positions_.size(); ++i) {
    const auto &p = positions_[i];
    if (i > 0) o << ",";

    // Find current price from markets.
    double current_price = p.entry_price;
    for (const auto &m : markets_) {
      if (m.id == p.market_id) {
        current_price = p.side == "yes" ? m.yes_price : m.no_price;
        break;
      }
    }
    double pnl = (current_price - p.entry_price) * p.shares;

    o << "{\"market_id\":\"" << JsonEscape(p.market_id) << "\""
      << ",\"question\":\"" << JsonEscape(p.question) << "\""
      << ",\"side\":\"" << p.side << "\""
      << ",\"entry_price\":" << p.entry_price
      << ",\"current_price\":" << current_price
      << ",\"shares\":" << p.shares
      << ",\"stake_usdc\":" << p.stake_usdc
      << ",\"unrealized_pnl\":" << pnl
      << ",\"is_live\":" << (p.is_live ? "true" : "false")
      << "}";
  }
  o << "]";

  xSemaphoreGive(mutex_);
  return o.str();
}

std::string DashboardState::DecisionHistoryJson() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);

  std::ostringstream o;
  o << "[";
  // Reverse order (newest first).
  for (int i = static_cast<int>(decisions_.size()) - 1; i >= 0; --i) {
    const auto &d = decisions_[i];
    if (i < static_cast<int>(decisions_.size()) - 1) o << ",";
    o << "{\"epoch\":" << d.epoch
      << ",\"type\":\"" << JsonEscape(d.type) << "\""
      << ",\"market_id\":\"" << JsonEscape(d.market_id) << "\""
      << ",\"question\":\"" << JsonEscape(d.market_question) << "\""
      << ",\"side\":\"" << JsonEscape(d.side) << "\""
      << ",\"confidence\":" << d.confidence
      << ",\"edge_bps\":" << d.edge_bps
      << ",\"size_usdc\":" << d.size_usdc
      << ",\"rationale\":\"" << JsonEscape(d.rationale) << "\""
      << "}";
  }
  o << "]";

  xSemaphoreGive(mutex_);
  return o.str();
}

std::string DashboardState::EquityHistoryJson() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);

  std::ostringstream o;
  o << "[";
  for (size_t i = 0; i < equity_history_.size(); ++i) {
    if (i > 0) o << ",";
    o << "[" << equity_history_[i].epoch << "," << equity_history_[i].equity << "]";
  }
  o << "]";

  xSemaphoreGive(mutex_);
  return o.str();
}

std::string DashboardState::SseStateEvent() const {
  return ToJson();
}

// Global singleton.
DashboardState &GetDashboardState() {
  static DashboardState instance;
  return instance;
}

}  // namespace survaiv
