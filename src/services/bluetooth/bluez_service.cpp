#include "services/bluetooth/bluez_service.hpp"

#include "platform/rfkill_helper.h"

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>

#include <algorithm>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace eh::bt {
namespace {

const sdbus::ServiceName kBluez{"org.bluez"};
const sdbus::ObjectPath kBluezRoot{"/"};
constexpr auto kObjMgr = "org.freedesktop.DBus.ObjectManager";
constexpr auto kProps = "org.freedesktop.DBus.Properties";
constexpr auto kAdapter = "org.bluez.Adapter1";
constexpr auto kDevice = "org.bluez.Device1";
constexpr auto kBattery = "org.bluez.Battery1";

using IfaceProps = std::map<std::string, sdbus::Variant>;
using ManagedObjects = std::map<sdbus::ObjectPath, std::map<std::string, IfaceProps>>;

template <typename T>
T get_or(const IfaceProps& p, const char* key, T fallback) {
   
  auto it = p.find(key);
  if (it == p.end()) return fallback;
  try {
    return it->second.get<T>();
  } catch (const sdbus::Error&) {
    return fallback;
  }
}

BluetoothDeviceKind classify_icon(const std::string& icon) {
   
  if (icon == "audio-headset") return BluetoothDeviceKind::Headset;
  if (icon == "audio-headphones") return BluetoothDeviceKind::Headphones;
  if (icon == "audio-card" || icon == "audio-speakers") return BluetoothDeviceKind::Speaker;
  if (icon == "input-mouse") return BluetoothDeviceKind::Mouse;
  if (icon == "input-keyboard") return BluetoothDeviceKind::Keyboard;
  if (icon == "input-gaming") return BluetoothDeviceKind::Gamepad;
  if (icon == "phone") return BluetoothDeviceKind::Phone;
  if (icon == "computer") return BluetoothDeviceKind::Computer;
  if (icon == "video-display") return BluetoothDeviceKind::Tv;
  return BluetoothDeviceKind::Unknown;
}

BluetoothDeviceKind classify_class(uint32_t cod) {
   
  const uint8_t major = (cod >> 8) & 0x1f;
  const uint8_t minor = (cod >> 2) & 0x3f;
  switch (major) {
    case 0x01: return BluetoothDeviceKind::Computer;
    case 0x02: return BluetoothDeviceKind::Phone;
    case 0x04:
      switch (minor >> 4) {
        case 1: return BluetoothDeviceKind::Headphones;
        case 2: return BluetoothDeviceKind::Speaker;
        case 4: return BluetoothDeviceKind::Microphone;
        case 6: return BluetoothDeviceKind::Tv;
        default: return (minor & 0x04) ? BluetoothDeviceKind::Headset : BluetoothDeviceKind::Unknown;
      }
    case 0x05:
      if ((minor >> 2) == 1) return BluetoothDeviceKind::Keyboard;
      if ((minor >> 2) == 2) return BluetoothDeviceKind::Mouse;
      if ((minor >> 2) == 3) return BluetoothDeviceKind::Gamepad;
      return BluetoothDeviceKind::Unknown;
    case 0x07: return BluetoothDeviceKind::Watch;
    case 0x08: return BluetoothDeviceKind::Gamepad;
    default: return BluetoothDeviceKind::Unknown;
  }
}

DeviceInfo make_device(const sdbus::ObjectPath& path, const IfaceProps& props) {
   
  DeviceInfo d;
  d.path = std::string(path);
  d.address = get_or<std::string>(props, "Address", "");
  d.alias = get_or<std::string>(props, "Alias", "");
  if (d.alias.empty()) d.alias = get_or<std::string>(props, "Name", "");
  if (d.alias.empty()) d.alias = d.address;
  d.paired = get_or<bool>(props, "Paired", false);
  d.trusted = get_or<bool>(props, "Trusted", false);
  d.connected = get_or<bool>(props, "Connected", false);
  d.has_rssi = props.find("RSSI") != props.end();
  d.rssi = get_or<int16_t>(props, "RSSI", 0);
  const std::string icon = get_or<std::string>(props, "Icon", "");
  d.kind = classify_icon(icon);
  if (d.kind == BluetoothDeviceKind::Unknown) {
    const uint32_t cod = get_or<uint32_t>(props, "Class", 0);
    d.kind = classify_class(cod);
  }
  return d;
}

}

