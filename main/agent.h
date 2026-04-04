#pragma once

#include <string>

#include "ledger.h"
#include "types.h"

namespace survaiv {

std::string BuildSystemPrompt(bool paper_only, bool geoblocked);
std::string BuildUserPrompt(const GeoblockStatus &geoblock, const BudgetLedger &ledger,
                            const std::vector<MarketSnapshot> &markets);

bool ChatCompletion(const std::string &system_prompt, const std::string &user_prompt,
                    std::string *content_out, UsageStats *usage_out,
                    const std::string &model_override = "");

ToolCall ParseToolCall(const std::string &json_text);
Decision ParseDecision(const std::string &json_text);
void SpendForUsage(BudgetLedger *ledger, const UsageStats &usage);
// Returns 0 on success, or a retry delay in seconds if the LLM was unreachable.
int RunAgentCycle(BudgetLedger *ledger);
void LogLedgerState(const BudgetLedger &ledger, const std::vector<MarketSnapshot> &markets);

}  // namespace survaiv
