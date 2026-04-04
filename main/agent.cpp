#include "agent.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>

#include "cJSON.h"
#include "clob.h"
#include "config.h"
#include "dashboard_state.h"
#include "esp_log.h"
#include "http.h"
#include "json_util.h"
#include "model_registry.h"
#include "polymarket.h"
#include "sdkconfig.h"
#include "webserver.h"
#include "x402.h"

namespace survaiv {

namespace {
constexpr const char *kTag = "survaiv_agent";
constexpr int kEstPromptTokens = 2000;
constexpr int kEstCompletionTokens = 500;
constexpr double kSimulatedCostPerRequest = 0.0005;
}

UsageStats ParseUsage(const cJSON *root) {
  UsageStats stats;
  const cJSON *usage = cJSON_GetObjectItemCaseSensitive(root, "usage");
  if (usage == nullptr) {
    return stats;
  }
  stats.prompt_tokens = static_cast<int>(
      JsonToDouble(cJSON_GetObjectItemCaseSensitive(usage, "prompt_tokens")));
  stats.completion_tokens = static_cast<int>(
      JsonToDouble(cJSON_GetObjectItemCaseSensitive(usage, "completion_tokens")));
  return stats;
}

std::string ExtractMessageContent(const cJSON *root) {
  const cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
  if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
    return "";
  }
  const cJSON *choice = cJSON_GetArrayItem(choices, 0);
  const cJSON *message = cJSON_GetObjectItemCaseSensitive(choice, "message");
  if (message == nullptr) {
    return "";
  }
  const cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
  if (cJSON_IsString(content) && content->valuestring != nullptr) {
    return content->valuestring;
  }
  return "";
}

std::string BuildSystemPrompt(bool paper_only, bool geoblocked) {
  std::ostringstream prompt;
  prompt
      << "You are a frugal market-analysis agent running behind an ESP32 controller. "
      << "You do not have direct web access. The ESP32 can perform a tiny set of "
      << "Polymarket HTTP tool calls for you, and every LLM call costs USDC from the same "
      << "survival budget.\n"
      << "Constraints:\n"
      << "1. Preserve capital first. If expected edge is weak or uncertain, hold.\n";

  if (paper_only || geoblocked) {
    prompt
        << "2. Paper trading mode is active. Only use paper_buy_yes, paper_buy_no, paper_close.\n"
        << "3. Never suggest live trading.\n";
  } else {
    prompt
        << "2. LIVE TRADING MODE. You are placing REAL trades with REAL money.\n"
        << "3. Only recommend a live position when confidence >= "
        << (CONFIG_SURVAIV_LIVE_CONFIDENCE_THRESHOLD / 100.0) << " and edge_bps >= "
        << CONFIG_SURVAIV_LIVE_MIN_EDGE_BPS << ".\n"
        << "4. Keep size_fraction <= 0.01. This is real capital — be extremely cautious.\n";
  }

  prompt
      << "5. Prefer zero or one tool call. Tool calls are expensive because they trigger another "
      << "LLM round.\n"
      << "6. Return JSON only. No markdown.\n";

  if (paper_only || geoblocked) {
    prompt
        << "7. Allowed decision types: hold, tool_call, paper_buy_yes, paper_buy_no, paper_close.\n"
        << "8. Only recommend a paper position when confidence >= 0.65 and edge_bps >= 150.\n"
        << "9. Keep size_fraction <= 0.02.\n";
  } else {
    prompt
        << "7. Allowed decision types: hold, tool_call, buy_yes, buy_no, close, "
        << "paper_buy_yes, paper_buy_no, paper_close.\n"
        << "8. Prefer live trades (buy_yes/buy_no/close) when you have conviction. "
        << "Use paper trades only for exploration or low-confidence ideas.\n";
  }

  prompt
      << "10. Allowed tool: search_markets with {\"order\":\"volume_24hr\",\"limit\":N,"
      << "\"offset\":N}. The ESP32 will fetch public Polymarket market data only.\n"
      << "Return one of these JSON shapes exactly:\n"
      << "{\"type\":\"tool_call\",\"tool\":\"search_markets\",\"arguments\":{\"order\":"
      << "\"volume_24hr\",\"limit\":5,\"offset\":0},\"rationale\":\"...\"}\n";

  if (!paper_only && !geoblocked) {
    prompt
        << "{\"type\":\"buy_yes\",\"market_id\":\"...\",\"edge_bps\":210,"
        << "\"confidence\":0.80,\"size_fraction\":0.005,\"rationale\":\"...\"}\n"
        << "{\"type\":\"buy_no\",\"market_id\":\"...\",\"edge_bps\":210,"
        << "\"confidence\":0.80,\"size_fraction\":0.005,\"rationale\":\"...\"}\n"
        << "{\"type\":\"close\",\"market_id\":\"...\",\"edge_bps\":0,"
        << "\"confidence\":0.75,\"size_fraction\":0.0,\"rationale\":\"...\"}\n";
  }

  prompt
      << "{\"type\":\"paper_buy_yes\",\"market_id\":\"...\",\"edge_bps\":210,"
      << "\"confidence\":0.70,\"size_fraction\":0.01,\"rationale\":\"...\"}\n"
      << "{\"type\":\"paper_buy_no\",\"market_id\":\"...\",\"edge_bps\":210,"
      << "\"confidence\":0.70,\"size_fraction\":0.01,\"rationale\":\"...\"}\n"
      << "{\"type\":\"paper_close\",\"market_id\":\"...\",\"edge_bps\":0,"
      << "\"confidence\":0.75,\"size_fraction\":0.0,\"rationale\":\"...\"}\n"
      << "{\"type\":\"hold\",\"market_id\":\"\",\"edge_bps\":0,"
      << "\"confidence\":0.0,\"size_fraction\":0.0,\"rationale\":\"...\"}";
  return prompt.str();
}

