#include "http.h"

#include <cstring>

#include "json_util.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

namespace survaiv {

namespace {
constexpr const char *kTag = "survaiv_http";
#if CONFIG_SPIRAM
constexpr size_t kMaxBodySize = 512 * 1024;   // boards with PSRAM
constexpr size_t kMinFreeHeap = 40 * 1024;
// With SPIRAM_USE_CAPS_ALLOC, standard malloc/new only uses internal SRAM.
// Heap checks must use MALLOC_CAP_INTERNAL to avoid counting unusable PSRAM.
constexpr uint32_t kHeapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#else
// C3: only ~230KB SRAM total, ~148KB free after WiFi+TLS.
// Keep body cap low so we never exhaust contiguous heap.
constexpr size_t kMaxBodySize = 48 * 1024;
constexpr size_t kMinFreeHeap = 20 * 1024;
constexpr uint32_t kHeapCaps = MALLOC_CAP_8BIT;  // all heap is internal
#endif

// ─── Lightweight mDNS A-record resolver ─────────────────────────
// Sends an mDNS query for hostname.local and listens for the unicast
// or multicast response on port 5353.  Returns resolved IPv4 as string
// or empty string on failure.  ~1 KB stack, no heap allocation.

static std::string MdnsResolve(const char *hostname) {
  // Build mDNS query packet for A record.
  // mDNS wire format: header (12) + QNAME + QTYPE(2) + QCLASS(2)
  uint8_t pkt[256];
  memset(pkt, 0, sizeof(pkt));

  // Transaction ID = 0 for mDNS
  // Flags: standard query
  // QDCOUNT = 1
  pkt[4] = 0; pkt[5] = 1;  // qdcount = 1

  // Encode QNAME: split hostname by '.', then append labels.
  size_t pos = 12;
  const char *p = hostname;
  while (*p) {
    const char *dot = strchr(p, '.');
    size_t len = dot ? static_cast<size_t>(dot - p) : strlen(p);
    if (len > 63 || pos + len + 1 >= sizeof(pkt) - 10) return "";
    pkt[pos++] = static_cast<uint8_t>(len);
    memcpy(&pkt[pos], p, len);
    pos += len;
    p += len + (dot ? 1 : 0);
  }
  // Append ".local" labels
  pkt[pos++] = 5; memcpy(&pkt[pos], "local", 5); pos += 5;
  pkt[pos++] = 0;  // root label

  // QTYPE = A (1), QCLASS = IN (1) with unicast-response bit set (0x8001)
  pkt[pos++] = 0; pkt[pos++] = 1;    // type A
  pkt[pos++] = 0x80; pkt[pos++] = 1; // class IN + unicast-response

  size_t pkt_len = pos;

  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) return "";

  // Bind to port 5353 to receive mDNS responses (both unicast and multicast).
  struct sockaddr_in local = {};
  local.sin_family = AF_INET;
  local.sin_port = htons(5353);
  local.sin_addr.s_addr = INADDR_ANY;
  if (bind(sock, reinterpret_cast<struct sockaddr *>(&local), sizeof(local)) < 0) {
    close(sock);
    return "";
  }

  // Join mDNS multicast group to receive multicast responses.
  struct ip_mreq mreq = {};
  mreq.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
  mreq.imr_interface.s_addr = INADDR_ANY;
  setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

  // Send query to mDNS multicast address.
  struct sockaddr_in dest = {};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(5353);
  dest.sin_addr.s_addr = inet_addr("224.0.0.251");
  sendto(sock, pkt, pkt_len, 0,
         reinterpret_cast<struct sockaddr *>(&dest), sizeof(dest));

