#include "ledger.h"

#include <algorithm>
#include <cmath>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *kTag = "ledger";
static const char *kNvsNamespace = "ledger";
static const char *kNvsKey = "state";

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
  SaveToNvs();
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
  SaveToNvs();
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
  SaveToNvs();
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
      SaveToNvs();
      return;
    }
  }
}

void BudgetLedger::ResetDailyLoss() {
  daily_loss_usdc_ = 0.0;
  SaveToNvs();
}

void BudgetLedger::ResetPaper(double starting_usdc, double reserve_usdc) {
  cash_usdc_ = starting_usdc;
  reserve_usdc_ = reserve_usdc;
  llm_spend_usdc_ = 0.0;
  realized_paper_pnl_usdc_ = 0.0;
  daily_loss_usdc_ = 0.0;
  positions_.clear();
  SaveToNvs();
}

// ── NVS persistence ──────────────────────────────────────────────────────────

void BudgetLedger::SaveToNvs() const {
  cJSON *root = cJSON_CreateObject();
  if (!root) {
    ESP_LOGW(kTag, "NVS save: cJSON alloc failed");
    return;
  }
  cJSON_AddNumberToObject(root, "c", cash_usdc_);
  cJSON_AddNumberToObject(root, "r", reserve_usdc_);
  cJSON_AddNumberToObject(root, "l", llm_spend_usdc_);
  cJSON_AddNumberToObject(root, "p", realized_paper_pnl_usdc_);
  cJSON_AddNumberToObject(root, "d", daily_loss_usdc_);

  cJSON *arr = cJSON_AddArrayToObject(root, "pos");
  for (const auto &pos : positions_) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "m", pos.market_id.c_str());
    cJSON_AddStringToObject(j, "q", pos.question.c_str());
    cJSON_AddStringToObject(j, "s", pos.side.c_str());
    cJSON_AddNumberToObject(j, "e", pos.entry_price);
    cJSON_AddNumberToObject(j, "k", pos.stake_usdc);
    cJSON_AddNumberToObject(j, "sh", pos.shares);
    cJSON_AddBoolToObject(j, "live", pos.is_live);
    cJSON_AddStringToObject(j, "oid", pos.order_id.c_str());
    cJSON_AddItemToArray(arr, j);
  }

  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json) {
    ESP_LOGW(kTag, "NVS save: cJSON print failed");
    return;
  }

  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) == ESP_OK) {
    size_t len = strlen(json) + 1;
    esp_err_t err = nvs_set_blob(h, kNvsKey, json, len);
    if (err == ESP_OK) {
      nvs_commit(h);
    } else {
      ESP_LOGW(kTag, "NVS set_blob failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
  } else {
    ESP_LOGW(kTag, "NVS open for write failed");
  }
  free(json);
}

bool BudgetLedger::LoadFromNvs() {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return false;

  size_t len = 0;
  esp_err_t err = nvs_get_blob(h, kNvsKey, nullptr, &len);
  if (err != ESP_OK || len == 0) {
    nvs_close(h);
    return false;
  }

  std::vector<char> buf(len);
  err = nvs_get_blob(h, kNvsKey, buf.data(), &len);
  nvs_close(h);
  if (err != ESP_OK) return false;

  cJSON *root = cJSON_Parse(buf.data());
  if (!root) {
    ESP_LOGW(kTag, "NVS load: JSON parse failed");
    return false;
  }

  cJSON *jc = cJSON_GetObjectItem(root, "c");
  cJSON *jr = cJSON_GetObjectItem(root, "r");
  cJSON *jl = cJSON_GetObjectItem(root, "l");
  cJSON *jp = cJSON_GetObjectItem(root, "p");
  cJSON *jd = cJSON_GetObjectItem(root, "d");

  if (!cJSON_IsNumber(jc) || !cJSON_IsNumber(jr)) {
    ESP_LOGW(kTag, "NVS load: missing required fields");
    cJSON_Delete(root);
    return false;
  }

  cash_usdc_ = jc->valuedouble;
  reserve_usdc_ = jr->valuedouble;
  llm_spend_usdc_ = cJSON_IsNumber(jl) ? jl->valuedouble : 0.0;
  realized_paper_pnl_usdc_ = cJSON_IsNumber(jp) ? jp->valuedouble : 0.0;
  daily_loss_usdc_ = cJSON_IsNumber(jd) ? jd->valuedouble : 0.0;

  positions_.clear();
  cJSON *arr = cJSON_GetObjectItem(root, "pos");
  if (cJSON_IsArray(arr)) {
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, arr) {
      Position pos;
      cJSON *v;
      v = cJSON_GetObjectItem(item, "m");
      if (cJSON_IsString(v)) pos.market_id = v->valuestring;
      v = cJSON_GetObjectItem(item, "q");
      if (cJSON_IsString(v)) pos.question = v->valuestring;
      v = cJSON_GetObjectItem(item, "s");
      if (cJSON_IsString(v)) pos.side = v->valuestring;
      v = cJSON_GetObjectItem(item, "e");
      if (cJSON_IsNumber(v)) pos.entry_price = v->valuedouble;
      v = cJSON_GetObjectItem(item, "k");
      if (cJSON_IsNumber(v)) pos.stake_usdc = v->valuedouble;
      v = cJSON_GetObjectItem(item, "sh");
      if (cJSON_IsNumber(v)) pos.shares = v->valuedouble;
      v = cJSON_GetObjectItem(item, "live");
      if (cJSON_IsBool(v)) pos.is_live = cJSON_IsTrue(v);
      v = cJSON_GetObjectItem(item, "oid");
      if (cJSON_IsString(v)) pos.order_id = v->valuestring;
      positions_.push_back(pos);
    }
  }

  cJSON_Delete(root);
  ESP_LOGI(kTag, "Loaded from NVS (%zu bytes, %d positions)",
           len, static_cast<int>(positions_.size()));
  return true;
}

}  // namespace survaiv
