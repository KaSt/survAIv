#pragma once

namespace survaiv {
namespace onboard {

// Start AP mode with captive portal for initial setup.
// Returns when user saves config and device is about to reboot.
void StartAccessPoint();

}  // namespace onboard
}  // namespace survaiv