  // Wait for response (up to 2 seconds, retry once).
  struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  std::string result;
  for (int attempt = 0; attempt < 2 && result.empty(); ++attempt) {
    if (attempt == 1) {
      // Resend query on retry.
      sendto(sock, pkt, pkt_len, 0,
             reinterpret_cast<struct sockaddr *>(&dest), sizeof(dest));
    }

    uint8_t resp[512];
    int n = recv(sock, resp, sizeof(resp), 0);
    if (n < 12) continue;

    // Check it's a response (QR bit set) with at least one answer.
    if (!(resp[2] & 0x80)) continue;
    int ancount = (resp[6] << 8) | resp[7];
    if (ancount == 0) continue;

    // Skip header (12 bytes) + question section.
    size_t off = 12;
    int qdcount = (resp[4] << 8) | resp[5];
    for (int q = 0; q < qdcount && off < static_cast<size_t>(n); ++q) {
      // Skip QNAME
      while (off < static_cast<size_t>(n)) {
        if (resp[off] == 0) { off++; break; }
        if ((resp[off] & 0xC0) == 0xC0) { off += 2; break; }
        off += 1 + resp[off];
      }
      off += 4;  // QTYPE + QCLASS
    }

    // Parse answer records looking for type A.
    for (int a = 0; a < ancount && off + 10 < static_cast<size_t>(n); ++a) {
      // Skip NAME (may be pointer)
      while (off < static_cast<size_t>(n)) {
        if (resp[off] == 0) { off++; break; }
        if ((resp[off] & 0xC0) == 0xC0) { off += 2; break; }
        off += 1 + resp[off];
      }
      if (off + 10 > static_cast<size_t>(n)) break;

      uint16_t rtype = (resp[off] << 8) | resp[off + 1];
      uint16_t rdlen = (resp[off + 8] << 8) | resp[off + 9];
      off += 10;

      if (rtype == 1 && rdlen == 4 && off + 4 <= static_cast<size_t>(n)) {
        // A record — 4-byte IPv4
        char ip[16];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                 resp[off], resp[off + 1], resp[off + 2], resp[off + 3]);
        result = ip;
        break;
      }
      off += rdlen;
    }
  }

  // Leave multicast group and close.
  setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
  close(sock);

  if (!result.empty()) {
    ESP_LOGI(kTag, "mDNS resolved %s.local → %s", hostname, result.c_str());
  } else {
    ESP_LOGW(kTag, "mDNS failed to resolve %s.local", hostname);
  }
  return result;
}

// Replace .local hostname in URL with resolved IP address.
// E.g. "http://kamini.local:8080/v1/…" → "http://192.168.1.42:8080/v1/…"
static std::string ResolveLocalUrl(const std::string &url) {
  // Find hostname start (after "://")
  size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) return url;
  size_t host_start = scheme_end + 3;

  // Find hostname end (before ':' port or '/' path)
  size_t host_end = url.find_first_of(":/?#", host_start);
  if (host_end == std::string::npos) host_end = url.size();

  std::string host = url.substr(host_start, host_end - host_start);

  // Check if it ends with ".local"
  if (host.size() < 7 || host.compare(host.size() - 6, 6, ".local") != 0) {
    return url;
  }

  // Strip ".local" suffix for the query — mDNS encodes labels separately.
  std::string name = host.substr(0, host.size() - 6);
  std::string ip = MdnsResolve(name.c_str());
  if (ip.empty()) return url;

  return url.substr(0, host_start) + ip + url.substr(host_end);
}

}  // namespace

