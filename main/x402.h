#pragma once

#include <string>

#include "types.h"

namespace survaiv {
namespace x402 {

// Initialize x402 payment module (call after wallet::Init).
void Init();

// Check if x402 provider is configured.
bool IsConfigured();

// Build X-PAYMENT header value for a 402 response.
// Returns base64-encoded JSON payment payload, or empty string on failure.
std::string MakePayment(const HttpResponse &resp_402);

// Total USDC spent on x402 inference payments this session.
double TotalSpentUsdc();

// Reset spending tracker (e.g., on new day).
void ResetSpending();

}  // namespace x402
}  // namespace survaiv
