#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <optional>

// Legacy types (global namespace, used by settings tab)
struct AccessPointInfo {
  std::string path;
  std::string devicePath;
  std::string ssid;
  std::uint8_t strength = 0;
  bool secured = false;
  bool active = false;
  uint32_t frequency = 0;
  std::string securityType;
};

struct VpnConnectionInfo {
  std::string uuid;
  std::string name;
  std::string type;
  bool active = false;
  bool autoconnect = false;
};

struct WireGuardConfig {
  std::string connectionName;
  std::string privateKey;
  std::string serverPublicKey;
  std::string endpoint;
  std::string allowedIPs;
  std::string clientIP;
  std::string dns;
  int listenPort = 51820;
  int persistentKeepalive = 25;
};

// Generic plugin-based VPN profile (OpenVPN, StrongSwan/IKEv2, L2TP, PPTP, AnyConnect).
// `data` becomes `vpn.data` ("key=value" pairs), `secrets` becomes `vpn.secrets`.
struct VpnAddParams {
  std::string name;
  std::string serviceType;
  std::vector<std::pair<std::string, std::string>> data;
  std::vector<std::pair<std::string, std::string>> secrets;
};

enum class NetworkConnectivity {
  Unknown = 0,
  None = 1,
  Wired = 2,
  Wireless = 3,
};

struct NetworkState {
  NetworkConnectivity kind = NetworkConnectivity::Unknown;
  bool connected = false;
  bool wirelessEnabled = false;
  bool scanning = false;
  bool vpnActive = false;
  std::string ssid;
  std::string ipv4;
  std::string macAddress;
  std::string interfaceName;
  std::uint8_t signalStrength = 0;
  std::vector<AccessPointInfo> accessPoints;
  std::vector<VpnConnectionInfo> vpnConnections;

  // Wired-specific fields
  std::string ipv4Gateway;
  std::string ipv6;
  std::string ipv6Gateway;
  std::vector<std::string> dnsServers;
  std::vector<std::string> dnsDomains;
  uint32_t speed = 0;
  bool carrier = false;
  std::string duplex;
  bool autoNegotiate = false;
  std::string permHwAddress;
  std::string driver;
  std::string driverVersion;
  std::string firmwareVersion;
  std::string vendor;
  std::string product;
  uint32_t mtu = 0;
  uint32_t deviceState = 0;
  uint32_t metered = 0;
  std::string activeConnectionPath;
  bool hasWiredDevice = false;
};

// New eh::net types (used by control center)
namespace eh::net {

enum class ConnectivityKind {
  Unknown = 0,
  None = 1,
  Wired = 2,
  Wireless = 3,
};

struct AccessPoint {
  std::string path;
  std::string devicePath;
  bool active = false;
  std::string ssid;
  int strength_pct = 0;
  bool secured = false;
};

struct Snapshot {
  ConnectivityKind kind = ConnectivityKind::Unknown;
  bool connected = false;
  bool wireless_enabled = false;
  bool scanning = false;
  std::string ssid;
  std::string ipv4;
  std::string iface;
  int signal_pct = 0;
  std::vector<AccessPoint> aps;
  std::vector<std::string> saved_ssids;
};

struct SecretRequest {
  std::string ssid;
};

} // namespace eh::net
