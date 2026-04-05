#pragma once

#include "dashboard_state.h"

namespace survaiv {
namespace telemetry {

// Start the telemetry background task (call once from main).
void Init(DashboardState *state);

}  // namespace telemetry
}  // namespace survaiv