std::string BuildUserPrompt(const GeoblockStatus &geoblock, const BudgetLedger &ledger,
                            const std::vector<MarketSnapshot> &markets) {
  std::ostringstream prompt;
  bool paper_only = config::PaperTradingOnly() || geoblock.blocked;
  double estimated_round_cost =
      EstimatedChatCostUsdc(kEstPromptTokens, kEstCompletionTokens);
  double equity = ledger.Equity(ledger.Positions(), markets);
  prompt << "{"
         << "\"paper_trading_only\":" << (paper_only ? "true" : "false")
         << ",\"geoblock\":{"
         << "\"blocked\":" << (geoblock.blocked ? "true" : "false") << ","
         << "\"country\":\"" << JsonEscape(geoblock.country) << "\","
         << "\"region\":\"" << JsonEscape(geoblock.region) << "\"},"
         << "\"budget\":{"
         << "\"cash_usdc\":" << ledger.Cash() << ","
         << "\"reserve_usdc\":" << ledger.Reserve() << ","
         << "\"estimated_llm_round_cost_usdc\":" << estimated_round_cost << ","
         << "\"cumulative_llm_spend_usdc\":" << ledger.LlmSpend() << ","
         << "\"equity_usdc\":" << equity << ","
         << "\"realized_paper_pnl_usdc\":" << ledger.RealizedPaperPnl() << ","
         << "\"daily_loss_usdc\":" << ledger.DailyLossUsdc() << ","
         << "\"daily_loss_limit_usdc\":"
         << (static_cast<double>(config::DailyLossLimitCents()) / 100.0) << "},"
         << "\"open_positions\":" << BuildPositionsJson(ledger.Positions(), markets) << ","
         << "\"market_snapshots\":" << BuildMarketsJson(markets) << "}";
  return prompt.str();
}

