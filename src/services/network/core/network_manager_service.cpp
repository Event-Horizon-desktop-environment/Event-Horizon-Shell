#include "services/network/core/network_manager_service.hpp"
#include "services/keyring/secret_service_daemon.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"

#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/sdbus-c++.h>

#include <algorithm>
#include <atomic>
#include <iostream>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <map>
#include <queue>
#include <set>
#include <thread>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>

#include <iostream>

#include "desktop_shell/common/log/verbose_log.hpp"

// D-Bus constants.
static constexpr const char* kNmBusName           = "org.freedesktop.NetworkManager";
static constexpr const char* kNmObjPath           = "/org/freedesktop/NetworkManager";
static constexpr const char* kNmInterface         = "org.freedesktop.NetworkManager";
static constexpr const char* kNmPropsIface        = "org.freedesktop.DBus.Properties";
static constexpr const char* kNmDeviceInterface   = "org.freedesktop.NetworkManager.Device";
static constexpr const char* kNmWirelessInterface = "org.freedesktop.NetworkManager.Device.Wireless";
static constexpr const char* kNmApInterface       = "org.freedesktop.NetworkManager.AccessPoint";
static constexpr const char* kNmIp4ConfigInterface= "org.freedesktop.NetworkManager.IP4Config";
static constexpr const char* kNmIp6ConfigInterface= "org.freedesktop.NetworkManager.IP6Config";
static constexpr const char* kNmDeviceWiredInterface = "org.freedesktop.NetworkManager.Device.Wired";
static constexpr const char* kNmActiveConnIface   = "org.freedesktop.NetworkManager.Connection.Active";
static constexpr const char* kNmSettingsInterface  = "org.freedesktop.NetworkManager.Settings";
static constexpr const char* kNmSettingsConnIface  = "org.freedesktop.NetworkManager.Settings.Connection";

static constexpr uint32_t kNmDeviceTypeEthernet = 1;
static constexpr uint32_t kNmDeviceTypeWifi     = 2;
static constexpr uint32_t kNmDeviceTypeVpn      = 11;

static std::string ssid_from_byte_array(const std::vector<uint8_t>& bytes) {
   
  if (bytes.empty()) return {};
  std::string out;
  out.reserve(bytes.size());
  for (auto b : bytes) {
    if (b >= 32 && b < 127) out.push_back(static_cast<char>(b));
    else out.push_back('.');
  }
  return out;
}

static std::string prop_string(sdbus::IProxy& proxy, const char* iface, const char* prop) {
   
  try { return proxy.getProperty(prop).onInterface(iface).get<std::string>(); }
  catch (const sdbus::Error& e) {
    if (eh_verbose_enabled()) std::cerr << "[nm] prop_string(" << prop << "): " << e.what() << '\n';
    return {};
  }
}

static bool prop_bool(sdbus::IProxy& proxy, const char* iface, const char* prop) {
   
  try { return proxy.getProperty(prop).onInterface(iface).get<bool>(); }
  catch (const sdbus::Error& e) {
    if (eh_verbose_enabled()) std::cerr << "[nm] prop_bool(" << prop << "): " << e.what() << '\n';
    return false;
  }
}

static int32_t prop_int(sdbus::IProxy& proxy, const char* iface, const char* prop) {
   
  try { return proxy.getProperty(prop).onInterface(iface).get<int32_t>(); }
  catch (const sdbus::Error& e) {
    if (eh_verbose_enabled()) std::cerr << "[nm] prop_int(" << prop << "): " << e.what() << '\n';
    return 0;
  }
}

static uint32_t prop_uint(sdbus::IProxy& proxy, const char* iface, const char* prop) {
   
  try { return proxy.getProperty(prop).onInterface(iface).get<uint32_t>(); }
  catch (const sdbus::Error& e) {
    if (eh_verbose_enabled()) std::cerr << "[nm] prop_uint(" << prop << "): " << e.what() << '\n';
    return 0;
  }
}

static sdbus::ObjectPath prop_objpath(sdbus::IProxy& proxy, const char* iface, const char* prop) {
   
  try { return proxy.getProperty(prop).onInterface(iface).get<sdbus::ObjectPath>(); }
  catch (const sdbus::Error& e) {
    if (eh_verbose_enabled()) std::cerr << "[nm] prop_objpath(" << prop << "): " << e.what() << '\n';
    return sdbus::ObjectPath{"/"};
  }
}

namespace eh::net {

// Command queue shared between the main thread and the worker.

struct CmdChannel {
  std::mutex mtx;
  std::queue<std::function<void()>> cmds;
  int wakeFd = -1;
};

static void push_cmd(CmdChannel& ch, std::function<void()> cmd) {
   
  {
    std::lock_guard lk(ch.mtx);
    ch.cmds.push(std::move(cmd));
  }
  if (ch.wakeFd >= 0) {
    uint64_t v = 1;
    (void)write(ch.wakeFd, &v, sizeof(v));
  }
}

// NetworkManagerService::Priv.

struct NetworkManagerService::Priv {
  std::thread worker;
  std::atomic<bool> workerRunning{false};
  int mainWakeFd = -1;
  int cmdWakeFd = -1;

  mutable std::mutex stateMtx;
  NetworkState state;

  std::mutex secretMtx;
  std::map<std::string, std::string> secretQueue;
  std::string pendingSsid;
  bool secretPending = false;

  CmdChannel cmdCh;

  std::int64_t scanBaselineLastScan = 0;
  std::atomic<bool> scanRequested{false};
  std::atomic<bool> vpnRefreshRequested{false};

