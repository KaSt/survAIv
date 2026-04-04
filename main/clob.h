#pragma once

#include <cstdint>
#include <string>

#include "types.h"

namespace survaiv {
namespace clob {

// Initialise CLOB client — derives API credentials via L1 EIP-712 auth.
// Requires wallet::Init() to have succeeded.
// Returns true if API credentials are obtained.
bool Init();

// Returns true if CLOB credentials are valid and ready.
bool IsReady();

// Place a limit order. Returns order_id on success, empty on failure.
// side: 0 = BUY, 1 = SELL.
// token_id: the CLOB token ID for the outcome (YES or NO).
// price: limit price in USDC (e.g. 0.65 for 65 cents).
// size: number of outcome tokens (= USDC amount / price for BUY).
std::string PlaceOrder(const std::string &token_id, int side,
                       double price, double size);

// Cancel an order by ID. Returns true on success.
bool CancelOrder(const std::string &order_id);

// Query open orders. Returns JSON string of orders array.
std::string GetOpenOrders();

// Get fee rate in basis points for the current maker.
int GetFeeRateBps();

// Refresh API credentials (call on 401 or periodically).
bool RefreshCredentials();

}  // namespace clob
}  // namespace survaiv