bool ChatCompletion(const std::string &system_prompt, const std::string &user_prompt,
                    std::string *content_out, UsageStats *usage_out,
                    const std::string &model_override) {
  if (content_out == nullptr || usage_out == nullptr) {
    return false;
  }

  std::string model = model_override.empty() ? config::OpenaiModel() : model_override;
  std::string base_url = config::OpenaiBaseUrl();
  std::string api_key = config::ApiKey();
  bool use_x402 = x402::IsConfigured();

  bool is_engine_api =
      (base_url.find("x402-gateway") != std::string::npos ||
       base_url.find("x402engine") != std::string::npos);

  std::ostringstream body;
  body << "{";
  if (!is_engine_api) {
    body << "\"model\":\"" << JsonEscape(model) << "\",";
  }
  body << "\"temperature\":0.2,"
       << "\"max_tokens\":" << kEstCompletionTokens << ","
       << "\"messages\":["
       << "{\"role\":\"system\",\"content\":\"" << JsonEscape(system_prompt) << "\"},"
       << "{\"role\":\"user\",\"content\":\"" << JsonEscape(user_prompt) << "\"}"
       << "]}";

  std::vector<std::pair<std::string, std::string>> headers = {
      {"Content-Type", "application/json"}};
  if (!use_x402 && !api_key.empty()) {
    headers.emplace_back("Authorization", "Bearer " + api_key);
  }

  std::string url;
  if (is_engine_api) {
    url = base_url + "/api/llm/" + model;
  } else {
    url = base_url + "/chat/completions";
  }
  HttpResponse response = HttpRequest(url, HTTP_METHOD_POST, headers, body.str());

  // x402: handle 402 Payment Required → sign → retry.
  if (response.status_code == 402 && use_x402) {
    ESP_LOGI(kTag, "x402: received 402, constructing payment...");
    std::string payment = x402::MakePayment(response);
    if (payment.empty()) {
      ESP_LOGE(kTag, "x402 payment construction failed");
      return false;
    }
    headers.emplace_back("X-PAYMENT", payment);
    response = HttpRequest(url, HTTP_METHOD_POST, headers, body.str());

    // Push updated inference spend to dashboard.
    GetDashboardState().SetInferenceSpend(x402::TotalSpentUsdc());
  }

  // Track simulated inference cost in paper mode using real provider pricing.
  if (!use_x402 && config::PaperTradingOnly()) {
    static double s_simulated_spend = 0.0;
    double matched_price = models::LookupPrice(model);
    s_simulated_spend += (matched_price > 0) ? matched_price : kSimulatedCostPerRequest;
    GetDashboardState().SetInferenceSpend(s_simulated_spend);
    if (matched_price > 0) {
      // Find the model name for dashboard display.
      for (int i = 0; i < models::ModelCount(); ++i) {
        const auto &m = models::GetModel(i);
        if (models::CheapestPrice(m) == matched_price) {
          GetDashboardState().SetActiveModel(
              std::string(m.name) + " (sim)", matched_price);
          break;
        }
      }
    }
  }

  if (response.err != ESP_OK || response.status_code < 200 || response.status_code >= 300) {
    ESP_LOGW(kTag, "Chat completion failed: status=%d body=%s", response.status_code,
             response.body.c_str());
    return false;
  }

  cJSON *root = cJSON_Parse(response.body.c_str());
  if (root == nullptr) {
    return false;
  }

  *usage_out = ParseUsage(root);
  *content_out = StripCodeFence(ExtractMessageContent(root));
  cJSON_Delete(root);
  return !content_out->empty();
}

ToolCall ParseToolCall(const std::string &json_text) {
  ToolCall call;
  cJSON *root = cJSON_Parse(json_text.c_str());
  if (root == nullptr) {
    return call;
  }

  std::string type = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "type"));
  std::string tool = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "tool"));
  if (type != "tool_call" || tool != "search_markets") {
    cJSON_Delete(root);
    return call;
  }

  call.valid = true;
  call.tool = tool;
  cJSON *arguments = cJSON_GetObjectItemCaseSensitive(root, "arguments");
  if (arguments != nullptr) {
    std::string order = JsonToString(cJSON_GetObjectItemCaseSensitive(arguments, "order"));
    if (!order.empty()) {
      call.order = order;
    }
    int limit = static_cast<int>(JsonToDouble(cJSON_GetObjectItemCaseSensitive(arguments, "limit")));
    int offset =
        static_cast<int>(JsonToDouble(cJSON_GetObjectItemCaseSensitive(arguments, "offset")));
    if (limit > 0) {
      call.limit = std::min(limit, 12);
    }
    if (offset >= 0) {
      call.offset = offset;
    }
  }

  cJSON_Delete(root);
  return call;
}

Decision ParseDecision(const std::string &json_text) {
  Decision decision;
  cJSON *root = cJSON_Parse(json_text.c_str());
  if (root == nullptr) {
    decision.rationale = "invalid_json";
    return decision;
  }

  decision.type = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "type"));
  decision.market_id = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "market_id"));
  decision.edge_bps = JsonToDouble(cJSON_GetObjectItemCaseSensitive(root, "edge_bps"));
  decision.confidence = JsonToDouble(cJSON_GetObjectItemCaseSensitive(root, "confidence"));
  decision.size_fraction = JsonToDouble(cJSON_GetObjectItemCaseSensitive(root, "size_fraction"));
  decision.rationale = JsonToString(cJSON_GetObjectItemCaseSensitive(root, "rationale"));

  if (decision.type == "paper_buy_yes" || decision.type == "buy_yes") {
    decision.side = "yes";
  } else if (decision.type == "paper_buy_no" || decision.type == "buy_no") {
    decision.side = "no";
  }

  cJSON_Delete(root);
  return decision;
}

