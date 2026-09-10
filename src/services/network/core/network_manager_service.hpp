#pragma once

#include "services/network/types/network_types.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdbus-c++/Types.h>

namespace sdbus { class IConnection; class IProxy; class IObject; }

namespace eh::net {

class NetworkManagerService {
public:
  using ChangeCallback = std::function<void(const Snapshot&)>;

  static NetworkManagerService& instance();
  void start();
  void stop();
  void set_change_callback(ChangeCallback cb);

  Snapshot snapshot() const;
  const NetworkState& state() const;
  void refresh();
  void request_scan();
  bool activate_ap(const AccessPoint& ap);
  bool activate_ap(const AccessPoint& ap, const std::string& psk);
  void set_wireless_enabled(bool enabled);
  void disconnect();
  void forget_ssid(const std::string& ssid);
  bool has_saved_connection(const std::string& ssid) const;

  // Secret queue for in-process WiFi password handling.
  std::optional<SecretRequest> pending_secret_request();
  void submit_secret(const std::string& pw);
  void queue_secret_for_ssid(const std::string& ssid, const std::string& pw);

  // VPN / WireGuard (uses nmcli). Synchronous add/import helpers.
  // Synchronous add/import helpers. They run nmcli directly (fast, ~100-300ms)
  // and report real success/failure through `err`. Only safe to call from the
  // UI thread (they do not touch the worker's sdbus connection).
  bool addWireguardConnection(const WireGuardConfig& cfg, std::string& err);
  bool addVpnConnection(const VpnAddParams& params, std::string& err);
  bool importVpnFile(const std::string& type, const std::string& path, std::string& err);
  void removeVpnConnection(const std::string& uuid);
  void activateVpnConnection(const VpnConnectionInfo& vpn);
  void deactivateVpnConnection(const VpnConnectionInfo& vpn);
  void setVpnAutoconnect(const std::string& uuid, bool enabled);
  void refreshVpnConnections();

  // camelCase aliases used by the settings tab.
  void setChangeCallback(ChangeCallback cb) { set_change_callback(std::move(cb)); }
  void requestScan() { request_scan(); }
  bool activateAccessPoint(const AccessPointInfo& ap);
  bool activateAccessPoint(const AccessPointInfo& ap, const std::string& psk);
  bool activateAccessPointPsk(const AccessPointInfo& ap, const std::string& psk) { return activateAccessPoint(ap, psk); }
  void setWirelessEnabled(bool enabled) { set_wireless_enabled(enabled); }
  bool hasSavedConnection(const std::string& ssid) const { return has_saved_connection(ssid); }
  void forgetSsid(const std::string& ssid) { forget_ssid(ssid); }

  // Connection profile editing (D-Bus Update2).
  using NmConnectionSettings = std::map<std::string, std::map<std::string, sdbus::Variant>>;
  bool updateConnection(const std::string& connectionPath, const NmConnectionSettings& settings);
  std::string activeWiredConnectionPath() const;

  // Main loop integration (eventfd wakeup).
  int wake_fd() const;
  void handle_wake();

private:
  NetworkManagerService();
  ~NetworkManagerService();
  NetworkManagerService(const NetworkManagerService&) = delete;
  NetworkManagerService& operator=(const NetworkManagerService&) = delete;
  NetworkManagerService(NetworkManagerService&&) = delete;
  NetworkManagerService& operator=(NetworkManagerService&&) = delete;

  void emitChange();
  void log_error(const char* msg, const char* detail = nullptr);

  struct Priv;
  static void worker_main(Priv* d);
  std::unique_ptr<Priv> d;
  ChangeCallback onChange_;
  bool started_ = false;
};

} // namespace eh::net
