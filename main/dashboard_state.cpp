#include "dashboard_state.h"

#include <cstdio>
#include <ctime>
#include <sstream>

#include "config.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_freertos_hooks.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "json_util.h"

// CPU usage monitoring via FreeRTOS idle hook counters.
// Each core's idle task calls our hook when it has nothing to do.
// We count calls per sampling window and self-calibrate: the maximum
// observed count = 0% CPU usage; fewer counts = higher usage.
namespace {

static volatile uint32_t s_idle_cnt[2] = {0, 0};
static uint32_t s_prev_cnt[2] = {0, 0};
static uint32_t s_max_delta[2] = {1, 1};
static int s_cpu_pct[2] = {0, 0};
static int64_t s_prev_us = 0;
static int s_num_cores = 0;
static bool s_hooks_ready = false;

static bool idle_hook_0(void) { s_idle_cnt[0] = s_idle_cnt[0] + 1; return false; }
static bool idle_hook_1(void) { s_idle_cnt[1] = s_idle_cnt[1] + 1; return false; }

static void EnsureCpuHooks() {
  if (s_hooks_ready) return;
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  s_num_cores = chip.cores;
  esp_register_freertos_idle_hook_for_cpu(idle_hook_0, 0);
  if (s_num_cores > 1)
    esp_register_freertos_idle_hook_for_cpu(idle_hook_1, 1);
  s_prev_us = esp_timer_get_time();
  s_hooks_ready = true;
}

static void SampleCpu() {
  if (!s_hooks_ready) return;
  int64_t now = esp_timer_get_time();
  if (now - s_prev_us < 1000000) return;  // ≥1 s between samples.
  for (int c = 0; c < s_num_cores && c < 2; ++c) {
    uint32_t cnt = s_idle_cnt[c];
    uint32_t delta = cnt - s_prev_cnt[c];
    s_prev_cnt[c] = cnt;
    if (delta > s_max_delta[c]) s_max_delta[c] = delta;
    int pct = 100 - static_cast<int>(
        static_cast<uint64_t>(delta) * 100 / s_max_delta[c]);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_cpu_pct[c] = pct;
  }
  s_prev_us = now;
}

}  // namespace

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

void DashboardState::SetScoutedMarkets(const std::vector<ScoutedMarket> &scouted) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  scouted_markets_ = scouted;
  // Trim to max.
  if (static_cast<int>(scouted_markets_.size()) > kMaxScoutedMarkets) {
    scouted_markets_.resize(kMaxScoutedMarkets);
  }
  xSemaphoreGive(mutex_);
}

double DashboardState::InferenceSpentUsdc() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  double val = inference_spent_usdc_;
  xSemaphoreGive(mutex_);
  return val;
}

double DashboardState::GetEquity() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  double val = equity_;
  xSemaphoreGive(mutex_);
  return val;
}

double DashboardState::GetCash() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  double val = cash_;
  xSemaphoreGive(mutex_);
  return val;
}

void DashboardState::IncrementCycleCount() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  cycle_count_++;
  xSemaphoreGive(mutex_);
}

void DashboardState::SetLastError(const std::string &error) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  last_error_ = error;
  xSemaphoreGive(mutex_);
}

void DashboardState::SetNextRetrySec(int seconds) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  next_retry_sec_ = seconds;
  xSemaphoreGive(mutex_);
}

void DashboardState::SetNextCycleEpoch(int64_t epoch) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  next_cycle_epoch_ = epoch;
  xSemaphoreGive(mutex_);
}

void DashboardState::ClearError() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  last_error_.clear();
  next_retry_sec_ = 0;
  xSemaphoreGive(mutex_);
}

void DashboardState::SetResetPaperFunc(ResetCallback fn) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  on_reset_paper_ = std::move(fn);
  xSemaphoreGive(mutex_);
}