  NetworkManagerService* self = nullptr;
};

// Worker helpers.

struct WorkerBus {
  std::unique_ptr<sdbus::IConnection> bus;
};

static void do_refresh(sdbus::IConnection& bus, sdbus::IProxy& nmProxy,
                       NetworkState& state,
                       std::map<std::string, std::string>& savedConns,
                       bool& savedLoaded, std::int64_t& scanBaseline) {
   
  const bool wasScanning = state.scanning;
  state = NetworkState{};
  state.scanning = wasScanning;

  try {
    const int32_t nmState = static_cast<int32_t>(prop_uint(nmProxy, kNmInterface, "State"));
    state.connected = (nmState >= 50);
    state.wirelessEnabled = prop_bool(nmProxy, kNmInterface, "WirelessEnabled");
  } catch (const sdbus::Error&) {}

  std::string connPath;
  try {
    connPath = prop_objpath(nmProxy, kNmInterface, "PrimaryConnection");
  } catch (const sdbus::Error&) {}
  if (connPath.empty() || connPath == "/") {
    try {
      const auto allActive = nmProxy.getProperty("ActiveConnections")
        .onInterface(kNmInterface).get<std::vector<sdbus::ObjectPath>>();
      if (!allActive.empty())
        connPath = static_cast<std::string>(allActive[0]);
    } catch (const sdbus::Error&) {}
  }

  if (!connPath.empty() && connPath != "/") {
    try {
      auto acProxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{connPath});
      const std::string type = prop_string(*acProxy, kNmActiveConnIface, "Type");

      std::string devicePath;
      try {
        const auto devs = acProxy->getProperty("Devices")
          .onInterface(kNmActiveConnIface).get<std::vector<sdbus::ObjectPath>>();
        if (!devs.empty()) devicePath = static_cast<std::string>(devs[0]);
      } catch (const sdbus::Error&) {}

      if (!devicePath.empty()) {
        try {
          auto devProxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{devicePath});
          const uint32_t devType = prop_uint(*devProxy, kNmDeviceInterface, "DeviceType");

          if (devType == kNmDeviceTypeEthernet)  state.kind = NetworkConnectivity::Wired;
          else if (devType == kNmDeviceTypeWifi) state.kind = NetworkConnectivity::Wireless;
          else if (devType == kNmDeviceTypeVpn)  state.kind = NetworkConnectivity::None;
          else                                   state.kind = NetworkConnectivity::None;

          state.interfaceName = prop_string(*devProxy, kNmDeviceInterface, "Interface");
          state.macAddress = prop_string(*devProxy, kNmDeviceInterface, "HwAddress");

          const std::string ip4Path = prop_string(*devProxy, kNmDeviceInterface, "Ip4Config");
          if (!ip4Path.empty()) {
            try {
              auto ip4Proxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{ip4Path});
              const auto addrData = ip4Proxy->getProperty("AddressData")
                .onInterface(kNmIp4ConfigInterface).get<std::vector<std::map<std::string, sdbus::Variant>>>();
              if (!addrData.empty()) {
                auto it = addrData[0].find("address");
                if (it != addrData[0].end())
                  state.ipv4 = it->second.get<std::string>();
              }
            } catch (const sdbus::Error&) {}
          }

          if (devType == kNmDeviceTypeEthernet) {
            state.speed = prop_uint(*devProxy, kNmDeviceWiredInterface, "Speed");
            state.carrier = prop_bool(*devProxy, kNmDeviceWiredInterface, "Carrier");
            state.duplex = prop_string(*devProxy, kNmDeviceWiredInterface, "Duplex");
            state.autoNegotiate = prop_bool(*devProxy, kNmDeviceWiredInterface, "AutoNegotiate");
            state.permHwAddress = prop_string(*devProxy, kNmDeviceWiredInterface, "PermHwAddress");

            state.driver = prop_string(*devProxy, kNmDeviceInterface, "Driver");
            state.driverVersion = prop_string(*devProxy, kNmDeviceInterface, "DriverVersion");
            state.firmwareVersion = prop_string(*devProxy, kNmDeviceInterface, "FirmwareVersion");
            state.vendor = prop_string(*devProxy, kNmDeviceInterface, "Vendor");
            state.product = prop_string(*devProxy, kNmDeviceInterface, "Product");
            state.mtu = prop_uint(*devProxy, kNmDeviceInterface, "Mtu");
            state.deviceState = prop_uint(*devProxy, kNmDeviceInterface, "State");
            state.metered = prop_uint(*devProxy, kNmDeviceInterface, "Metered");
            state.activeConnectionPath = connPath;

            state.hasWiredDevice = true;

            if (!ip4Path.empty()) {
              try {
                auto ip4ExtProxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{ip4Path});
                state.ipv4Gateway = prop_string(*ip4ExtProxy, kNmIp4ConfigInterface, "Gateway");
                try {
                  const auto dnsData = ip4ExtProxy->getProperty("NameserverData")
                    .onInterface(kNmIp4ConfigInterface).get<std::vector<std::map<std::string, sdbus::Variant>>>();
                  for (const auto& entry : dnsData) {
                    auto it = entry.find("address");
                    if (it != entry.end())
                      state.dnsServers.push_back(it->second.get<std::string>());
                  }
                } catch (const sdbus::Error&) {}
                try {
                  state.dnsDomains = ip4ExtProxy->getProperty("Domains")
                    .onInterface(kNmIp4ConfigInterface).get<std::vector<std::string>>();
                } catch (const sdbus::Error&) {}
              } catch (const sdbus::Error&) {}
            }

            const std::string ip6Path = prop_string(*devProxy, kNmDeviceInterface, "Ip6Config");
            if (!ip6Path.empty()) {
              try {
                auto ip6Proxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{ip6Path});
                try {
                  const auto addrData = ip6Proxy->getProperty("AddressData")
                    .onInterface(kNmIp6ConfigInterface).get<std::vector<std::map<std::string, sdbus::Variant>>>();
                  if (!addrData.empty()) {
                    auto it = addrData[0].find("address");
                    if (it != addrData[0].end())
                      state.ipv6 = it->second.get<std::string>();
                  }
                } catch (const sdbus::Error&) {}
                state.ipv6Gateway = prop_string(*ip6Proxy, kNmIp6ConfigInterface, "Gateway");
              } catch (const sdbus::Error&) {}
            }
          }