void SpendForUsage(BudgetLedger *ledger, const UsageStats &usage) {
  if (ledger == nullptr) {
    return;
  }
  int prompt_tokens = usage.prompt_tokens > 0 ? usage.prompt_tokens : kEstPromptTokens;
  int completion_tokens =
      usage.completion_tokens > 0 ? usage.completion_tokens : kEstCompletionTokens;
  ledger->DebitInference(EstimatedChatCostUsdc(prompt_tokens, completion_tokens));
}

void LogLedgerState(const BudgetLedger &ledger, const std::vector<MarketSnapshot> &markets) {
  ESP_LOGI(kTag,
           "cash=%.4f reserve=%.4f equity=%.4f llm_spend=%.4f realized_pnl=%.4f "
           "daily_loss=%.4f open_positions=%d",
           ledger.Cash(), ledger.Reserve(), ledger.Equity(ledger.Positions(), markets),
           ledger.LlmSpend(), ledger.RealizedPaperPnl(), ledger.DailyLossUsdc(),
           ledger.OpenPositionCount());
}

static bool IsLiveBuyDecision(const std::string &type) {
  return type == "buy_yes" || type == "buy_no";
}

static bool IsPaperBuyDecision(const std::string &type) {
  return type == "paper_buy_yes" || type == "paper_buy_no";
}

// Cooldown tracking: epoch time of last losing close.
static int64_t g_last_loss_epoch = 0;

static bool InCooldown() {
  if (config::CooldownAfterLossSeconds() <= 0 || g_last_loss_epoch == 0) {
    return false;
  }
  time_t now;
  time(&now);
  int64_t elapsed = static_cast<int64_t>(now) - g_last_loss_epoch;
  return elapsed < config::CooldownAfterLossSeconds();
}