BluezService& BluezService::instance() {
   
  static BluezService s{};
  return s;
}

BluezService::BluezService() = default;
BluezService::~BluezService() = default;

void BluezService::start() {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (started_) return;
  started_ = true;

  bus_ = sdbus::createSystemBusConnection();
  objmgr_ = sdbus::createProxy(*bus_, kBluez, kBluezRoot);

  bind_signals_locked();
  refresh_locked();

  bus_->enterEventLoopAsync();
}

void BluezService::start_discovery() {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (!started_ || snap_.adapter_path.empty()) return;
  try {
    if (!adapter_proxy_) {
      adapter_proxy_ = sdbus::createProxy(*bus_, kBluez, sdbus::ObjectPath{snap_.adapter_path});
      cached_adapter_path_ = snap_.adapter_path;
    }
    adapter_proxy_->callMethod("StartDiscovery").onInterface(kAdapter);
  } catch (const sdbus::Error&) {
  }
}

void BluezService::stop_discovery() {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (!started_ || snap_.adapter_path.empty()) return;
  try {
    if (!adapter_proxy_) {
      adapter_proxy_ = sdbus::createProxy(*bus_, kBluez, sdbus::ObjectPath{snap_.adapter_path});
      cached_adapter_path_ = snap_.adapter_path;
    }
    adapter_proxy_->callMethod("StopDiscovery").onInterface(kAdapter);
  } catch (const sdbus::Error&) {
  }
}

void BluezService::set_powered(bool on) {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (!started_ || snap_.adapter_path.empty()) return;
  try {
    if (!adapter_proxy_) {
      adapter_proxy_ = sdbus::createProxy(*bus_, kBluez, sdbus::ObjectPath{snap_.adapter_path});
      cached_adapter_path_ = snap_.adapter_path;
    }
    adapter_proxy_->setProperty("Powered").onInterface(kAdapter).toValue(on);
    snap_.powered = on;
    if (on_change_) on_change_();
  } catch (const sdbus::Error&) {
  }
}

void BluezService::refresh() {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (started_ && objmgr_) refresh_locked();
}

void BluezService::set_change_callback(ChangeCallback cb) {
   
  std::lock_guard<std::mutex> lock(mtx_);
  on_change_ = std::move(cb);
}

Snapshot BluezService::snapshot() const {
   
  std::lock_guard<std::mutex> lock(mtx_);
  return snap_;
}

std::vector<DeviceInfo> BluezService::devices() const {
    
  std::lock_guard<std::mutex> lock(mtx_);
  return devices_;
}

FullState BluezService::full_state() const {
    
  std::lock_guard<std::mutex> lock(mtx_);
  return {snap_, devices_};
}