          if (devType == kNmDeviceTypeWifi) {
            const std::string apPath = prop_string(*devProxy, kNmWirelessInterface, "ActiveAccessPoint");
            if (!apPath.empty()) {
              try {
                auto apProxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{apPath});
                const auto ssidBytes = apProxy->getProperty("Ssid")
                  .onInterface(kNmApInterface).get<std::vector<uint8_t>>();
                state.ssid = ssid_from_byte_array(ssidBytes);
                try { state.signalStrength = apProxy->getProperty("Strength").onInterface(kNmApInterface).get<uint8_t>(); }
                catch (const sdbus::Error&) { state.signalStrength = 0; }
              } catch (const sdbus::Error&) {}
            }
          }
        } catch (const sdbus::Error&) {}
      }

      state.vpnActive = (type == "vpn" || type == "wireguard");
      if (!state.vpnActive) {
        try {
          const auto allActive = nmProxy.getProperty("ActiveConnections")
            .onInterface(kNmInterface).get<std::vector<sdbus::ObjectPath>>();
          for (const auto& ac : allActive) {
            if (static_cast<std::string>(ac) == connPath) continue;
            try {
              auto vpnProxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, ac);
              const auto vt = prop_string(*vpnProxy, kNmActiveConnIface, "Type");
              if (vt == "vpn" || vt == "wireguard") { state.vpnActive = true; break; }
            } catch (const sdbus::Error&) {}
          }
        } catch (const sdbus::Error&) {}
      }

      if (state.kind != NetworkConnectivity::Wireless) {
        try {
          const auto allActive = nmProxy.getProperty("ActiveConnections")
            .onInterface(kNmInterface).get<std::vector<sdbus::ObjectPath>>();
          for (const auto& ac : allActive) {
            if (static_cast<std::string>(ac) == connPath) continue;
            try {
              auto ac2Proxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, ac);
              const auto act = prop_string(*ac2Proxy, kNmActiveConnIface, "Type");
              if (act != "802-11-wireless") continue;
              const auto devs = ac2Proxy->getProperty("Devices")
                .onInterface(kNmActiveConnIface).get<std::vector<sdbus::ObjectPath>>();
              if (devs.empty()) continue;
              auto dev2Proxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, devs[0]);
              const uint32_t dt = prop_uint(*dev2Proxy, kNmDeviceInterface, "DeviceType");
              if (dt != kNmDeviceTypeWifi) continue;
              state.kind = NetworkConnectivity::Wireless;
              state.interfaceName = prop_string(*dev2Proxy, kNmDeviceInterface, "Interface");
              state.macAddress = prop_string(*dev2Proxy, kNmDeviceInterface, "HwAddress");
              const std::string apPath = prop_string(*dev2Proxy, kNmWirelessInterface, "ActiveAccessPoint");
              if (!apPath.empty()) {
                try {
                  auto ap2Proxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{apPath});
                  const auto ssidBytes = ap2Proxy->getProperty("Ssid")
                    .onInterface(kNmApInterface).get<std::vector<uint8_t>>();
                  state.ssid = ssid_from_byte_array(ssidBytes);
                  try { state.signalStrength = ap2Proxy->getProperty("Strength").onInterface(kNmApInterface).get<uint8_t>(); }
                  catch (const sdbus::Error&) { state.signalStrength = 0; }
                } catch (const sdbus::Error&) {}
              }
              break;
            } catch (const sdbus::Error&) {}
          }
        } catch (const sdbus::Error&) {}
      }
    } catch (...) {}
  }

  // Enumerate all devices to find wired (Ethernet) device
  // This covers the case where WiFi is the active connection but a cable is plugged
  if (!state.hasWiredDevice) {
    try {
      const auto devices = nmProxy.getProperty("Devices")
        .onInterface(kNmInterface).get<std::vector<sdbus::ObjectPath>>();
      for (const auto& devPath : devices) {
        try {
          auto devProxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, devPath);
          if (prop_uint(*devProxy, kNmDeviceInterface, "DeviceType") != kNmDeviceTypeEthernet) continue;

          state.hasWiredDevice = true;
          state.speed = prop_uint(*devProxy, kNmDeviceWiredInterface, "Speed");
          state.carrier = prop_bool(*devProxy, kNmDeviceWiredInterface, "Carrier");
          state.duplex = prop_string(*devProxy, kNmDeviceWiredInterface, "Duplex");
          state.autoNegotiate = prop_bool(*devProxy, kNmDeviceWiredInterface, "AutoNegotiate");
          state.permHwAddress = prop_string(*devProxy, kNmDeviceWiredInterface, "PermHwAddress");

          state.driver = prop_string(*devProxy, kNmDeviceInterface, "Driver");
          state.driverVersion = prop_string(*devProxy, kNmDeviceInterface, "DriverVersion");
          state.firmwareVersion = prop_string(*devProxy, kNmDeviceInterface, "FirmwareVersion");
          state.vendor = prop_string(*devProxy, kNmDeviceInterface, "Vendor");
          state.product = prop_string(*devProxy, kNmDeviceInterface, "Product");
          state.mtu = prop_uint(*devProxy, kNmDeviceInterface, "Mtu");
          state.deviceState = prop_uint(*devProxy, kNmDeviceInterface, "State");
          state.metered = prop_uint(*devProxy, kNmDeviceInterface, "Metered");
          state.activeConnectionPath = prop_string(*devProxy, kNmDeviceInterface, "ActiveConnection");

          if (state.interfaceName.empty())
            state.interfaceName = prop_string(*devProxy, kNmDeviceInterface, "Interface");
          if (state.macAddress.empty())
            state.macAddress = prop_string(*devProxy, kNmDeviceInterface, "HwAddress");

          const std::string ip4Path = prop_string(*devProxy, kNmDeviceInterface, "Ip4Config");
          if (!ip4Path.empty()) {
            try {
              auto ip4ExtProxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{ip4Path});
              state.ipv4Gateway = prop_string(*ip4ExtProxy, kNmIp4ConfigInterface, "Gateway");
              try {
                const auto dnsData = ip4ExtProxy->getProperty("NameserverData")
                  .onInterface(kNmIp4ConfigInterface).get<std::vector<std::map<std::string, sdbus::Variant>>>();
                for (const auto& entry : dnsData) {
                  auto it = entry.find("address");
                  if (it != entry.end())
                    state.dnsServers.push_back(it->second.get<std::string>());
                }
              } catch (const sdbus::Error&) {}
              try {
                state.dnsDomains = ip4ExtProxy->getProperty("Domains")
                  .onInterface(kNmIp4ConfigInterface).get<std::vector<std::string>>();
              } catch (const sdbus::Error&) {}
            } catch (const sdbus::Error&) {}
          }

          const std::string ip6Path = prop_string(*devProxy, kNmDeviceInterface, "Ip6Config");
          if (!ip6Path.empty()) {
            try {
              auto ip6Proxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{ip6Path});
              try {
                const auto addrData = ip6Proxy->getProperty("AddressData")
                  .onInterface(kNmIp6ConfigInterface).get<std::vector<std::map<std::string, sdbus::Variant>>>();
                if (!addrData.empty()) {
                  auto it = addrData[0].find("address");
                  if (it != addrData[0].end())
                    state.ipv6 = it->second.get<std::string>();
                }
              } catch (const sdbus::Error&) {}
              state.ipv6Gateway = prop_string(*ip6Proxy, kNmIp6ConfigInterface, "Gateway");
            } catch (const sdbus::Error&) {}
          }

          break;
        } catch (const sdbus::Error&) {}
      }
    } catch (const sdbus::Error&) {}
  }

  state.accessPoints.clear();
  try {
    const auto devices = nmProxy.getProperty("Devices")
      .onInterface(kNmInterface).get<std::vector<sdbus::ObjectPath>>();
    for (const auto& devPath : devices) {
      try {
        auto devProxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, devPath);
        const auto devType = prop_uint(*devProxy, kNmDeviceInterface, "DeviceType");
        if (devType != kNmDeviceTypeWifi) continue;
        const auto apPaths = devProxy->getProperty("AccessPoints")
          .onInterface(kNmWirelessInterface).get<std::vector<sdbus::ObjectPath>>();
        for (const auto& apPath : apPaths) {
          try {
            auto apProxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, apPath);
            const auto ssidBytes = apProxy->getProperty("Ssid")
              .onInterface(kNmApInterface).get<std::vector<uint8_t>>();
            if (ssidBytes.empty()) continue;
            AccessPointInfo ap;
            ap.path = apPath;
            ap.devicePath = devPath;
            ap.ssid = ssid_from_byte_array(ssidBytes);
            try { ap.strength = apProxy->getProperty("Strength").onInterface(kNmApInterface).get<uint8_t>(); }
            catch (const sdbus::Error&) { ap.strength = 0; }
            ap.frequency = prop_uint(*apProxy, kNmApInterface, "Frequency");
            const auto wpaFlags = prop_uint(*apProxy, kNmApInterface, "WpaFlags");
            const auto rsnFlags = prop_uint(*apProxy, kNmApInterface, "RsnFlags");
            ap.secured = (wpaFlags != 0 || rsnFlags != 0);
            if (ap.secured) {
              if (wpaFlags != 0 && rsnFlags != 0) ap.securityType = "WPA/WPA2";
              else if (rsnFlags != 0) ap.securityType = "WPA2";
              else if (wpaFlags != 0) ap.securityType = "WPA";
              else ap.securityType = "Secure";
            }
            auto existing = std::find_if(state.accessPoints.begin(), state.accessPoints.end(),
              [&](const AccessPointInfo& x) { return x.ssid == ap.ssid; });
            if (existing != state.accessPoints.end()) {
              if (ap.strength > existing->strength || ap.active) *existing = ap;
            } else {
              state.accessPoints.push_back(std::move(ap));
            }
          } catch (const sdbus::Error&) {}
        }
      } catch (const sdbus::Error&) {}
    }
    std::sort(state.accessPoints.begin(), state.accessPoints.end(),
      [](const AccessPointInfo& a, const AccessPointInfo& b) {
        if (a.active != b.active) return a.active;
        return a.strength > b.strength;
      });
  } catch (...) {}

  {
    try {
      const auto devices = nmProxy.getProperty("Devices")
        .onInterface(kNmInterface).get<std::vector<sdbus::ObjectPath>>();
      bool scanDone = true;
      for (const auto& devPath : devices) {
        try {
          auto dev = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, devPath);
          if (prop_uint(*dev, kNmDeviceInterface, "DeviceType") != kNmDeviceTypeWifi) continue;
          try {
            const auto ls = dev->getProperty("LastScan")
              .onInterface(kNmWirelessInterface).get<std::int64_t>();
            if (ls <= scanBaseline) scanDone = false;
          } catch (const sdbus::Error&) {}
        } catch (const sdbus::Error&) {}
      }
      if (scanDone) state.scanning = false;
    } catch (...) {}
  }

  savedConns.clear();
  try {
    auto settingsProxy = sdbus::createProxy(bus,
      sdbus::ServiceName{kNmBusName},
      sdbus::ObjectPath{"/org/freedesktop/NetworkManager/Settings"});
    const auto connPaths = settingsProxy->getProperty("Connections")
      .onInterface(kNmSettingsInterface).get<std::vector<sdbus::ObjectPath>>();
    for (const auto& cpath : connPaths) {
      try {
        auto connProxy = sdbus::createProxy(bus, sdbus::ServiceName{kNmBusName}, cpath);
        const auto settings = connProxy->getProperty("Settings")
          .onInterface(kNmSettingsConnIface).get<std::map<std::string, std::map<std::string, sdbus::Variant>>>();
        auto wit = settings.find("802-11-wireless");
        if (wit == settings.end()) continue;
        auto ssidIt = wit->second.find("ssid");
        if (ssidIt == wit->second.end()) continue;
        const auto ssidBytes = ssidIt->second.get<std::vector<uint8_t>>();
        savedConns[ssid_from_byte_array(ssidBytes)] = cpath;
      } catch (const sdbus::Error&) {}
    }
  } catch (...) {}
  savedLoaded = true;
}

