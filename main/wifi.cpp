#include "wifi.h"

#include <cstdio>
#include <string>

#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace survaiv {

namespace {

constexpr const char *kTag = "survaiv_wifi";
constexpr int kWifiConnectedBit = BIT0;
constexpr int kWifiFailBit = BIT1;
constexpr int kWifiMaxRetries = 10;

EventGroupHandle_t s_wifi_event_group = nullptr;
int s_wifi_retry_count = 0;

void WifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id,
                      void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
    return;
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_wifi_retry_count < kWifiMaxRetries) {
      esp_wifi_connect();
      s_wifi_retry_count++;
      return;
    }
    xEventGroupSetBits(s_wifi_event_group, kWifiFailBit);
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    s_wifi_retry_count = 0;
    xEventGroupSetBits(s_wifi_event_group, kWifiConnectedBit);
  }
}

}  // namespace

bool ConnectWifi() {
  s_wifi_event_group = xEventGroupCreate();
  if (s_wifi_event_group == nullptr) {
    return false;
  }

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                      &WifiEventHandler, nullptr, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                      &WifiEventHandler, nullptr,
                                                      &instance_got_ip));

  wifi_config_t wifi_config = {};
  std::string ssid = config::WifiSsid();
  std::string pass = config::WifiPassword();
  std::snprintf(reinterpret_cast<char *>(wifi_config.sta.ssid), sizeof(wifi_config.sta.ssid),
                "%s", ssid.c_str());
  std::snprintf(reinterpret_cast<char *>(wifi_config.sta.password),
                sizeof(wifi_config.sta.password), "%s", pass.c_str());
  wifi_config.sta.threshold.authmode = pass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         kWifiConnectedBit | kWifiFailBit,
                                         pdFALSE, pdFALSE, portMAX_DELAY);
  bool connected = (bits & kWifiConnectedBit) != 0;

  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        instance_got_ip));
  ESP_ERROR_CHECK(
      esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
  vEventGroupDelete(s_wifi_event_group);
  s_wifi_event_group = nullptr;

  return connected;
}

}  // namespace survaiv
