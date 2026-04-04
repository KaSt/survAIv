#pragma once

#include <string>
#include <vector>

#include "types.h"

namespace survaiv {

GeoblockStatus FetchGeoblockStatus();

std::vector<MarketSnapshot> FetchMarkets(int limit, int offset = 0,
                                         const std::string &order = "volume_24hr");

std::string BuildMarketsJson(const std::vector<MarketSnapshot> &markets);

std::string BuildPositionsJson(const std::vector<Position> &positions,
                               const std::vector<MarketSnapshot> &markets);

}  // namespace survaiv