struct NmResult {
  int rc = 0;
  std::string out; // merged stdout+stderr, trailing newlines trimmed
};

static std::string nmcli_exec(const char* cmd) {
  std::string result;
  FILE* fp = popen(cmd, "r");
  if (!fp) return result;
  char buf[4096];
  while (fgets(buf, sizeof(buf), fp)) result += buf;
  pclose(fp);
  return result;
}

static NmResult nmcli_run(const std::string& cmd) {
  NmResult res;
  FILE* fp = popen((cmd + " 2>&1").c_str(), "r");
  if (!fp) {
    res.rc = -1;
    res.out = "failed to spawn nmcli";
    return res;
  }
  char buf[4096];
  while (fgets(buf, sizeof(buf), fp)) res.out += buf;
  int status = pclose(fp);
  res.rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  while (!res.out.empty() && (res.out.back() == '\n' || res.out.back() == '\r'))
    res.out.pop_back();
  return res;
}

// Single-quote a string for /bin/sh so it can be embedded safely in a command.
static std::string sh_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

// Extract "Connection 'name' (uuid) successfully added." → the profile name.
static std::string nmcli_added_name(const std::string& output) {
  const std::string marker = "Connection '";
  auto p = output.find(marker);
  if (p == std::string::npos) return {};
  p += marker.size();
  auto e = output.find('\'', p);
  if (e == std::string::npos) return {};
  return output.substr(p, e - p);
}

// Derive a valid WireGuard interface name (<= 15 chars, [a-z0-9_-], not
// starting with a digit) from an arbitrary config basename. NetworkManager's
// WireGuard importer rejects files whose basename is not a valid interface
// name followed by ".conf", e.g. "AirVPN_NL-Alblasserdam_Dalim_UDP-1637-Entry3.conf".
static std::string wg_import_interface_name(const std::string& base) {
  std::string san;
  for (char c : base) {
    const unsigned char u = static_cast<unsigned char>(c);
    char v;
    if (std::isalnum(u)) {
      v = static_cast<char>(std::tolower(u));
    } else if (c == '_' || c == '-') {
      v = c;
    } else {
      v = '_';
    }
    if (san.size() < 15) san += v;
  }
  if (san.empty()) san = "wgimport";
  if (std::isdigit(static_cast<unsigned char>(san[0]))) {
    if (san.size() < 15) san.insert(san.begin(), 'w');
    else san[0] = 'w';
  }
  return san;
}

// Copy a config file to a private (0600) path used as the import source.
static bool copy_config_file(const std::string& src, const std::string& dst) {
  const int in = open(src.c_str(), O_RDONLY);
  if (in < 0) return false;
  const int out = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (out < 0) {
    close(in);
    return false;
  }
  fchmod(out, 0600);
  char buf[4096];
  bool ok = true;
  for (;;) {
    const ssize_t n = read(in, buf, sizeof(buf));
    if (n < 0) { ok = false; break; }
    if (n == 0) break;
    ssize_t off = 0;
    while (off < n) {
      const ssize_t w = write(out, buf + off, static_cast<size_t>(n - off));
      if (w <= 0) { ok = false; break; }
      off += w;
    }
    if (!ok) break;
  }
  close(in);
  close(out);
  if (!ok) unlink(dst.c_str());
  return ok;
}