static esp_err_t HttpEventHandler(esp_http_client_event_t *event) {
  auto *context = static_cast<HttpContext *>(event->user_data);
  if (context == nullptr || context->response == nullptr) {
    return ESP_OK;
  }

  // If we already decided to stop accumulating, abort the transfer so
  // esp_http_client doesn't keep draining TLS data (which allocates memory).
  if (context->truncated) {
    return ESP_FAIL;
  }

  switch (event->event_id) {
    case HTTP_EVENT_ON_HEADER:
      if (event->header_key != nullptr && event->header_value != nullptr) {
        context->response->headers[ToLower(event->header_key)] = event->header_value;
      }
      break;
    case HTTP_EVENT_ON_DATA:
      if (event->data != nullptr && event->data_len > 0) {
        size_t new_size = context->response->body.size() + event->data_len;
        if (new_size > kMaxBodySize) {
          ESP_LOGW(kTag, "HTTP response exceeds max body — aborting");
          context->truncated = true;
          return ESP_FAIL;
        }
        // When the string must reallocate, verify enough contiguous heap
        // and pre-reserve so std::string doesn't pick its own capacity.
        if (new_size > context->response->body.capacity()) {
          size_t largest_block =
              heap_caps_get_largest_free_block(kHeapCaps);
          size_t free_heap = heap_caps_get_free_size(kHeapCaps);
          // reserve() needs new_cap bytes contiguous, plus the old buffer
          // stays alive until the copy is done → need new_cap + old_cap.
          size_t old_cap = context->response->body.capacity();
          size_t new_cap = new_size + new_size / 4;
          if (new_cap > kMaxBodySize) new_cap = kMaxBodySize;
          size_t peak = new_cap + old_cap;
          if (largest_block < peak + kMinFreeHeap ||
              free_heap < peak + kMinFreeHeap) {
            // Try exact fit as last resort.
            new_cap = new_size;
            peak = new_cap + old_cap;
            if (largest_block < peak + kMinFreeHeap ||
                free_heap < peak + kMinFreeHeap) {
              ESP_LOGW(kTag,
                       "HTTP body stopped at %uB: largest_block %uB, "
                       "need %uB+%uB",
                       static_cast<unsigned>(context->response->body.size()),
                       static_cast<unsigned>(largest_block),
                       static_cast<unsigned>(peak),
                       static_cast<unsigned>(kMinFreeHeap));
              context->truncated = true;
              return ESP_FAIL;
            }
          }
          context->response->body.reserve(new_cap);
        }
        context->response->body.append(
            static_cast<const char *>(event->data), event->data_len);
      }
      break;
    default:
      break;
  }

  return ESP_OK;
}

HttpResponse HttpRequest(const std::string &url, esp_http_client_method_t method,
                         const std::vector<std::pair<std::string, std::string>> &headers,
                         const std::string &body, int timeout_ms) {
  HttpResponse response;
  response.body.reserve(4096);  // Pre-allocate to reduce realloc churn.
  HttpContext context{.response = &response};

  // Resolve .local hostnames via mDNS before connecting.
  std::string resolved_url = ResolveLocalUrl(url);

  esp_http_client_config_t config = {};
  config.url = resolved_url.c_str();
  config.method = method;
  config.event_handler = HttpEventHandler;
  config.user_data = &context;
  config.timeout_ms = timeout_ms;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    response.err = ESP_FAIL;
    return response;
  }

  for (const auto &header : headers) {
    esp_http_client_set_header(client, header.first.c_str(), header.second.c_str());
  }

  if (!body.empty()) {
    esp_http_client_set_post_field(client, body.c_str(), static_cast<int>(body.size()));
  }

  response.err = esp_http_client_perform(client);
  if (response.err == ESP_OK) {
    response.status_code = esp_http_client_get_status_code(client);
  } else if (context.truncated) {
    // We intentionally aborted the transfer due to memory pressure.
    // The body we have is partial but the HTTP status may still be valid.
    int code = esp_http_client_get_status_code(client);
    response.status_code = code > 0 ? code : 200;
    response.err = ESP_OK;
    response.truncated = true;
    ESP_LOGW(kTag, "Response truncated for %s (%uB kept)",
             url.c_str(), static_cast<unsigned>(response.body.size()));
  } else {
    ESP_LOGE(kTag, "HTTP request to %s failed: %s", url.c_str(), esp_err_to_name(response.err));
  }

  esp_http_client_cleanup(client);
  return response;
}

}  // namespace survaiv