bool DashboardState::ResetPaperTrading() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  // Only allow reset in paper mode.
  bool paper = !live_mode_;
  ResetCallback fn = on_reset_paper_;
  if (paper) {
    decisions_.clear();
    equity_history_.clear();
    positions_.clear();
    scouted_markets_.clear();
    cash_ = 0.0;
    reserve_ = 0.0;
    equity_ = 0.0;
    llm_spend_ = 0.0;
    realized_pnl_ = 0.0;
    daily_loss_ = 0.0;
    inference_spent_usdc_ = 0.0;
    cycle_count_ = 0;
    last_error_.clear();
  }
  xSemaphoreGive(mutex_);
  if (paper && fn) fn();
  return paper;
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

  if (!last_error_.empty()) {
    o << ",\"last_error\":\"" << JsonEscape(last_error_) << "\""
      << ",\"next_retry_sec\":" << next_retry_sec_;
  }

  if (next_cycle_epoch_ > 0) {
    o << ",\"next_cycle_epoch\":" << next_cycle_epoch_;
  }

  const esp_app_desc_t *app = esp_app_get_description();
  o << ",\"firmware\":\"" << app->version << " (" << app->date << ")\""
    << ",\"tier\":\"" << SURVAIV_TIER << "\"";

  // Agent name from NVS config.
  std::string aname = config::AgentName();
  if (!aname.empty()) {
    o << ",\"agent_name\":\"" << JsonEscape(aname) << "\"";
  }

  // Paper-only flag for trading mode UI.
  o << ",\"paper_only\":" << (config::PaperTradingOnly() ? 1 : 0);

  // Tool usage level for settings UI.
  o << ",\"tool_usage\":" << config::ToolUsageLevel();

  // News search config presence.
  o << ",\"news_provider\":\"" << JsonEscape(config::NewsProvider()) << "\"";
  o << ",\"has_news_key\":" << (config::NewsApiKey().empty() ? "false" : "true");

#if CONFIG_SURVAIV_ENABLE_OTA
  o << ",\"ota_enabled\":true";
#else
  o << ",\"ota_enabled\":false";
#endif

  // System stats + CPU usage.
  EnsureCpuHooks();
  SampleCpu();
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  o << ",\"sys\":{\"cores\":" << static_cast<int>(chip.cores)
    << ",\"free_heap\":" << esp_get_free_heap_size()
    << ",\"min_free_heap\":" << esp_get_minimum_free_heap_size()
    << ",\"total_heap\":" << heap_caps_get_total_size(MALLOC_CAP_DEFAULT)
    << ",\"cpu\":[";
  for (int c = 0; c < s_num_cores && c < 2; ++c) {
    if (c > 0) o << ",";
    o << s_cpu_pct[c];
  }
  o << "]}";

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
      << ",\"rationale\":\"" << JsonEscape(d.rationale) << "\"";
    if (!d.tools_used.empty()) {
      o << ",\"tools_used\":[";
      for (size_t t = 0; t < d.tools_used.size(); ++t) {
        if (t > 0) o << ",";
        o << "\"" << JsonEscape(d.tools_used[t]) << "\"";
      }
      o << "]";
    }
    o << "}";
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

std::string DashboardState::ScoutedMarketsJson() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);

  std::ostringstream o;
  o << "[";
  for (size_t i = 0; i < scouted_markets_.size(); ++i) {
    const auto &s = scouted_markets_[i];
    if (i > 0) o << ",";
    o << "{\"epoch\":" << s.epoch
      << ",\"market_id\":\"" << JsonEscape(s.market_id) << "\""
      << ",\"question\":\"" << JsonEscape(s.question) << "\""
      << ",\"signal\":\"" << JsonEscape(s.signal) << "\""
      << ",\"edge_bps\":" << s.edge_bps
      << ",\"confidence\":" << s.confidence
      << ",\"note\":\"" << JsonEscape(s.note) << "\""
      << ",\"yes_price\":" << s.yes_price
      << ",\"volume\":" << s.volume
      << ",\"liquidity\":" << s.liquidity
      << "}";
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