static void do_refresh_vpn(std::vector<VpnConnectionInfo>& out) {
   
  out.clear();
  // Get active VPN/WireGuard connections: NAME:UUID:TYPE:STATE
  std::set<std::string> activeUuids;
  std::string activeOut = nmcli_exec("nmcli -t -f NAME,UUID,TYPE,STATE connection show --active 2>/dev/null");
  if (!activeOut.empty()) {
    size_t pos = 0;
    while (pos < activeOut.size()) {
      size_t nl = activeOut.find('\n', pos);
      std::string line = activeOut.substr(pos, nl - pos);
      pos = (nl == std::string::npos) ? activeOut.size() : nl + 1;
      if (line.empty()) continue;
      // NAME:UUID:TYPE:STATE
      auto c1 = line.find(':');
      if (c1 == std::string::npos) continue;
      auto c2 = line.find(':', c1 + 1);
      if (c2 == std::string::npos) continue;
      auto c3 = line.find(':', c2 + 1);
      if (c3 == std::string::npos) continue;
      std::string type = line.substr(c2 + 1, c3 - c2 - 1);
      if (type == "vpn" || type == "wireguard") {
        activeUuids.insert(line.substr(c1 + 1, c2 - c1 - 1));
      }
    }
  }

  // Get all saved connections: NAME:UUID:TYPE:AUTOCONNECT
  std::string allOut = nmcli_exec("nmcli -t -f NAME,UUID,TYPE,AUTOCONNECT connection show 2>/dev/null");
  if (!allOut.empty()) {
    size_t pos = 0;
    while (pos < allOut.size()) {
      size_t nl = allOut.find('\n', pos);
      std::string line = allOut.substr(pos, nl - pos);
      pos = (nl == std::string::npos) ? allOut.size() : nl + 1;
      if (line.empty()) continue;
      auto c1 = line.find(':');
      if (c1 == std::string::npos) continue;
      auto c2 = line.find(':', c1 + 1);
      if (c2 == std::string::npos) continue;
      auto c3 = line.find(':', c2 + 1);
      if (c3 == std::string::npos) continue;
      std::string type = line.substr(c2 + 1, c3 - c2 - 1);
      if (type != "vpn" && type != "wireguard") continue;
      VpnConnectionInfo vpn;
      vpn.uuid = line.substr(c1 + 1, c2 - c1 - 1);
      vpn.name = line.substr(0, c1);
      vpn.type = type;
      vpn.autoconnect = (line.substr(c3 + 1) == "yes");
      vpn.active = activeUuids.contains(vpn.uuid);
      out.push_back(std::move(vpn));
    }
  }

  std::sort(out.begin(), out.end(),
    [](const VpnConnectionInfo& a, const VpnConnectionInfo& b) {
      if (a.active != b.active) return a.active;
      return a.name < b.name;
    });
}

// Worker thread entry point.

void NetworkManagerService::worker_main(Priv* d) {
   
  try {
    auto bus = sdbus::createSystemBusConnection();
    auto nmProxy = sdbus::createProxy(*bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{kNmObjPath});

    // Signal handler — wakes the worker
    nmProxy->uponSignal("PropertiesChanged")
      .onInterface(kNmPropsIface)
      .call([d](const std::string& iface,
                const std::map<std::string, sdbus::Variant>& changed,
                const std::vector<std::string>&) {
         
        if (iface == kNmInterface) {
          if (changed.contains("ActiveConnections")) {
            d->vpnRefreshRequested = true;
          }
          if (d->cmdWakeFd >= 0) {
            uint64_t v = 1;
            (void)write(d->cmdWakeFd, &v, sizeof(v));
          }
        }
      });

    bus->enterEventLoopAsync();

    // Worker-local caches.
    NetworkState ws;
    std::map<std::string, std::string> savedConns;
    bool savedLoaded = false;
    std::int64_t scanBaseline = 0;

    // Helper: flush worker state to the shared state and signal the main thread.
    auto commit = [d](const NetworkState& st) {
      {
        std::lock_guard lk(d->stateMtx);
        d->state = st;
      }
      if (d->mainWakeFd >= 0) {
        uint64_t v = 1;
        (void)write(d->mainWakeFd, &v, sizeof(v));
      }
    };

    // Initial full refresh.
    do_refresh(*bus, *nmProxy, ws, savedConns, savedLoaded, scanBaseline);
    do_refresh_vpn(ws.vpnConnections);
    ws.vpnActive = !ws.vpnConnections.empty() && std::any_of(ws.vpnConnections.begin(), ws.vpnConnections.end(),
      [](const auto& v) { return v.active; });
    d->scanBaselineLastScan = scanBaseline;
    commit(ws);

    // Periodic refresh loop.
    auto lastRefresh = std::chrono::steady_clock::now();

    while (d->workerRunning) {
      const auto now = std::chrono::steady_clock::now();
      const bool due = (now - lastRefresh >= std::chrono::seconds{2});

      // Check for pending scan request
      if (d->scanRequested.exchange(false)) {
        scanBaseline = 0;
        try {
          const auto devices = nmProxy->getProperty("Devices")
            .onInterface(kNmInterface).get<std::vector<sdbus::ObjectPath>>();
          for (const auto& devPath : devices) {
            try {
              auto devProxy = sdbus::createProxy(*bus, sdbus::ServiceName{kNmBusName}, devPath);
              const auto devType = prop_int(*devProxy, kNmDeviceInterface, "DeviceType");
              if (devType != static_cast<int32_t>(kNmDeviceTypeWifi)) continue;
              const auto ls = prop_int(*devProxy, kNmWirelessInterface, "LastScan");
              if (ls > scanBaseline) scanBaseline = ls;
            } catch (const sdbus::Error&) {}
          }
        } catch (const sdbus::Error&) {}
        try {
          const auto devices = nmProxy->getProperty("Devices")
            .onInterface(kNmInterface).get<std::vector<sdbus::ObjectPath>>();
          for (const auto& devPath : devices) {
            try {
              auto devProxy = sdbus::createProxy(*bus, sdbus::ServiceName{kNmBusName}, devPath);
              const auto devType = prop_int(*devProxy, kNmDeviceInterface, "DeviceType");
              if (devType != static_cast<int32_t>(kNmDeviceTypeWifi)) continue;
              devProxy->callMethod("RequestScan")
                .onInterface(kNmWirelessInterface)
                .withArguments(std::map<std::string, sdbus::Variant>{});
            } catch (const sdbus::Error& e) {
              std::cerr << "[nm] RequestScan failed: " << e.what() << '\n';
            }
          }
        } catch (const sdbus::Error& e) {
          std::cerr << "[nm] GetDevices for scan failed: " << e.what() << '\n';
        }
        ws.scanning = true;
        commit(ws);
      }

      // Drain command queue
      bool anyCmd = false;
      for (;;) {
        std::function<void()> cmd;
        {
          std::lock_guard lk(d->cmdCh.mtx);
          if (d->cmdCh.cmds.empty()) break;
          cmd = std::move(d->cmdCh.cmds.front());
          d->cmdCh.cmds.pop();
        }
        if (cmd) { cmd(); anyCmd = true; }
      }

      if (due || anyCmd) {
        do_refresh(*bus, *nmProxy, ws, savedConns, savedLoaded, scanBaseline);
        do_refresh_vpn(ws.vpnConnections);
        ws.vpnActive = !ws.vpnConnections.empty() && std::any_of(ws.vpnConnections.begin(), ws.vpnConnections.end(),
          [](const auto& v) { return v.active; });
        d->scanBaselineLastScan = scanBaseline;
        commit(ws);
        lastRefresh = now;
      }

      if (d->vpnRefreshRequested.exchange(false)) {
        do_refresh_vpn(ws.vpnConnections);
        ws.vpnActive = !ws.vpnConnections.empty() && std::any_of(ws.vpnConnections.begin(), ws.vpnConnections.end(),
          [](const auto& v) { return v.active; });
        commit(ws);
      }

      // Poll with timeout (500ms for responsiveness to stop signal)
      struct pollfd pfd;
      pfd.fd = d->cmdWakeFd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      int r = poll(&pfd, 1, 500);
      if (r > 0) {
        uint64_t val = 0;
        (void)read(d->cmdWakeFd, &val, sizeof(val));
      }
    }

    bus->leaveEventLoop();
  } catch (const std::exception& e) {
    std::cerr << "[nm] worker fatal: " << e.what() << '\n';
  }
}

