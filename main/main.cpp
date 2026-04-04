#include <string>

#include "agent.h"
#include "claw402.h"
#include "clob.h"
#include "config.h"
#include "dashboard_state.h"
#include "ledger.h"
#include "model_registry.h"
#include "onboard.h"
#include "provider.h"
#include "wallet.h"
#include "webserver.h"
#include "wifi.h"
#include "wisdom.h"
#include "x402.h"

#include "esp_app_desc.h"
#include "esp_chip_info.h"
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
  // ── Startup banner ──────────────────────────────────────────────
  const esp_app_desc_t *app = esp_app_get_description();

  esp_chip_info_t chip;
  esp_chip_info(&chip);
  const char *chip_name = "ESP32";
  switch (chip.model) {
    case CHIP_ESP32C3: chip_name = "ESP32-C3"; break;
    case CHIP_ESP32S3: chip_name = "ESP32-S3"; break;
    default: break;
  }

  ESP_LOGI(kTag,
    "╔═══════════════════════════════════╗\n"
    "║        ⟁ SURVAIV starting         ║\n"
    "╚═══════════════════════════════════╝");
  ESP_LOGI(kTag, "Firmware : %s (%s)", app->version, app->date);
  ESP_LOGI(kTag, "IDF      : %s", app->idf_ver);
  ESP_LOGI(kTag, "Chip     : %s rev %d, %d core(s)",
           chip_name, chip.revision, chip.cores);
#if CONFIG_SURVAIV_ENABLE_OTA
  ESP_LOGI(kTag, "OTA      : enabled (dual-partition)");
#else
  ESP_LOGI(kTag, "OTA      : disabled (single factory partition)");
#endif
  ESP_LOGI(kTag, "Free heap: %lu bytes",
           (unsigned long)esp_get_free_heap_size());

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

  // Register provider adapters (must happen before x402::IsConfigured).
  survaiv::providers::Init();

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

  survaiv::GetDashboardState().SetResetPaperFunc([&ledger]() {
    ledger.ResetPaper(
        static_cast<double>(survaiv::config::StartingBankrollCents()) / 100.0,
        static_cast<double>(survaiv::config::ReserveCents()) / 100.0);
    ESP_LOGI("main", "Paper trading reset by user");
  });

  int cycle = 0;
  while (true) {
    int retry_delay = survaiv::RunAgentCycle(&ledger);

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

    // Early retry on LLM failure, otherwise wait the normal loop interval.
    int delay_sec = retry_delay > 0 ? retry_delay
                                    : survaiv::config::LoopSeconds();

    // Tell dashboard when next cycle fires.
    time_t now;
    time(&now);
    survaiv::GetDashboardState().SetNextCycleEpoch(
        static_cast<int64_t>(now) + delay_sec);

    vTaskDelay(pdMS_TO_TICKS(delay_sec * 1000));
  }
}
