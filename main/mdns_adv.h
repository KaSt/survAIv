#pragma once
// Lightweight mDNS responder — advertises <hostname>.local on the network.
// Runs as a FreeRTOS background task listening on UDP port 5353.

namespace survaiv {
namespace mdns_adv {

// Start the mDNS responder task. |hostname| is the bare name (without ".local").
// If empty, defaults to "survaiv". Also registers an _http._tcp service on |port|.
void Start(const char *hostname, int http_port);

}  // namespace mdns_adv
}  // namespace survaiv