// NetworkManagerService implementation.

NetworkManagerService& NetworkManagerService::instance() {
   
  static NetworkManagerService svc;
  return svc;
}

NetworkManagerService::NetworkManagerService()
  : d(std::make_unique<Priv>()) {
   
  d->self = this;
}

NetworkManagerService::~NetworkManagerService() { stop(); }

void NetworkManagerService::start() {
   
  if (started_) return;
  started_ = true;

  d->mainWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  d->cmdWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  d->cmdCh.wakeFd = d->cmdWakeFd;

  if (d->mainWakeFd < 0 || d->cmdWakeFd < 0) {
    std::cerr << "[nm] eventfd creation failed\n";
    return;
  }

  d->workerRunning = true;
  d->worker = std::thread(worker_main, d.get());
  std::cerr << "[nm] worker launched\n";
}

void NetworkManagerService::stop() {
   
  if (!started_ || !d->workerRunning) return;
  std::cerr << "[nm] stopping worker…\n";
  d->workerRunning = false;

  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
  if (d->worker.joinable()) d->worker.join();

  if (d->mainWakeFd >= 0) { close(d->mainWakeFd); d->mainWakeFd = -1; }
  if (d->cmdWakeFd >= 0) { close(d->cmdWakeFd); d->cmdWakeFd = -1; }
  d->cmdCh.wakeFd = -1;
  started_ = false;
  std::cerr << "[nm] stopped\n";
}

int NetworkManagerService::wake_fd() const {
   
  return d->mainWakeFd;
}

void NetworkManagerService::handle_wake() {
   
  if (d->mainWakeFd >= 0) {
    uint64_t val = 0;
    (void)read(d->mainWakeFd, &val, sizeof(val));
  }
  if (onChange_) onChange_(snapshot());
}

void NetworkManagerService::set_change_callback(ChangeCallback cb) {
   
  onChange_ = std::move(cb);
}

void NetworkManagerService::refresh() {
   
  if (!started_) return;
  push_cmd(d->cmdCh, []{});
}

void NetworkManagerService::refreshVpnConnections() {
   
  if (!started_) return;
  d->vpnRefreshRequested = true;
  push_cmd(d->cmdCh, []{});
}

void NetworkManagerService::request_scan() {
   
  if (!started_) return;
  d->scanRequested = true;
  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
}

Snapshot NetworkManagerService::snapshot() const {
   
  std::lock_guard lk(d->stateMtx);
  const auto& st = d->state;
  Snapshot s;
  switch (st.kind) {
    case NetworkConnectivity::None:    s.kind = ConnectivityKind::None; break;
    case NetworkConnectivity::Wired:   s.kind = ConnectivityKind::Wired; break;
    case NetworkConnectivity::Wireless:s.kind = ConnectivityKind::Wireless; break;
    default:                            s.kind = ConnectivityKind::Unknown; break;
  }
  s.connected = st.connected;
  s.wireless_enabled = st.wirelessEnabled;
  s.scanning = st.scanning;
  s.ssid = st.ssid;
  s.ipv4 = st.ipv4;
  s.iface = st.interfaceName;
  s.signal_pct = st.signalStrength;

  s.aps.reserve(st.accessPoints.size());
  for (const auto& ap : st.accessPoints) {
    AccessPoint nap{
      .path = ap.path,
      .devicePath = ap.devicePath,
      .active = ap.active,
      .ssid = ap.ssid,
      .strength_pct = ap.strength,
      .secured = ap.secured,
    };
    s.aps.push_back(std::move(nap));
  }

  s.saved_ssids.reserve(s.saved_ssids.size());

  return s;
}

const NetworkState& NetworkManagerService::state() const {
   
  std::lock_guard lk(d->stateMtx);
  return d->state;
}

bool NetworkManagerService::activate_ap(const AccessPoint& ap) {
   
  return activate_ap(ap, {});
}

bool NetworkManagerService::activate_ap(const AccessPoint& ap, const std::string& psk) {
   
  if (!started_ || ap.path.empty()) return false;

  push_cmd(d->cmdCh, [ap, psk] {
    auto bus = sdbus::createSystemBusConnection();
    auto nmProxy = sdbus::createProxy(*bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{kNmObjPath});

    if (!psk.empty()) {
      std::map<std::string, std::map<std::string, sdbus::Variant>> connSettings;
      connSettings["802-11-wireless-security"]["key-mgmt"] = sdbus::Variant{std::string{"wpa-psk"}};
      connSettings["802-11-wireless-security"]["psk"] = sdbus::Variant{psk};

      const std::string devPath = ap.devicePath.empty() ? "/" : ap.devicePath;
      const std::map<std::string, sdbus::Variant> options{{"persist", sdbus::Variant{std::string{"disk"}}}};

      try {
        nmProxy->callMethod("AddAndActivateConnection2")
          .onInterface(kNmInterface)
          .withArguments(connSettings,
                         sdbus::ObjectPath{devPath},
                         sdbus::ObjectPath{ap.path},
                         options);
      } catch (const sdbus::Error& e) {
        if (std::string(e.getName()) != "org.freedesktop.DBus.Error.UnknownMethod")
          return;
        try {
          nmProxy->callMethod("AddAndActivateConnection")
            .onInterface(kNmInterface)
            .withArguments(connSettings,
                           sdbus::ObjectPath{devPath},
                           sdbus::ObjectPath{ap.path});
        } catch (const sdbus::Error&) {}
      }
      try {
        eh::keyring::SecretServiceDaemon::instance().store_wifi_password(ap.ssid, psk);
      } catch (const sdbus::Error&) {}
    } else {
      try {
        nmProxy->callMethod("ActivateConnection")
          .onInterface(kNmInterface)
          .withArguments(sdbus::ObjectPath{"/"},
                         sdbus::ObjectPath{"/"},
                         sdbus::ObjectPath{ap.path});
      } catch (const sdbus::Error&) {}
    }
  });

  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
  return true;
}

bool NetworkManagerService::activateAccessPoint(const AccessPointInfo& ap) {
   
  AccessPoint nap{
    .path = ap.path,
    .devicePath = ap.devicePath,
    .active = ap.active,
    .ssid = ap.ssid,
    .strength_pct = ap.strength,
    .secured = ap.secured,
  };
  return activate_ap(nap);
}

bool NetworkManagerService::activateAccessPoint(const AccessPointInfo& ap, const std::string& psk) {
   
  AccessPoint nap{
    .path = ap.path,
    .devicePath = ap.devicePath,
    .active = ap.active,
    .ssid = ap.ssid,
    .strength_pct = ap.strength,
    .secured = ap.secured,
  };
  return activate_ap(nap, psk);
}

