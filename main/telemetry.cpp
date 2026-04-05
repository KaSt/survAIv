#include "telemetry.h"

#include "config.h"
#include "http.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string>

static const char *TAG = "survaiv_telem";

namespace survaiv {
namespace telemetry {

static DashboardState *s_state = nullptr;

static void SendReport() {
  std::string url = config::TelemetryUrl();
  if (url.empty()) return;

  if (url.back() == '/') url.pop_back();
  url += "/api/report";

  // Build the combined JSON payload by wrapping existing JSON methods.
  std::string state_json = s_state->ToJson();
  std::string positions_json = s_state->PositionsJson();
  std::string decisions_json = s_state->DecisionHistoryJson();
  std::string equity_json = s_state->EquityHistoryJson();

  // Remove trailing '}' from state_json and append additional arrays.
  if (!state_json.empty() && state_json.back() == '}') state_json.pop_back();

  std::string payload;
  payload.reserve(state_json.size() + positions_json.size() +
                  decisions_json.size() + equity_json.size() + 64);
  payload += state_json;
  payload += ",\"positions\":";
  payload += positions_json;
  payload += ",\"decisions\":";
  payload += decisions_json;
  payload += ",\"equity_history\":";
  payload += equity_json;
  payload += "}";

  auto resp = HttpRequest(
      url, HTTP_METHOD_POST,
      {{"Content-Type", "application/json"}},
      payload, 10000);

  if (resp.err != ESP_OK) {
    ESP_LOGW(TAG, "Telemetry POST failed: %s", esp_err_to_name(resp.err));
  } else if (resp.status_code < 200 || resp.status_code >= 300) {
    ESP_LOGW(TAG, "Telemetry POST: HTTP %d", resp.status_code);
  } else {
    ESP_LOGI(TAG, "Telemetry report sent (%d bytes)",
             static_cast<int>(payload.size()));
  }
}

static void TelemetryTask(void *) {
  // Wait 30s after boot before first report.
  vTaskDelay(pdMS_TO_TICKS(30000));

  for (;;) {
    SendReport();
    int interval = config::TelemetryIntervalSec();
    if (interval < 60) interval = 60;
    vTaskDelay(pdMS_TO_TICKS(interval * 1000));
  }
}

void Init(DashboardState *state) {
  s_state = state;
  xTaskCreate(TelemetryTask, "telemetry", 6144, nullptr, 3, nullptr);
  ESP_LOGI(TAG, "Telemetry task started");
}

}  // namespace telemetry
}  // namespace survaiv
