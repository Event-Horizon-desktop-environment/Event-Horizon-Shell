#pragma once

// Simple secret agent: not actively used by the settings tab (which connects
// with AddAndActivateConnection2 providing the PSK directly).
// The control center uses an in-process secret queue instead.
class NetworkSecretAgent {
public:
  static NetworkSecretAgent& instance();
  void start();
private:
  NetworkSecretAgent() = default;
  bool started_ = false;
};