void NetworkManagerService::set_wireless_enabled(bool enabled) {
   
  if (!started_) return;
  push_cmd(d->cmdCh, [enabled] {
    auto bus = sdbus::createSystemBusConnection();
    auto nmProxy = sdbus::createProxy(*bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{kNmObjPath});
    try {
      nmProxy->setProperty("WirelessEnabled")
        .onInterface(kNmInterface)
        .toValue(enabled);
    } catch (const std::exception& e) {
      std::cerr << "[nm] set wireless failed: " << e.what() << '\n';
    }
  });
  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
}

void NetworkManagerService::disconnect() {
   
  if (!started_) return;
  push_cmd(d->cmdCh, [] {
    auto bus = sdbus::createSystemBusConnection();
    auto nmProxy = sdbus::createProxy(*bus, sdbus::ServiceName{kNmBusName}, sdbus::ObjectPath{kNmObjPath});
    try {
      const auto activeConns = nmProxy->getProperty("ActiveConnections")
        .onInterface(kNmInterface).get<std::vector<sdbus::ObjectPath>>();
      if (!activeConns.empty()) {
        nmProxy->callMethod("DeactivateConnection")
          .onInterface(kNmInterface)
          .withArguments(activeConns[0]);
      }
    } catch (const std::exception& e) {
      std::cerr << "[nm] disconnect failed: " << e.what() << '\n';
    }
  });
  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
}

void NetworkManagerService::forget_ssid(const std::string& ssid) {
   
  if (!started_) return;
  push_cmd(d->cmdCh, [ssid] {
    auto bus = sdbus::createSystemBusConnection();
    sdbus::IConnection& bref = *bus;
    auto settingsProxy = sdbus::createProxy(bref,
      sdbus::ServiceName{kNmBusName},
      sdbus::ObjectPath{"/org/freedesktop/NetworkManager/Settings"});
    try {
      const auto connPaths = settingsProxy->getProperty("Connections")
        .onInterface(kNmSettingsInterface).get<std::vector<sdbus::ObjectPath>>();
      for (const auto& cpath : connPaths) {
        try {
          auto connProxy = sdbus::createProxy(bref, sdbus::ServiceName{kNmBusName}, cpath);
          const auto settings = connProxy->getProperty("Settings")
            .onInterface(kNmSettingsConnIface).get<std::map<std::string, std::map<std::string, sdbus::Variant>>>();
          auto wit = settings.find("802-11-wireless");
          if (wit == settings.end()) continue;
          auto ssidIt = wit->second.find("ssid");
          if (ssidIt == wit->second.end()) continue;
          const auto sBytes = ssidIt->second.get<std::vector<uint8_t>>();
          if (ssid_from_byte_array(sBytes) == ssid) {
            connProxy->callMethod("Delete").onInterface(kNmSettingsConnIface);
            break;
          }
        } catch (const sdbus::Error&) {}
      }
    } catch (const std::exception& e) {
      std::cerr << "[nm] forget SSID failed: " << e.what() << '\n';
    }
  });
  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
}

bool NetworkManagerService::has_saved_connection(const std::string& ssid) const {
   
  (void)ssid;
  return false;
}

// VPN (nmcli-based).

void NetworkManagerService::activateVpnConnection(const VpnConnectionInfo& vpn) {
   
  if (!started_ || vpn.uuid.empty()) return;
  push_cmd(d->cmdCh, [uuid = vpn.uuid] {
    std::string cmd = "nmcli connection up uuid " + uuid + " 2>/dev/null";
    nmcli_exec(cmd.c_str());
  });
  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
}

void NetworkManagerService::deactivateVpnConnection(const VpnConnectionInfo& vpn) {
   
  if (!started_ || vpn.uuid.empty()) return;
  push_cmd(d->cmdCh, [uuid = vpn.uuid] {
    std::string cmd = "nmcli connection down uuid " + uuid + " 2>/dev/null";
    nmcli_exec(cmd.c_str());
    // A manual disconnect must stick. Without disabling autoconnect here,
    // NetworkManager re-dials the VPN right after it is taken down (and with
    // several autoconnect-enabled VPNs it keeps alternating between them).
    cmd = "nmcli connection modify uuid " + uuid + " connection.autoconnect no 2>/dev/null";
    nmcli_exec(cmd.c_str());
  });
  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
}

void NetworkManagerService::setVpnAutoconnect(const std::string& uuid, bool enabled) {
   
  if (!started_ || uuid.empty()) return;
  push_cmd(d->cmdCh, [uuid, enabled] {
    std::string cmd = "nmcli connection modify uuid " + uuid + " connection.autoconnect " +
                      (enabled ? "yes" : "no") + " 2>/dev/null";
    nmcli_exec(cmd.c_str());
  });
  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
}

bool NetworkManagerService::addWireguardConnection(const WireGuardConfig& cfg, std::string& err) {
    
  err.clear();
  if (!started_) { err = "Network service is not running"; return false; }

  // Validate the required fields so nmcli import gets a sane config.
  const auto req = [&err](bool ok, const char* what) {
    if (!ok) err = "Missing " + std::string(what);
  };
  req(!cfg.connectionName.empty(), "connection name");
  if (!err.empty()) return false;
  req(!cfg.privateKey.empty(), "private key");
  if (!err.empty()) return false;
  req(!cfg.serverPublicKey.empty(), "server public key");
  if (!err.empty()) return false;
  req(!cfg.endpoint.empty(), "endpoint (host:port)");
  if (!err.empty()) return false;
  req(!cfg.allowedIPs.empty(), "allowed IPs");
  if (!err.empty()) return false;

  // Write the config to a private (0600) temp file.
  std::string content = "[Interface]\n";
  content += "PrivateKey = " + cfg.privateKey + "\n";
  if (!cfg.clientIP.empty())
    content += "Address = " + cfg.clientIP + "\n";
  if (!cfg.dns.empty())
    content += "DNS = " + cfg.dns + "\n";
  content += "\n[Peer]\n";
  content += "PublicKey = " + cfg.serverPublicKey + "\n";
  content += "Endpoint = " + cfg.endpoint + "\n";
  content += "AllowedIPs = " + cfg.allowedIPs + "\n";
  content += "PersistentKeepalive = " + std::to_string(cfg.persistentKeepalive) + "\n";

  const char* rtDir = std::getenv("XDG_RUNTIME_DIR");
  std::string dir = (rtDir && *rtDir) ? std::string(rtDir) : "/tmp";
  std::string tmpPath = dir + "/eh_wg_" + std::to_string(static_cast<long>(getpid())) + "_" +
                        std::to_string(static_cast<long>(time(nullptr))) + ".conf";

  int fd = open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) {
    err = "Cannot create temporary config file";
    return false;
  }
  fchmod(fd, 0600);
  ssize_t written = 0;
  while (written < static_cast<ssize_t>(content.size())) {
    ssize_t w = write(fd, content.data() + written, content.size() - static_cast<size_t>(written));
    if (w <= 0) break;
    written += w;
  }
  close(fd);
  if (written < static_cast<ssize_t>(content.size())) {
    unlink(tmpPath.c_str());
    err = "Failed to write temporary config file";
    return false;
  }

  // Import via nmcli — the only path that supports WireGuard peers.
  NmResult res = nmcli_run("nmcli connection import type wireguard file " + sh_quote(tmpPath));
  unlink(tmpPath.c_str());
  if (res.rc != 0) {
    err = res.out.empty() ? "nmcli import failed" : res.out;
    return false;
  }

  std::string importedName = nmcli_added_name(res.out);
  if (importedName.empty()) {
    err = "Import succeeded but the profile name could not be determined";
    return false;
  }

  // Rename to the desired connection name.
  if (importedName != cfg.connectionName) {
    NmResult rn = nmcli_run("nmcli connection modify " + sh_quote(importedName) +
                            " connection.id " + sh_quote(cfg.connectionName));
    if (rn.rc != 0) {
      nmcli_run("nmcli connection delete " + sh_quote(importedName));
      err = rn.out.empty() ? "Failed to rename connection" : rn.out;
      return false;
    }
  }
  return true;
}

