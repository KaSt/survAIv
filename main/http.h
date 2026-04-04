#pragma once

#include <string>
#include <utility>
#include <vector>

#include "types.h"

#include "esp_http_client.h"

namespace survaiv {

HttpResponse HttpRequest(const std::string &url, esp_http_client_method_t method,
                         const std::vector<std::pair<std::string, std::string>> &headers,
                         const std::string &body = "");

}  // namespace survaiv
