#pragma once

#include <string>
#include <vector>

namespace survaiv {

struct NewsResult {
  std::string title;
  std::string snippet;
  std::string url;
};

// Search news using configured provider (Tavily or Brave).
// Returns up to `limit` results. Empty vector on failure or no API key.
std::vector<NewsResult> SearchNews(const std::string &query, int limit = 3);

// Build a JSON string from news results for the LLM prompt.
std::string BuildNewsJson(const std::vector<NewsResult> &results);

}  // namespace survaiv
