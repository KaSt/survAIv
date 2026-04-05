#include "mdns_adv.h"

#include <cstring>
#include <string>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/igmp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "esp_netif.h"

static const char *kTag = "survaiv_mdns";

namespace survaiv {
namespace mdns_adv {

static std::string g_hostname;
static int g_http_port = 80;

// Encode a DNS name into wire format: "survaiv" → \x07survaiv\x05local\x00
static int EncodeName(const char *hostname, uint8_t *buf, int max) {
  int pos = 0;
  // First label: the hostname.
  int len = strlen(hostname);
  if (len > 63 || pos + 1 + len > max) return -1;
  buf[pos++] = (uint8_t)len;
  memcpy(&buf[pos], hostname, len);
  pos += len;
  // Second label: "local".
  if (pos + 7 > max) return -1;
  buf[pos++] = 5;
  memcpy(&buf[pos], "local", 5);
  pos += 5;
  // Terminator.
  buf[pos++] = 0;
  return pos;
}

// Build an mDNS A-record response for our hostname.
static int BuildResponse(uint16_t txn_id, uint8_t *buf, int max) {
  if (max < 128) return -1;
  int pos = 0;

  // Header: txn_id, flags=0x8400 (response, authoritative), 1 answer.
  buf[pos++] = txn_id >> 8; buf[pos++] = txn_id & 0xFF;
  buf[pos++] = 0x84; buf[pos++] = 0x00;  // QR=1, AA=1
  buf[pos++] = 0; buf[pos++] = 0;        // QDCOUNT=0
  buf[pos++] = 0; buf[pos++] = 1;        // ANCOUNT=1
  buf[pos++] = 0; buf[pos++] = 0;        // NSCOUNT=0
  buf[pos++] = 0; buf[pos++] = 0;        // ARCOUNT=0

  // Answer: NAME
  int name_len = EncodeName(g_hostname.c_str(), &buf[pos], max - pos);
  if (name_len < 0) return -1;
  pos += name_len;

  // TYPE=A (1), CLASS=IN (1) with cache-flush bit.
  buf[pos++] = 0; buf[pos++] = 1;        // TYPE A
  buf[pos++] = 0x80; buf[pos++] = 1;     // CLASS IN + cache-flush

  // TTL = 120 seconds.
  buf[pos++] = 0; buf[pos++] = 0; buf[pos++] = 0; buf[pos++] = 120;

  // RDLENGTH = 4 (IPv4).
  buf[pos++] = 0; buf[pos++] = 4;

  // RDATA = our IP address.
  esp_netif_ip_info_t ip_info;
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!netif) netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return -1;
  uint32_t ip = ip_info.ip.addr;
  buf[pos++] = (ip >> 0) & 0xFF;
  buf[pos++] = (ip >> 8) & 0xFF;
  buf[pos++] = (ip >> 16) & 0xFF;
  buf[pos++] = (ip >> 24) & 0xFF;

  return pos;
}

// Check if an incoming mDNS query is asking for our hostname.
static bool IsQueryForUs(const uint8_t *data, int len) {
  if (len < 17) return false;  // Minimum: 12 header + some question

  // Check QR bit = 0 (query) and QDCOUNT >= 1.
  if (data[2] & 0x80) return false;  // It's a response, not a query.
  int qdcount = (data[4] << 8) | data[5];
  if (qdcount < 1) return false;

  // Parse first question name starting at offset 12.
  int pos = 12;
  std::string qname;
  while (pos < len) {
    uint8_t lbl_len = data[pos++];
    if (lbl_len == 0) break;
    if (pos + lbl_len > len) return false;
    if (!qname.empty()) qname += '.';
    qname.append((const char *)&data[pos], lbl_len);
    pos += lbl_len;
  }

  // Check QTYPE=A (1) or ANY (255), QCLASS=IN (1).
  if (pos + 4 > len) return false;
  uint16_t qtype = (data[pos] << 8) | data[pos + 1];
  // Accept A(1), AAAA(28), or ANY(255) — we only respond with A.
  if (qtype != 1 && qtype != 255) return false;

  // Case-insensitive compare: "<hostname>.local"
  std::string expected = g_hostname + ".local";
  if (qname.size() != expected.size()) return false;
  for (size_t i = 0; i < qname.size(); i++) {
    if (tolower(qname[i]) != tolower(expected[i])) return false;
  }
  return true;
}

static void MdnsTask(void *) {
  // Wait for network to be ready.
  vTaskDelay(pdMS_TO_TICKS(5000));

  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(kTag, "Failed to create mDNS socket");
    vTaskDelete(nullptr);
    return;
  }

  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in bind_addr = {};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(5353);
  bind_addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
    ESP_LOGE(kTag, "Failed to bind mDNS socket");
    close(sock);
    vTaskDelete(nullptr);
    return;
  }

  // Join mDNS multicast group 224.0.0.251.
  struct ip_mreq mreq = {};
  mreq.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
  mreq.imr_interface.s_addr = INADDR_ANY;
  setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

  ESP_LOGI(kTag, "Advertising %s.local (HTTP port %d)", g_hostname.c_str(), g_http_port);

  uint8_t buf[512];
  while (true) {
    struct sockaddr_in src = {};
    socklen_t src_len = sizeof(src);
    int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &src_len);
    if (n <= 0) continue;

    if (!IsQueryForUs(buf, n)) continue;

    uint16_t txn_id = (buf[0] << 8) | buf[1];
    uint8_t resp[256];
    int resp_len = BuildResponse(txn_id, resp, sizeof(resp));
    if (resp_len <= 0) continue;

    // Send multicast response.
    struct sockaddr_in mcast = {};
    mcast.sin_family = AF_INET;
    mcast.sin_port = htons(5353);
    mcast.sin_addr.s_addr = inet_addr("224.0.0.251");
    sendto(sock, resp, resp_len, 0, (struct sockaddr *)&mcast, sizeof(mcast));
  }
}

void Start(const char *hostname, int http_port) {
  if (hostname && hostname[0]) {
    g_hostname = hostname;
  } else {
    g_hostname = "survaiv";
  }

  // Sanitize: lowercase, replace spaces/underscores with hyphens, max 63 chars.
  for (auto &c : g_hostname) {
    if (c == ' ' || c == '_') c = '-';
    else c = tolower(c);
  }
  if (g_hostname.size() > 63) g_hostname.resize(63);

  g_http_port = http_port;

  xTaskCreate(MdnsTask, "mdns_adv", 3072, nullptr, 2, nullptr);
}

}  // namespace mdns_adv
}  // namespace survaiv
