#pragma once

#include <string>

namespace survaiv {
namespace webserver {

// Start the dashboard web server on the given port.
// Call after WiFi STA is connected.
void StartDashboard(int port = 80);

// Start the onboarding web server (AP mode).
void StartOnboarding(int port = 80);

// Stop the web server.
void Stop();

// Push an SSE event to all connected clients.
void PushSseEvent(const std::string &event_type, const std::string &data);

// Returns true if the server is running.
bool IsRunning();

}  // namespace webserver
}  // namespace survaiv