bool NetworkManagerService::addVpnConnection(const VpnAddParams& params, std::string& err) {
    
  err.clear();
  if (!started_) { err = "Network service is not running"; return false; }
  if (params.name.empty()) { err = "Missing connection name"; return false; }
  if (params.serviceType.empty()) { err = "Missing VPN service type"; return false; }

  std::string cmd = "nmcli connection add type vpn con-name " + sh_quote(params.name) +
                    " vpn.service-type " + sh_quote(params.serviceType) + " connection.autoconnect no";
  if (!params.data.empty()) {
    std::string data;
    for (const auto& kv : params.data) {
      if (!data.empty()) data += ',';
      data += kv.first + "=" + kv.second;
    }
    cmd += " vpn.data " + sh_quote(data);
  }
  if (!params.secrets.empty()) {
    std::string secrets;
    for (const auto& kv : params.secrets) {
      if (!secrets.empty()) secrets += ',';
      secrets += kv.first + "=" + kv.second;
    }
    cmd += " vpn.secrets " + sh_quote(secrets);
  }

  NmResult res = nmcli_run(cmd);
  if (res.rc != 0) {
    err = res.out.empty() ? "nmcli add failed" : res.out;
    return false;
  }
  return true;
}

bool NetworkManagerService::importVpnFile(const std::string& type, const std::string& path, std::string& err) {
     
  err.clear();
  if (!started_) { err = "Network service is not running"; return false; }
  if (path.empty()) { err = "No file selected"; return false; }

  // WireGuard: NetworkManager derives the profile/interface name from the file
  // basename and requires it to be a valid interface name followed by ".conf"
  // (so long names like "AirVPN_NL-Alblasserdam_Dalim_UDP-1637-Entry3.conf"
  // are rejected outright). Import through a temp copy with a valid name,
  // then rename the profile to the original basename.
  if (type == "wireguard") {
    std::string base = path;
    const auto slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);

    const std::string san = wg_import_interface_name(base);
    const char* rtDir = std::getenv("XDG_RUNTIME_DIR");
    const std::string dir = (rtDir && *rtDir) ? std::string(rtDir) : "/tmp";
    const std::string tmpPath = dir + "/" + san + ".conf";

    if (!copy_config_file(path, tmpPath)) {
      err = "Cannot copy config file for import";
      return false;
    }
    NmResult res = nmcli_run("nmcli connection import type wireguard file " + sh_quote(tmpPath));
    unlink(tmpPath.c_str());
    if (res.rc != 0) {
      err = res.out.empty() ? "nmcli import failed" : res.out;
      return false;
    }
    const std::string importedName = nmcli_added_name(res.out);
    if (importedName.empty()) {
      err = "Import succeeded but the profile name could not be determined";
      return false;
    }
    if (importedName != base) {
      NmResult rn = nmcli_run("nmcli connection modify " + sh_quote(importedName) +
                              " connection.id " + sh_quote(base));
      if (rn.rc != 0) {
        nmcli_run("nmcli connection delete " + sh_quote(importedName));
        err = rn.out.empty() ? "Failed to rename imported connection" : rn.out;
        return false;
      }
    }
    // Imported VPNs must not auto-connect; the user chooses when to dial in.
    nmcli_run("nmcli connection modify " + sh_quote(base) + " connection.autoconnect no");
    return true;
  }

  NmResult res = nmcli_run("nmcli connection import type " + type + " file " + sh_quote(path));
  if (res.rc != 0) {
    err = res.out.empty() ? "nmcli import failed" : res.out;
    return false;
  }
  const std::string importedName = nmcli_added_name(res.out);
  if (!importedName.empty()) {
    nmcli_run("nmcli connection modify " + sh_quote(importedName) + " connection.autoconnect no");
  }
  return true;
}

void NetworkManagerService::removeVpnConnection(const std::string& uuid) {
   
  if (!started_ || uuid.empty()) return;
  push_cmd(d->cmdCh, [uuid] {
    std::string cmd = "nmcli connection delete uuid " + uuid + " 2>/dev/null";
    nmcli_exec(cmd.c_str());
  });
  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
}

// Secrets.

std::optional<SecretRequest> NetworkManagerService::pending_secret_request() {
   
  std::lock_guard lk(d->secretMtx);
  if (d->secretPending) {
    return SecretRequest{d->pendingSsid};
  }
  return std::nullopt;
}

void NetworkManagerService::submit_secret(const std::string& pw) {
   
  std::lock_guard lk(d->secretMtx);
  if (d->secretPending) {
    d->secretQueue[d->pendingSsid] = pw;
    d->secretPending = false;
    d->pendingSsid.clear();
  }
}

void NetworkManagerService::queue_secret_for_ssid(const std::string& ssid, const std::string& pw) {
   
  std::lock_guard lk(d->secretMtx);
  d->secretQueue[ssid] = pw;
}

void NetworkManagerService::log_error(const char* msg, const char* detail) {
    
  std::cerr << "[nm] " << msg;
  if (detail) std::cerr << detail;
  std::cerr << '\n';
}

bool NetworkManagerService::updateConnection(const std::string& connectionPath,
                                              const NmConnectionSettings& settings) {
  if (!started_ || connectionPath.empty() || connectionPath == "/") return false;

  push_cmd(d->cmdCh, [connectionPath, settings] {
    try {
      auto bus = sdbus::createSystemBusConnection();
      auto connProxy = sdbus::createProxy(*bus,
        sdbus::ServiceName{kNmBusName},
        sdbus::ObjectPath{connectionPath});

      const std::map<std::string, sdbus::Variant> emptyArgs;
      connProxy->callMethod("Update2")
        .onInterface(kNmSettingsConnIface)
        .withArguments(settings, uint32_t{0}, emptyArgs);
    } catch (const std::exception& e) {
      std::cerr << "[nm] Update2 failed: " << e.what() << '\n';
    }
  });

  if (d->cmdWakeFd >= 0) {
    uint64_t v = 1;
    (void)write(d->cmdWakeFd, &v, sizeof(v));
  }
  return true;
}

std::string NetworkManagerService::activeWiredConnectionPath() const {
  std::lock_guard lk(d->stateMtx);
  return d->state.activeConnectionPath;
}

} // namespace eh::net
