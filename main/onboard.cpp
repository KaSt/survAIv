#include "onboard.h"

#include <cstdio>
#include <cstring>

#include "webserver.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"

namespace survaiv {
namespace onboard {

namespace {
constexpr const char *kTag = "survaiv_onboard";

// DNS server task — redirects all DNS queries to our AP IP (captive portal).
void DnsServerTask(void *) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(kTag, "DNS socket failed");
    vTaskDelete(nullptr);
    return;
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(53);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    ESP_LOGE(kTag, "DNS bind failed");
    close(sock);
    vTaskDelete(nullptr);
    return;
  }

  ESP_LOGI(kTag, "Captive DNS server started");

  uint8_t buf[512];
  while (true) {
    struct sockaddr_in client = {};
    socklen_t client_len = sizeof(client);
    int len = recvfrom(sock, buf, sizeof(buf), 0,
                       reinterpret_cast<struct sockaddr *>(&client), &client_len);
    if (len < 12) continue;

    // Build a minimal DNS response: copy the query, set response flags,
    // and append an A record pointing to 192.168.4.1.
    uint8_t resp[512];
    if (len > 480) continue;  // too large
    memcpy(resp, buf, len);

    // Set flags: response, authoritative, no error.
    resp[2] = 0x81;
    resp[3] = 0x80;
    // Answer count = 1.
    resp[6] = 0x00;
    resp[7] = 0x01;

    // Append answer: pointer to name in question (0xC00C), type A, class IN,
    // TTL 60, data length 4, IP 192.168.4.1.
    int pos = len;
    resp[pos++] = 0xC0;  // name pointer
    resp[pos++] = 0x0C;
    resp[pos++] = 0x00; resp[pos++] = 0x01;  // type A
    resp[pos++] = 0x00; resp[pos++] = 0x01;  // class IN
    resp[pos++] = 0x00; resp[pos++] = 0x00; resp[pos++] = 0x00; resp[pos++] = 0x3C;  // TTL 60
    resp[pos++] = 0x00; resp[pos++] = 0x04;  // data length
    resp[pos++] = 192;
    resp[pos++] = 168;
    resp[pos++] = 4;
    resp[pos++] = 1;

    sendto(sock, resp, pos, 0,
           reinterpret_cast<struct sockaddr *>(&client), client_len);
  }
}

}  // namespace

void StartAccessPoint() {
  ESP_LOGI(kTag, "Starting AP mode for onboarding...");

  // Generate AP SSID with last 4 hex of MAC.
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_AP, mac);
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "survaiv-%02X%02X", mac[4], mac[5]);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

  wifi_config_t ap_config = {};
  strncpy(reinterpret_cast<char *>(ap_config.ap.ssid), ssid, sizeof(ap_config.ap.ssid));
  ap_config.ap.ssid_len = strlen(ssid);
  ap_config.ap.channel = 1;
  ap_config.ap.authmode = WIFI_AUTH_OPEN;
  ap_config.ap.max_connection = 4;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(kTag, "AP started: %s (open, no password)", ssid);
  ESP_LOGI(kTag, "Connect and visit http://192.168.4.1/");

  // Start captive DNS server.
  xTaskCreate(DnsServerTask, "dns_srv", 4096, nullptr, 5, nullptr);

  // Start onboarding web server.
  webserver::StartOnboarding(80);

  // Block forever — reboot happens when user saves config via /api/save.
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

}  // namespace onboard
}  // namespace survaiv