void BluezService::bind_signals_locked() {
    
  if (!objmgr_) return;

  auto debounce_ms = [this]() -> std::chrono::milliseconds {
    return snap_.scanning ? std::chrono::milliseconds(500) : std::chrono::milliseconds(200);
  };

  objmgr_->uponSignal("InterfacesAdded").onInterface(kObjMgr).call(
      [this, debounce_ms](const sdbus::ObjectPath& path,
                          const std::map<std::string, IfaceProps>& interfaces) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto devIt = interfaces.find(kDevice);
    if (devIt != interfaces.end() && !snap_.adapter_path.empty() &&
        std::string(path).rfind(snap_.adapter_path + "/", 0) == 0) {
      DeviceInfo d = make_device(path, devIt->second);
      auto batIt = interfaces.find(kBattery);
      if (batIt != interfaces.end()) {
        d.has_battery = true;
        d.battery_percent = get_or<uint8_t>(batIt->second, "Percentage", 0);
      }
      bool found = false;
      for (auto& existing : devices_) {
        if (existing.path == d.path) {
          existing = std::move(d);
          found = true;
          break;
        }
      }
      if (!found) devices_.push_back(std::move(d));
      snap_.paired_count = 0;
      snap_.connected_count = 0;
      for (const auto& dev : devices_) {
        if (dev.paired) snap_.paired_count++;
        if (dev.connected) snap_.connected_count++;
      }
      snap_.connected = (snap_.powered && snap_.connected_count > 0);
      if (on_change_) on_change_();
    } else {
      auto now = std::chrono::steady_clock::now();
      if (now - lastRefreshDebounce_ >= debounce_ms()) {
        lastRefreshDebounce_ = now;
        refresh_locked();
      }
    }
  });

  objmgr_->uponSignal("InterfacesRemoved").onInterface(kObjMgr).call(
      [this, debounce_ms](const sdbus::ObjectPath& path,
                          const std::vector<std::string>& ) {
    std::lock_guard<std::mutex> lock(mtx_);
    bool removed = false;
    for (auto it = devices_.begin(); it != devices_.end(); ++it) {
      if (it->path == std::string(path)) {
        devices_.erase(it);
        removed = true;
        break;
      }
    }
    if (removed) {
      snap_.paired_count = 0;
      snap_.connected_count = 0;
      for (const auto& dev : devices_) {
        if (dev.paired) snap_.paired_count++;
        if (dev.connected) snap_.connected_count++;
      }
      snap_.connected = (snap_.powered && snap_.connected_count > 0);
      if (on_change_) on_change_();
    } else {
      auto now = std::chrono::steady_clock::now();
      if (now - lastRefreshDebounce_ >= debounce_ms()) {
        lastRefreshDebounce_ = now;
        refresh_locked();
      }
    }
  });

  objmgr_->uponSignal("PropertiesChanged").onInterface(kProps).call(
      [this, debounce_ms](const std::string& iface,
                          const std::map<std::string, sdbus::Variant>& props,
                          const std::vector<std::string>& ) {
    if (iface != kAdapter && iface != kDevice) return;
    std::lock_guard<std::mutex> lock(mtx_);
    if (iface == kAdapter) {
      auto pit = props.find("Powered");
      if (pit != props.end()) {
        try { snap_.powered = pit->second.get<bool>(); } catch (...) {}
      }
      auto dit = props.find("Discovering");
      if (dit != props.end()) {
        try { snap_.scanning = dit->second.get<bool>(); } catch (...) {}
      }
      if (on_change_) on_change_();
    } else if (iface == kDevice) {
      // Extract device path from the signal message sender
      try {
        auto msg = objmgr_->getCurrentlyProcessedMessage();
        std::string path = msg.getPath();
        for (auto& dev : devices_) {
          if (dev.path == path) {
            auto it = props.find("Connected");
            if (it != props.end()) dev.connected = it->second.get<bool>();
            it = props.find("Paired");
            if (it != props.end()) dev.paired = it->second.get<bool>();
            it = props.find("Trusted");
            if (it != props.end()) dev.trusted = it->second.get<bool>();
            it = props.find("RSSI");
            if (it != props.end()) { dev.has_rssi = true; dev.rssi = it->second.get<int16_t>(); }
            it = props.find("Alias");
            if (it != props.end()) dev.alias = it->second.get<std::string>();
            it = props.find("Name");
            if (it != props.end() && dev.alias.empty()) dev.alias = it->second.get<std::string>();
            it = props.find("Icon");
            if (it != props.end()) {
              std::string icon = it->second.get<std::string>();
              dev.kind = classify_icon(icon);
              if (dev.kind == BluetoothDeviceKind::Unknown) {
                auto cit = props.find("Class");
                if (cit != props.end()) {
                  uint32_t cod = cit->second.get<uint32_t>();
                  dev.kind = classify_class(cod);
                }
              }
            }
            it = props.find("Class");
            if (it != props.end() && dev.kind == BluetoothDeviceKind::Unknown) {
              uint32_t cod = it->second.get<uint32_t>();
              dev.kind = classify_class(cod);
            }
            // Recalculate counts
            snap_.paired_count = 0;
            snap_.connected_count = 0;
            for (const auto& d : devices_) {
              if (d.paired) snap_.paired_count++;
              if (d.connected) snap_.connected_count++;
            }
            snap_.connected = (snap_.powered && snap_.connected_count > 0);
            if (on_change_) on_change_();
            return;
          }
        }
      } catch (const sdbus::Error&) {
      } catch (const std::exception&) {
      }
      // Device not found in list or failed to read message path — full refresh
      auto now = std::chrono::steady_clock::now();
      if (now - lastRefreshDebounce_ >= debounce_ms()) {
        lastRefreshDebounce_ = now;
        refresh_locked();
      }
    }
  });
}