void RunAgentCycle(BudgetLedger *ledger) {
  if (ledger == nullptr) {
    return;
  }

  GeoblockStatus geoblock = FetchGeoblockStatus();
  std::vector<MarketSnapshot> markets = FetchMarkets(config::MarketLimit());
  if (markets.empty()) {
    ESP_LOGW(kTag, "No markets fetched this cycle.");
    return;
  }
  LogLedgerState(*ledger, markets);

  // Push state to dashboard.
  auto &dash = GetDashboardState();
  double eq = ledger->Equity(ledger->Positions(), markets);
  dash.UpdateBudget(ledger->Cash(), ledger->Reserve(), eq,
                    ledger->LlmSpend(), ledger->RealizedPaperPnl(),
                    ledger->DailyLossUsdc());
  dash.UpdatePositions(ledger->Positions());
  dash.UpdateMarkets(markets);
  dash.SetGeoblock(geoblock.blocked, geoblock.country);
  dash.SetAgentStatus("running");

  double estimated_cost =
      EstimatedChatCostUsdc(kEstPromptTokens, kEstCompletionTokens);
  if (!ledger->CanSpendOnInference(estimated_cost, ledger->Positions(), markets)) {
    ESP_LOGW(kTag, "Inference reserve reached. cash=%.4f reserve=%.4f",
             ledger->Cash(), ledger->Reserve());
    return;
  }

  bool paper_only = config::PaperTradingOnly() || geoblock.blocked;
  bool daily_loss_exceeded =
      ledger->DailyLossUsdc() >=
      (static_cast<double>(config::DailyLossLimitCents()) / 100.0);

  // Hard stop: if equity is at or below reserve, refuse all new positions.
  double equity_check = ledger->Equity(ledger->Positions(), markets);
  if (equity_check <= ledger->Reserve()) {
    ESP_LOGW(kTag, "HARD STOP: equity (%.4f) <= reserve (%.4f). No new trades.",
             equity_check, ledger->Reserve());
    return;
  }

  std::string system_prompt = BuildSystemPrompt(paper_only, geoblock.blocked);
  std::string user_prompt = BuildUserPrompt(geoblock, *ledger, markets);
  std::string response_text;
  UsageStats usage;

  // Dynamic model selection: pick the best model for the task given remaining
  // budget. Estimate ~96 cycles/day at 15-min intervals. Scale by remaining cash
  // as a fraction of starting bankroll.
  std::string base_url = config::OpenaiBaseUrl();
  bool use_x402 = x402::IsConfigured();
  double cash = ledger->Cash();
  int est_cycles = static_cast<int>(96.0 * (cash / (config::StartingBankrollCents() / 100.0)));
  if (est_cycles < 4) est_cycles = 4;

  // First call: standard analysis (market scan + possible decision).
  std::string model_id;
  if (use_x402) {
    auto sel = models::SelectModel(base_url, models::TaskComplexity::kStandard,
                                   cash, est_cycles);
    if (sel.model) {
      model_id = sel.model_id;
      dash.SetActiveModel(sel.model->name, sel.price);
    }
  }

  if (!ChatCompletion(system_prompt, user_prompt, &response_text, &usage, model_id)) {
    return;
  }
  SpendForUsage(ledger, usage);

  // Handle tool calls.
  ToolCall tool_call = ParseToolCall(response_text);
  if (tool_call.valid && CONFIG_SURVAIV_MAX_TOOL_CALLS_PER_CYCLE > 0) {
    std::vector<MarketSnapshot> tool_markets =
        FetchMarkets(tool_call.limit, tool_call.offset, tool_call.order);
    if (!tool_markets.empty() &&
        ledger->CanSpendOnInference(estimated_cost, ledger->Positions(), tool_markets)) {
      // Follow-up after tool result: may need to make a trade decision,
      // so bump complexity to kComplex.
      std::string follow_model;
      if (use_x402) {
        auto sel2 = models::SelectModel(base_url, models::TaskComplexity::kComplex,
                                        ledger->Cash(), est_cycles);
        if (sel2.model) follow_model = sel2.model_id;
      }
      std::ostringstream follow_up;
      follow_up << user_prompt << "\n"
                << "{\"tool_result\":{\"tool\":\"search_markets\",\"markets\":"
                << BuildMarketsJson(tool_markets) << "}}";
      if (ChatCompletion(system_prompt, follow_up.str(), &response_text, &usage, follow_model)) {
        SpendForUsage(ledger, usage);
        markets = tool_markets;
      }
    }
  }

  Decision decision = ParseDecision(response_text);
  double equity = ledger->Equity(ledger->Positions(), markets);
  double max_position_usdc =
      equity * (static_cast<double>(CONFIG_SURVAIV_MAX_POSITION_BPS) / 10000.0);

  ESP_LOGI(kTag,
           "Decision=%s market=%s edge=%.1f confidence=%.2f size_fraction=%.4f rationale=%s",
           decision.type.c_str(), decision.market_id.c_str(), decision.edge_bps,
           decision.confidence, decision.size_fraction, decision.rationale.c_str());

  // Push decision to dashboard and SSE.
  {
    DecisionRecord rec;
    time_t now;
    time(&now);
    rec.epoch = static_cast<int64_t>(now);
    rec.type = decision.type;
    rec.market_id = decision.market_id;
    rec.side = decision.side;
    rec.confidence = decision.confidence;
    rec.edge_bps = decision.edge_bps;
    rec.size_usdc = equity * decision.size_fraction;
    rec.rationale = decision.rationale;
    // Find market question for display.
    const MarketSnapshot *dm = FindMarket(markets, decision.market_id);
    if (dm) rec.market_question = dm->question;
    dash.PushDecision(rec);
    dash.IncrementCycleCount();
    webserver::PushSseEvent("state", dash.SseStateEvent());
  }

  // Handle close decisions.
  if (decision.type == "paper_close" || decision.type == "close") {
    if (decision.type == "close" && !paper_only && clob::IsReady()) {
      // Find the live position and cancel any open order.
      for (const auto &pos : ledger->Positions()) {
        if (pos.market_id == decision.market_id && pos.is_live && !pos.order_id.empty()) {
          if (clob::CancelOrder(pos.order_id)) {
            ESP_LOGI(kTag, "Cancelled live order %s", pos.order_id.c_str());
          }
        }
      }
    }
    double pnl_before = ledger->RealizedPaperPnl();
    if (ledger->ClosePaperPosition(decision.market_id, markets)) {
      ESP_LOGI(kTag, "Closed position for market %s", decision.market_id.c_str());
      // Track cooldown on losses.
      if (ledger->RealizedPaperPnl() < pnl_before) {
        time_t now;
        time(&now);
        g_last_loss_epoch = static_cast<int64_t>(now);
        ESP_LOGI(kTag, "Loss detected — cooldown for %d seconds",
                 config::CooldownAfterLossSeconds());
      }
    }
    return;
  }

  // Handle live buy decisions.
  if (IsLiveBuyDecision(decision.type)) {
    if (paper_only) {
      ESP_LOGW(kTag, "Live buy rejected: paper-only mode is active.");
      return;
    }
    if (InCooldown()) {
      ESP_LOGW(kTag, "Live buy rejected: in post-loss cooldown.");
      return;
    }
    double live_conf_threshold = config::LiveConfidenceThreshold() / 100.0;
    if (decision.market_id.empty() ||
        decision.confidence < live_conf_threshold ||
        decision.edge_bps < static_cast<double>(config::LiveMinEdgeBps())) {
      ESP_LOGI(kTag, "Live buy rejected: thresholds not met.");
      return;
    }
    if (daily_loss_exceeded) {
      ESP_LOGW(kTag, "Live buy rejected: daily loss limit reached (%.4f USDC).",
               ledger->DailyLossUsdc());
      return;
    }
    if (!clob::IsReady()) {
      ESP_LOGW(kTag, "Live buy rejected: CLOB not authenticated.");
      return;
    }

    const MarketSnapshot *market = FindMarket(markets, decision.market_id);
    if (market == nullptr) {
      ESP_LOGI(kTag, "Market not found in snapshot for live trade.");
      return;
    }

    // Determine token ID and price.
    std::string token_id;
    double price = 0.0;
    int side = 0;  // BUY
    if (decision.type == "buy_yes") {
      token_id = market->clob_token_yes;
      price = market->yes_price;
    } else {
      token_id = market->clob_token_no;
      price = 1.0 - market->yes_price;
    }

    if (token_id.empty() || price <= 0.0 || price >= 1.0) {
      ESP_LOGW(kTag, "Invalid token_id or price for live trade.");
      return;
    }

    // Compute size (number of outcome tokens).
    double size_fraction = std::clamp(decision.size_fraction, 0.0, 0.01);
    double size_usdc = std::min(max_position_usdc, equity * size_fraction);
    size_usdc = std::max(0.0, size_usdc);
    double size_tokens = size_usdc / price;

    if (size_usdc < 0.10) {
      ESP_LOGI(kTag, "Live order too small: %.4f USDC", size_usdc);
      return;
    }

    std::string order_id = clob::PlaceOrder(token_id, side, price, size_tokens);
    if (!order_id.empty()) {
      ESP_LOGI(kTag, "Live %s order placed: %s (%.4f USDC @ %.4f)",
               decision.side.c_str(), order_id.c_str(), size_usdc, price);
      // Track as a paper position for P&L accounting, then mark as live.
      if (ledger->OpenPaperPosition(*market, decision.side, size_usdc)) {
        ledger->MarkPositionLive(decision.market_id, order_id);
      }
    }
    return;
  }

  // Handle paper buy decisions.
  if (!IsPaperBuyDecision(decision.type) || decision.market_id.empty() ||
      decision.confidence < 0.65 || decision.edge_bps < 150.0) {
    return;
  }

  if (ledger->OpenPositionCount() >= config::MaxOpenPositions()) {
    ESP_LOGI(kTag, "Skipping paper trade, max open positions reached.");
    return;
  }

  const MarketSnapshot *market = FindMarket(markets, decision.market_id);
  if (market == nullptr) {
    ESP_LOGI(kTag, "Suggested market not present in current snapshot.");
    return;
  }

  double size_fraction = std::clamp(decision.size_fraction, 0.0, 0.02);
  double size_usdc = std::min(max_position_usdc, equity * size_fraction);
  size_usdc = std::max(0.0, size_usdc);

  if (ledger->OpenPaperPosition(*market, decision.side, size_usdc)) {
    ESP_LOGI(kTag, "Opened paper %s on %s with %.4f USDC", decision.side.c_str(),
             market->question.c_str(), size_usdc);
  }
}

}  // namespace survaiv
