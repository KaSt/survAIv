#pragma once

#include <string>
#include <vector>

#include "cJSON.h"

namespace survaiv {

std::string ToLower(std::string text);
std::string JsonEscape(const std::string &text);
std::string StripCodeFence(std::string text);
double JsonToDouble(const cJSON *item);
std::string JsonToString(const cJSON *item);
std::vector<double> ParseStringifiedArrayToDoubles(const cJSON *item);
double TokensToUsdc(int tokens, int micro_usdc_per_million);
double EstimatedChatCostUsdc(int prompt_tokens, int completion_tokens);

}  // namespace survaiv
