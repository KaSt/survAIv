#include "json_util.h"

#include <algorithm>
#include <cstdlib>

#include "sdkconfig.h"

namespace survaiv {

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

std::string JsonEscape(const std::string &text) {
  std::string escaped;
  escaped.reserve(text.size() + 16);
  for (char ch : text) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

std::string StripCodeFence(std::string text) {
  text.erase(0, text.find_first_not_of(" \n\r\t"));
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
  }
  if (text.rfind("```", 0) != 0) {
    return text;
  }

  size_t first_newline = text.find('\n');
  if (first_newline == std::string::npos) {
    return text;
  }

  size_t last_fence = text.rfind("```");
  if (last_fence == std::string::npos || last_fence <= first_newline) {
    return text;
  }
  return text.substr(first_newline + 1, last_fence - first_newline - 1);
}

std::string ExtractFirstJsonObject(const std::string &text) {
  // Fast path: already starts with '{'.
  if (!text.empty() && text.front() == '{') {
    cJSON *test = cJSON_Parse(text.c_str());
    if (test) { cJSON_Delete(test); return text; }
  }

  // Scan for each '{', extract balanced block, validate it's real JSON with "type".
  size_t search_from = 0;
  while (search_from < text.size()) {
    size_t start = text.find('{', search_from);
    if (start == std::string::npos) break;

    int depth = 0;
    bool in_string = false;
    bool escape = false;
    size_t end = std::string::npos;
    for (size_t i = start; i < text.size(); ++i) {
      char ch = text[i];
      if (escape) { escape = false; continue; }
      if (ch == '\\' && in_string) { escape = true; continue; }
      if (ch == '"') { in_string = !in_string; continue; }
      if (in_string) continue;
      if (ch == '{') ++depth;
      else if (ch == '}') {
        --depth;
        if (depth == 0) { end = i; break; }
      }
    }

    if (end != std::string::npos) {
      std::string candidate = text.substr(start, end - start + 1);
      cJSON *parsed = cJSON_Parse(candidate.c_str());
      if (parsed) {
        // Accept if it has a "type" field (decision/tool_call).
        cJSON *type_field = cJSON_GetObjectItemCaseSensitive(parsed, "type");
        if (type_field && cJSON_IsString(type_field)) {
          cJSON_Delete(parsed);
          return candidate;
        }
        cJSON_Delete(parsed);
      }
    }
    search_from = start + 1;
  }

  // Last resort: return first balanced block even without "type".
  size_t start = text.find('{');
  if (start == std::string::npos) return text;
  int depth = 0;
  bool in_string = false;
  bool escape = false;
  for (size_t i = start; i < text.size(); ++i) {
    char ch = text[i];
    if (escape) { escape = false; continue; }
    if (ch == '\\' && in_string) { escape = true; continue; }
    if (ch == '"') { in_string = !in_string; continue; }
    if (in_string) continue;
    if (ch == '{') ++depth;
    else if (ch == '}') {
      --depth;
      if (depth == 0) return text.substr(start, i - start + 1);
    }
  }
  return text.substr(start);
}

double JsonToDouble(const cJSON *item) {
  if (item == nullptr) {
    return 0.0;
  }
  if (cJSON_IsNumber(item)) {
    return item->valuedouble;
  }
  if (cJSON_IsString(item) && item->valuestring != nullptr) {
    return std::strtod(item->valuestring, nullptr);
  }
  return 0.0;
}

std::string JsonToString(const cJSON *item) {
  if (item == nullptr) {
    return "";
  }
  if (cJSON_IsString(item) && item->valuestring != nullptr) {
    return item->valuestring;
  }
  if (cJSON_IsNumber(item)) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6f", item->valuedouble);
    return buffer;
  }
  return "";
}

std::vector<double> ParseStringifiedArrayToDoubles(const cJSON *item) {
  std::vector<double> values;
  if (item == nullptr) {
    return values;
  }

  cJSON *array = nullptr;
  if (cJSON_IsArray(item)) {
    array = cJSON_Duplicate(item, 1);
  } else if (cJSON_IsString(item) && item->valuestring != nullptr) {
    array = cJSON_Parse(item->valuestring);
  }

  if (array == nullptr || !cJSON_IsArray(array)) {
    if (array != nullptr) {
      cJSON_Delete(array);
    }
    return values;
  }

  cJSON *entry = nullptr;
  cJSON_ArrayForEach(entry, array) {
    values.push_back(JsonToDouble(entry));
  }
  cJSON_Delete(array);
  return values;
}

double TokensToUsdc(int tokens, int micro_usdc_per_million) {
  double micro = (static_cast<double>(tokens) * static_cast<double>(micro_usdc_per_million)) /
                 1000000.0;
  return micro / 1000000.0;
}

double EstimatedChatCostUsdc(int prompt_tokens, int completion_tokens) {
  return TokensToUsdc(prompt_tokens, CONFIG_SURVAIV_INPUT_PRICE_MICROUSDC_PER_1M) +
         TokensToUsdc(completion_tokens, CONFIG_SURVAIV_OUTPUT_PRICE_MICROUSDC_PER_1M);
}

}  // namespace survaiv
