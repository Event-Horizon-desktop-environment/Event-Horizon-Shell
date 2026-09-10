#include "services/network/types/network_secret_agent.hpp"

NetworkSecretAgent& NetworkSecretAgent::instance() {
   
  static NetworkSecretAgent a;
  return a;
}

void NetworkSecretAgent::start() {
   
  if (started_) return;
  started_ = true;
  // No-op: secret handling is done in-process via
  // NetworkManagerService::queue_secret_for_ssid / submit_secret.
}
