#include <string>

#include "agent.h"
#include "claw402.h"
#include "clob.h"
#include "config.h"
#include "dashboard_state.h"
#include "ledger.h"
#include "model_registry.h"
#include "onboard.h"
#include "wallet.h"
#include "webserver.h"
#include "wifi.h"
#include "wisdom.h"
#include "x402.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace {
constexpr const char *kTag = "survaiv";
}

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Check if we have stored config (from onboarding wizard) or Kconfig.
  bool have_config = survaiv::config::HasStoredConfig() ||
                     (strlen(CONFIG_SURVAIV_WIFI_SSID) > 0);

  if (!have_config) {
    ESP_LOGI(kTag, "No WiFi config found — entering onboarding mode");
    esp_netif_create_default_wifi_ap();
    survaiv::onboard::StartAccessPoint();
    // StartAccessPoint blocks forever (reboots after saving config).
    return;
  }

  ESP_LOGI(kTag, "Config found, booting in normal mode");

  if (!survaiv::ConnectWifi()) {
    ESP_LOGE(kTag, "Wi-Fi connection failed.");
    return;
  }

  // Sync time via SNTP (needed for x402 payment timestamps).
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  // Start the dashboard web server.
  survaiv::webserver::StartDashboard(80);
  ESP_LOGI(kTag, "Dashboard available at http://<device-ip>/");

  // Initialise wallet (required for live trading and x402 payments).
  bool live_capable __attribute__((unused)) = false;
  if (!survaiv::config::PaperTradingOnly() || survaiv::x402::IsConfigured()) {
    if (survaiv::wallet::Init()) {
      ESP_LOGI(kTag, "Wallet: %s", survaiv::wallet::AddressHex().c_str());
      survaiv::GetDashboardState().SetWalletInfo(
          survaiv::wallet::AddressHex(), -1.0);

      // Init x402 payment module if configured.
      if (survaiv::x402::IsConfigured()) {
        survaiv::x402::Init();
        ESP_LOGI(kTag, "x402 provider configured — inference via wallet payments");
      }

      if (!survaiv::config::PaperTradingOnly()) {
        if (survaiv::clob::Init()) {
          live_capable = true;
          ESP_LOGI(kTag, "CLOB authenticated — live trading enabled");
          survaiv::GetDashboardState().SetLiveMode(true);

          double balance = survaiv::wallet::QueryUsdcBalance();
          if (balance >= 0.0) {
            ESP_LOGI(kTag, "USDC.e balance: %.2f", balance);
            survaiv::GetDashboardState().SetWalletInfo(
                survaiv::wallet::AddressHex(), balance);
          }
          survaiv::wallet::EnsureApprovals();
        } else {
          ESP_LOGW(kTag, "CLOB auth failed — falling back to paper trading");
        }
      }
    } else {
      ESP_LOGW(kTag, "No wallet key — paper trading only");
    }
  }

  survaiv::GetDashboardState().SetAgentStatus("running");
  survaiv::wisdom::Init();
  survaiv::claw402::Init();

  // Refresh dynamic model registry from provider catalogs (free, no auth).
  survaiv::models::RefreshRegistry();

  survaiv::BudgetLedger ledger(
      static_cast<double>(survaiv::config::StartingBankrollCents()) / 100.0,
      static_cast<double>(survaiv::config::ReserveCents()) / 100.0);

  int cycle = 0;
  while (true) {
    survaiv::RunAgentCycle(&ledger);

    // Check market outcomes and update wisdom every 4th cycle.
    if (++cycle % 4 == 0) {
      survaiv::wisdom::CheckOutcomes();
      survaiv::wisdom::EvaluateAndUpdateWisdom();
      survaiv::webserver::PushSseEvent("wisdom", survaiv::wisdom::StatsJson());
    }

    // Refresh dynamic model registry every 96 cycles (~24h at 15-min intervals).
    if (cycle % 96 == 0) {
      survaiv::models::RefreshRegistry();
    }

    vTaskDelay(pdMS_TO_TICKS(survaiv::config::LoopSeconds() * 1000));
  }
}
