#include "http.h"

#include "json_util.h"

#include "esp_crt_bundle.h"
#include "esp_log.h"

namespace survaiv {

namespace {
constexpr const char *kTag = "survaiv_http";
}

static esp_err_t HttpEventHandler(esp_http_client_event_t *event) {
  auto *context = static_cast<HttpContext *>(event->user_data);
  if (context == nullptr || context->response == nullptr) {
    return ESP_OK;
  }

  switch (event->event_id) {
    case HTTP_EVENT_ON_HEADER:
      if (event->header_key != nullptr && event->header_value != nullptr) {
        context->response->headers[ToLower(event->header_key)] = event->header_value;
      }
      break;
    case HTTP_EVENT_ON_DATA:
      if (event->data != nullptr && event->data_len > 0) {
        context->response->body.append(static_cast<const char *>(event->data), event->data_len);
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
  } else {
    ESP_LOGE(kTag, "HTTP request to %s failed: %s", url.c_str(), esp_err_to_name(response.err));
  }

  esp_http_client_cleanup(client);
  return response;
}

}  // namespace survaiv
