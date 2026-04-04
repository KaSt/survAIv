#pragma once

#include <cstdint>
#include <string>

namespace survaiv {
namespace wallet {

// Initialise wallet from NVS or Kconfig private key.
bool Init();

// Generate a new random wallet using hardware RNG.
// Stores in NVS and initialises the wallet. Returns true on success.
bool Generate();

// Returns true if a private key exists in NVS (without loading it).
bool HasStoredKey();

// 20-byte Ethereum address (available after Init()).
const uint8_t *Address();

// Hex-encoded address with 0x prefix.
std::string AddressHex();

// 32-byte private key (available after Init()).
const uint8_t *PrivateKey();

// Returns true if a wallet is loaded and usable.
bool IsReady();

// Query real USDC.e balance on Polygon via JSON-RPC.
// Returns balance in USDC (6 decimals → double), or -1.0 on error.
double QueryUsdcBalance();

// Check if CTF Exchange has USDC.e allowance.  Returns true if approved.
bool CheckUsdcApproval();

// Check if CTF Exchange has ERC-1155 (Conditional Tokens) approval.
bool CheckCtApproval();

// Send one-time ERC-20 approve(CTF_EXCHANGE, MAX_UINT256) on USDC.e.
// Requires MATIC for gas.
bool ApproveUsdc();

// Send one-time setApprovalForAll(CTF_EXCHANGE, true) on Conditional Tokens.
bool ApproveConditionalTokens();

// Run both approvals if needed. Logs status.
bool EnsureApprovals();

}  // namespace wallet
}  // namespace survaiv