void BluezService::refresh_locked() {
   
  snap_ = {};
  if (!objmgr_) return;

  ManagedObjects objs;
  try {
    objmgr_->callMethod("GetManagedObjects").onInterface(kObjMgr).storeResultsTo(objs);
  } catch (const sdbus::Error&) {
    return;
  }

  std::optional<sdbus::ObjectPath> adapterPath;
  for (const auto& [path, ifaces] : objs) {
    auto it = ifaces.find(kAdapter);
    if (it == ifaces.end()) continue;
    adapterPath = path;
    snap_.available = true;
    snap_.adapter_path = std::string(path);
    // Properties read from GetManagedObjects map below, with
    // Properties.Get fallback for booleans that may fail to deserialize
    // from nested variants in some sdbus-c++ versions.
    break;
  }

  if (!adapterPath) {
    if (on_change_) on_change_();
    return;
  }

  // Read Powered and Discovering from GetManagedObjects response first (avoids extra D-Bus calls)
  bool poweredRead = false, scanningRead = false;
  {
    auto adapterIt = objs.find(*adapterPath);
    if (adapterIt != objs.end()) {
      auto ifaceIt = adapterIt->second.find(kAdapter);
      if (ifaceIt != adapterIt->second.end()) {
        auto pit = ifaceIt->second.find("Powered");
        if (pit != ifaceIt->second.end()) {
          try { snap_.powered = pit->second.get<bool>(); poweredRead = true; } catch (...) {}
        }
        auto dit = ifaceIt->second.find("Discovering");
        if (dit != ifaceIt->second.end()) {
          try { snap_.scanning = dit->second.get<bool>(); scanningRead = true; } catch (...) {}
        }
      }
    }
  }

  // Fallback: direct Properties.Get for values that failed to deserialize from GetManagedObjects
  {
    if (!adapter_proxy_ || *adapterPath != cached_adapter_path_) {
      adapter_proxy_ = sdbus::createProxy(*bus_, kBluez, *adapterPath);
      cached_adapter_path_ = std::string(*adapterPath);
    }
    if (!poweredRead) {
      try {
        snap_.powered = adapter_proxy_->getProperty("Powered").onInterface(kAdapter).get<bool>();
      } catch (const sdbus::Error&) {
        snap_.powered = false;
      }
      // Fallback: some BlueZ versions expose PowerState as a string
      if (!snap_.powered) {
        try {
          snap_.powered = (adapter_proxy_->getProperty("PowerState").onInterface(kAdapter).get<std::string>() == "on");
        } catch (const sdbus::Error&) {}
      }
    }
    if (!scanningRead) {
      try {
        snap_.scanning = adapter_proxy_->getProperty("Discovering").onInterface(kAdapter).get<bool>();
      } catch (const sdbus::Error&) {
        snap_.scanning = false;
      }
    }
  }

  // rfkill caching: only re-read from sysfs every 2 seconds
  {
    auto now = std::chrono::steady_clock::now();
    if (now - lastRfkillCheck_ > std::chrono::seconds(2)) {
      lastRfkillCheck_ = now;
      cachedSoftBlocked_ = isRfkillSoftBlocked(RfkillDeviceType::Bluetooth);
      cachedHardBlocked_ = isRfkillHardBlocked(RfkillDeviceType::Bluetooth);
    }
    snap_.rfkillSoftBlocked = cachedSoftBlocked_;
    snap_.rfkillHardBlocked = cachedHardBlocked_;
  }

  int paired = 0;
  int connected = 0;
  std::vector<DeviceInfo> newDevices;
  try {
    for (const auto& [path, ifaces] : objs) {
      auto it = ifaces.find(kDevice);
      if (it == ifaces.end()) continue;
      const std::string p = std::string(path);
      if (p.rfind(snap_.adapter_path + "/", 0) != 0) continue;

      DeviceInfo d = make_device(path, it->second);

      // Check for battery
      auto batIt = ifaces.find(kBattery);
      if (batIt != ifaces.end()) {
        d.has_battery = true;
        d.battery_percent = get_or<uint8_t>(batIt->second, "Percentage", 0);
      }

      if (d.paired) paired++;
      if (d.connected) connected++;
      newDevices.push_back(std::move(d));
    }
  } catch (const std::exception&) {
    // Device parsing is best-effort; keep adapter state intact
  }
  snap_.paired_count = paired;
  snap_.connected_count = connected;
  snap_.connected = (snap_.powered && connected > 0);
  devices_ = std::move(newDevices);

  if (on_change_) on_change_();
}

