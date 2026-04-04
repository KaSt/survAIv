#include "http.h"

#include "json_util.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace survaiv {

namespace {
constexpr const char *kTag = "survaiv_http";
constexpr size_t kMaxBodySize = 64 * 1024;
// Minimum free heap to maintain while accumulating response body.
// Covers TLS internal buffers (~16 KB) plus headroom for other allocations.
constexpr size_t kMinFreeHeap = 20 * 1024;
}

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
          ESP_LOGW(kTag, "HTTP response exceeds 64 KB — aborting transfer");
          context->truncated = true;
          return ESP_FAIL;
        }
        // When the string must reallocate, verify enough heap remains.
        // Peak usage: new buffer allocated before old buffer is freed.
        if (new_size > context->response->body.capacity()) {
          size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
          size_t new_cap = context->response->body.capacity() * 2;
          if (new_cap < new_size) new_cap = new_size;
          if (free_heap < new_cap + kMinFreeHeap) {
            ESP_LOGW(kTag, "HTTP body stopped at %uB: heap %uB, need %uB+%uB",
                     static_cast<unsigned>(context->response->body.size()),
                     static_cast<unsigned>(free_heap),
                     static_cast<unsigned>(new_cap),
                     static_cast<unsigned>(kMinFreeHeap));
            context->truncated = true;
            return ESP_FAIL;
          }
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
                         const std::string &body) {
  HttpResponse response;
  response.body.reserve(4096);  // Pre-allocate to reduce realloc churn.
  HttpContext context{.response = &response};

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = method;
  config.event_handler = HttpEventHandler;
  config.user_data = &context;
  config.timeout_ms = 30000;
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
    ESP_LOGW(kTag, "Response truncated for %s (%uB kept)",
             url.c_str(), static_cast<unsigned>(response.body.size()));
  } else {
    ESP_LOGE(kTag, "HTTP request to %s failed: %s", url.c_str(), esp_err_to_name(response.err));
  }

  esp_http_client_cleanup(client);
  return response;
}

}  // namespace survaiv