void BluezService::connect_device(const std::string& path) {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (!started_) return;
  try {
    auto dev = sdbus::createProxy(*bus_, kBluez, sdbus::ObjectPath{path});
    dev->callMethod("Connect").onInterface(kDevice);
  } catch (const sdbus::Error&) {
  }
}

void BluezService::disconnect_device(const std::string& path) {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (!started_) return;
  try {
    auto dev = sdbus::createProxy(*bus_, kBluez, sdbus::ObjectPath{path});
    dev->callMethod("Disconnect").onInterface(kDevice);
  } catch (const sdbus::Error&) {
  }
}

void BluezService::forget_device(const std::string& path) {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (!started_ || snap_.adapter_path.empty()) return;
  try {
    if (!adapter_proxy_) {
      adapter_proxy_ = sdbus::createProxy(*bus_, kBluez, sdbus::ObjectPath{snap_.adapter_path});
      cached_adapter_path_ = snap_.adapter_path;
    }
    adapter_proxy_->callMethod("RemoveDevice").onInterface(kAdapter).withArguments(sdbus::ObjectPath{path});
  } catch (const sdbus::Error&) {
  }
}

void BluezService::pair_device(const std::string& path) {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (!started_) return;
  try {
    auto dev = sdbus::createProxy(*bus_, kBluez, sdbus::ObjectPath{path});
    dev->callMethod("Pair").onInterface(kDevice);
  } catch (const sdbus::Error&) {
  }
}

void BluezService::set_device_trust(const std::string& path, bool trusted) {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (!started_) return;
  try {
    auto dev = sdbus::createProxy(*bus_, kBluez, sdbus::ObjectPath{path});
    dev->setProperty("Trusted").onInterface(kDevice).toValue(trusted);
  } catch (const sdbus::Error&) {
  }
}

// Helper functions.

const char* bluetooth_device_kind_name(BluetoothDeviceKind kind) {
   
  switch (kind) {
    case BluetoothDeviceKind::Headset: return "Headset";
    case BluetoothDeviceKind::Headphones: return "Headphones";
    case BluetoothDeviceKind::Earbuds: return "Earbuds";
    case BluetoothDeviceKind::Speaker: return "Speaker";
    case BluetoothDeviceKind::Microphone: return "Microphone";
    case BluetoothDeviceKind::Mouse: return "Mouse";
    case BluetoothDeviceKind::Keyboard: return "Keyboard";
    case BluetoothDeviceKind::Phone: return "Phone";
    case BluetoothDeviceKind::Computer: return "Computer";
    case BluetoothDeviceKind::Gamepad: return "Gamepad";
    case BluetoothDeviceKind::Watch: return "Watch";
    case BluetoothDeviceKind::Tv: return "TV";
    default: return "Device";
  }
}

const char* bluetooth_device_kind_glyph(BluetoothDeviceKind kind) {
   
  switch (kind) {
    case BluetoothDeviceKind::Headset:
    case BluetoothDeviceKind::Headphones:
    case BluetoothDeviceKind::Earbuds:
    case BluetoothDeviceKind::Speaker:
      return "speaker";
    case BluetoothDeviceKind::Microphone: return "mic";
    case BluetoothDeviceKind::Mouse: return "mouse";
    case BluetoothDeviceKind::Keyboard: return "keyboard";
    case BluetoothDeviceKind::Phone: return "smartphone";
    case BluetoothDeviceKind::Computer: return "computer";
    case BluetoothDeviceKind::Gamepad: return "sports_esports";
    case BluetoothDeviceKind::Watch: return "watch";
    case BluetoothDeviceKind::Tv: return "tv";
    default: return "bluetooth";
  }
}

} // namespace eh::bt
