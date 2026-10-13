#include "services/bluetooth/bluez_service.hpp"

#include "services/bluetooth/bluetooth_agent.hpp"
#include "configuration/shell_config.hpp"
#include "platform/radio_block.h"
#include "desktop_shell/common/log/debug_log.hpp"

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Message.h>
#include <sdbus-c++/Types.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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

// Serializes Pair/Connect D-Bus calls across processes. Every shell process
// (dock/taskbar/stage/settings) runs its own BluezService + auto-reconnect
// worker, so a manual Pair in settings can otherwise collide with a background
// Connect from another process on the same device — BlueZ answers the loser
// with AuthenticationRejected (seen 12:06:40). Manual ops take the lock
// blocking; auto-reconnect takes it non-blocking and skips on contention.
std::string bt_op_lock_path() {
  std::string toml;
  try {
    toml = eh::config::state_component_toml_path("bluetooth");
  } catch (...) {
    toml.clear();
  }
  if (toml.empty()) {
    const char* xdg = std::getenv("XDG_STATE_HOME");
    std::string base;
    if (xdg && xdg[0])
      base = std::string(xdg) + "/event-horizon";
    else if (const char* h = std::getenv("HOME"))
      base = std::string(h) + "/.local/state/event-horizon";
    else
      base = "/tmp/event-horizon";
    toml = base + "/bluetooth/bluetooth.toml";
  }
  const auto slash = toml.find_last_of('/');
  const std::string dir =
      (slash == std::string::npos) ? toml : toml.substr(0, slash);
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir + "/bt-op.lock";
}

int acquire_bt_op_lock(bool blocking) {
  const std::string path = bt_op_lock_path();
  const int fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
  if (fd < 0) return -1;
  const int op = LOCK_EX | (blocking ? 0 : LOCK_NB);
  if (::flock(fd, op) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

void release_bt_op_lock(int fd) {
  if (fd < 0) return;
  ::flock(fd, LOCK_UN);
  ::close(fd);
}

struct BtFreshState {
  bool ok = false;
  bool paired = false;
  bool connected = false;
  std::string address;
};

// Best-effort fresh read straight from BlueZ (no cached state, no mtx_).
// Lets a worker correct a stale UI decision (Pair vs Connect) and lets a
// failed op detect "connected anyway" instead of reporting an error.
BtFreshState read_bt_device_fresh(const std::string& path) {
  BtFreshState st;
  try {
    auto bus = sdbus::createSystemBusConnection();
    auto dev = sdbus::createProxy(*bus, kBluez, sdbus::ObjectPath{path});
    try {
      st.paired = dev->getProperty("Paired").onInterface(kDevice).get<bool>();
    } catch (...) {
    }
    try {
      st.connected =
          dev->getProperty("Connected").onInterface(kDevice).get<bool>();
    } catch (...) {
    }
    try {
      st.address =
          dev->getProperty("Address").onInterface(kDevice).get<std::string>();
    } catch (...) {
    }
    st.ok = true;
  } catch (...) {
  }
  return st;
}

}

BluezService& BluezService::instance() {
   
  static BluezService s{};
  return s;
}

BluezService::BluezService() = default;
BluezService::~BluezService() {
  {
    std::lock_guard<std::mutex> lock(worker_mtx_);
    stopWorker_ = true;
  }
  worker_cv_.notify_all();
  if (worker_.joinable()) worker_.join();
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  if (wake_fd_ >= 0) {
    close(wake_fd_);
    wake_fd_ = -1;
  }
  {
    std::lock_guard<std::mutex> dlock(discovery_mtx_);
    discovery_bus_.reset();
  }
}

void BluezService::start() {
   
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  if (started_) return;
  try {
    bus_ = sdbus::createSystemBusConnection();
    objmgr_ = sdbus::createProxy(*bus_, kBluez, kBluezRoot);
    autoReconnect_.store(eh::config::shell_config_snapshot().bluetooth.autoReconnect,
                         std::memory_order_relaxed);
    load_persisted_state_locked();
    if (wake_fd_ < 0) {
      wake_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
      if (wake_fd_ < 0) std::cerr << "[bluetooth] eventfd creation failed\n";
    }
    {
      std::lock_guard<std::mutex> dlock(discovery_mtx_);
      if (!discovery_bus_) {
        try {
          discovery_bus_ = sdbus::createSystemBusConnection();
        } catch (const std::exception& e) {
          debug_log("bluetooth", "discovery bus FAILED: %s", e.what());
        }
      }
    }
    bind_signals_locked();
    refresh_locked();
    try {
      agent_ = std::make_unique<BluetoothAgent>(*bus_);
      debug_log("bluetooth", "agent registered (NoInputNoOutput)");
    } catch (const std::exception& e) {
      debug_log("bluetooth", "agent FAILED: %s", e.what());
    }
    // Pairing is impossible while the adapter itself is not pairable
    // (bluetoothctl showed `Pairable: no`). Best-effort, off-thread.
    {
      std::string adapter = snap_.adapter_path;
      std::thread([adapter]() {
        try {
          auto bus = sdbus::createSystemBusConnection();
          auto proxy = sdbus::createProxy(*bus, kBluez, sdbus::ObjectPath{adapter});
          proxy->setProperty("Pairable").onInterface(kAdapter).toValue(true);
        } catch (...) {
        }
      }).detach();
    }
    bus_->enterEventLoopAsync();
    worker_ = std::thread([this]() { auto_reconnect_worker(); });
  } catch (const std::exception& e) {
    debug_log("bluetooth", "start FAILED: %s", e.what());
    bus_.reset();
    objmgr_.reset();
    return;
  }
  started_ = true;
  debug_log("bluetooth", "service started (auto_reconnect=%d)",
            autoReconnect_.load(std::memory_order_relaxed) ? 1 : 0);
}

void BluezService::start_discovery() {
  run_adapter_method_async("StartDiscovery", {});
}

void BluezService::stop_discovery() {
  run_adapter_method_async("StopDiscovery", {});
}

void BluezService::set_powered(bool on) {
  // Async: BlueZ SetPowered can block for seconds (adapter init); never hold
  // the UI thread or mtx_ across the D-Bus round-trip (DE freeze).
  run_set_powered_async(on);
}

void BluezService::refresh() {
   
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  if (started_ && objmgr_) refresh_locked();
}

void BluezService::set_change_callback(ChangeCallback cb) {
   
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  on_change_ = std::move(cb);
}

Snapshot BluezService::snapshot() const {
   
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  return snap_;
}

std::vector<DeviceInfo> BluezService::devices() const {
    
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  return devices_;
}

FullState BluezService::full_state() const {
    
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  return {snap_, devices_};
}

void BluezService::bind_signals_locked() {
    
  if (!objmgr_) return;

  objmgr_->uponSignal("InterfacesAdded").onInterface(kObjMgr).call(
      [this](const sdbus::ObjectPath& path,
                          const std::map<std::string, IfaceProps>& interfaces) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto devIt = interfaces.find(kDevice);
    if (devIt != interfaces.end() && !snap_.adapter_path.empty() &&
        std::string(path).rfind(snap_.adapter_path + "/", 0) == 0) {
      DeviceInfo d = make_device(path, devIt->second);
      auto batIt = interfaces.find(kBattery);
      if (batIt != interfaces.end()) {
        d.has_battery = true;
        d.battery_percent = get_or<uint8_t>(batIt->second, "Percentage", 0);
      }
      debug_log("bluetooth", "signal InterfacesAdded dev=%s alias='%s' paired=%d connected=%d",
                  d.path.c_str(), d.alias.c_str(), d.paired ? 1 : 0, d.connected ? 1 : 0);
      if (d.connected) mark_connected_locked(d.address);
      bool found = false;
      for (auto& existing : devices_) {
        if (existing.path == d.path) {
          d.connecting = existing.connecting && !d.connected;
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
      fire_change_locked();
    } else {
      // Non-device change: flag a re-enumeration for UI threads instead of
      // blocking this event thread in D-Bus.
      mark_refresh_needed();
    }
  });

  objmgr_->uponSignal("InterfacesRemoved").onInterface(kObjMgr).call(
      [this](const sdbus::ObjectPath& path,
                          const std::vector<std::string>& ) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    bool removed = false;
    for (auto it = devices_.begin(); it != devices_.end(); ++it) {
      if (it->path == std::string(path)) {
        devices_.erase(it);
        removed = true;
        break;
      }
    }
    if (removed) {
      debug_log("bluetooth", "signal InterfacesRemoved dev=%s", std::string(path).c_str());
      snap_.paired_count = 0;
      snap_.connected_count = 0;
      for (const auto& dev : devices_) {
        if (dev.paired) snap_.paired_count++;
        if (dev.connected) snap_.connected_count++;
      }
      snap_.connected = (snap_.powered && snap_.connected_count > 0);
      fire_change_locked();
    } else {
      mark_refresh_needed();
    }
  });

  // Live PropertiesChanged from adapters/devices/batteries. This MUST be a
  // connection-level match: proxy signal subscriptions are pinned to the
  // proxy's own object path ("/" here), so the previous
  // objmgr_->uponSignal("PropertiesChanged") form silently dropped every
  // adapter/device signal and live updates never happened. path_namespace
  // covers the whole BlueZ tree with one rule; InterfacesAdded/Removed above
  // stay on the proxy (they are emitted from "/" itself).
  try {
    const std::string match =
        std::string("type='signal',sender='org.bluez',interface='") + kProps +
        "',member='PropertiesChanged',path_namespace='/org/bluez'";
    bus_->addMatch(match, [this](sdbus::Message msg) {
      on_bluez_properties_changed(std::move(msg));
    });
  } catch (const sdbus::Error& e) {
    debug_log("bluetooth", "PropertiesChanged match FAILED: %s", e.what());
  } catch (const std::exception& e) {
    debug_log("bluetooth", "PropertiesChanged match FAILED: %s", e.what());
  }
}


void BluezService::on_bluez_properties_changed(sdbus::Message msg) {
  std::string iface;
  std::map<std::string, sdbus::Variant> props;
  std::vector<std::string> invalidated;
  std::string path;
  try {
    msg >> iface >> props >> invalidated;
    path = msg.getPath();
  } catch (...) {
    return;
  }
  if (iface != kAdapter && iface != kDevice && iface != kBattery) return;
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  if (iface == kAdapter) {
    // Only track our adapter; extra controllers are ignored like elsewhere.
    if (!snap_.adapter_path.empty() && path != snap_.adapter_path) return;
    const bool was_powered = snap_.powered;
    const bool was_scanning = snap_.scanning;
    auto pit = props.find("Powered");
    if (pit != props.end()) {
      try { snap_.powered = pit->second.get<bool>(); } catch (...) {}
    }
    auto dit = props.find("Discovering");
    if (dit != props.end()) {
      try { snap_.scanning = dit->second.get<bool>(); } catch (...) {}
    }
    if (snap_.powered != was_powered)
      debug_log("bluetooth", "adapter Powered=%d", snap_.powered ? 1 : 0);
    if (snap_.scanning != was_scanning)
      debug_log("bluetooth", "adapter Discovering=%d", snap_.scanning ? 1 : 0);
    fire_change_locked();
    return;
  }
  bool known = false;
  for (auto& dev : devices_) {
    if (dev.path != path) continue;
    known = true;
    if (iface == kDevice) {
      auto it = props.find("Connected");
      if (it != props.end()) {
        const bool was = dev.connected;
        try {
          dev.connected = it->second.get<bool>();
        } catch (...) {
        }
        if (dev.connected) {
          dev.connecting = false;
          debug_log("bluetooth", "device Connected alias='%s' addr=%s",
                    dev.alias.c_str(), dev.address.c_str());
          mark_connected_locked(dev.address);
        } else if (was) {
          dev.connecting = false;
          debug_log("bluetooth", "device Disconnected alias='%s' addr=%s",
                    dev.alias.c_str(), dev.address.c_str());
        }
      }
      it = props.find("Paired");
      if (it != props.end()) {
        try { dev.paired = it->second.get<bool>(); } catch (...) {}
      }
      it = props.find("Trusted");
      if (it != props.end()) {
        try { dev.trusted = it->second.get<bool>(); } catch (...) {}
      }
      it = props.find("RSSI");
      if (it != props.end()) {
        try { dev.has_rssi = true; dev.rssi = it->second.get<int16_t>(); } catch (...) {}
      }
      it = props.find("Alias");
      if (it != props.end()) {
        try { dev.alias = it->second.get<std::string>(); } catch (...) {}
      }
      it = props.find("Name");
      if (it != props.end() && dev.alias.empty()) {
        try { dev.alias = it->second.get<std::string>(); } catch (...) {}
      }
      it = props.find("Icon");
      if (it != props.end()) {
        try {
          std::string icon = it->second.get<std::string>();
          dev.kind = classify_icon(icon);
          if (dev.kind == BluetoothDeviceKind::Unknown) {
            auto cit = props.find("Class");
            if (cit != props.end()) {
              uint32_t cod = cit->second.get<uint32_t>();
              dev.kind = classify_class(cod);
            }
          }
        } catch (...) {}
      }
      it = props.find("Class");
      if (it != props.end() && dev.kind == BluetoothDeviceKind::Unknown) {
        try {
          uint32_t cod = it->second.get<uint32_t>();
          dev.kind = classify_class(cod);
        } catch (...) {}
      }
      // Recalculate counts
      snap_.paired_count = 0;
      snap_.connected_count = 0;
      for (const auto& d : devices_) {
        if (d.paired) snap_.paired_count++;
        if (d.connected) snap_.connected_count++;
      }
      snap_.connected = (snap_.powered && snap_.connected_count > 0);
      fire_change_locked();
      return;
    }
    // org.bluez.Battery1 lives on the device object path itself.
    auto pit = props.find("Percentage");
    if (pit != props.end()) {
      try {
        dev.battery_percent = pit->second.get<uint8_t>();
        dev.has_battery = true;
      } catch (...) {}
    }
    fire_change_locked();
    return;
  }
  if (!known) {
    // Signal for a device we haven't enumerated — flag it; the next UI-thread
    // refresh_if_needed() picks it up. Never block this event thread in D-Bus.
    mark_refresh_needed();
  }
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
    fire_change_locked();
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

  // radio-block cache: only re-read from sysfs every 2 seconds
  {
    auto now = std::chrono::steady_clock::now();
    if (now - lastRfkillCheck_ > std::chrono::seconds(2)) {
      lastRfkillCheck_ = now;
      const auto radio = eh::platform::radioBlockStatus(eh::platform::RadioKind::Bluetooth);
      cachedSoftBlocked_ = radio.softBlocked;
      cachedHardBlocked_ = radio.hardBlocked;
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
      if (d.connected) {
        connected++;
        mark_connected_locked(d.address);
      }
      newDevices.push_back(std::move(d));
    }
  } catch (const std::exception&) {
    // Device parsing is best-effort; keep adapter state intact
  }
  snap_.paired_count = paired;
  snap_.connected_count = connected;
  snap_.connected = (snap_.powered && connected > 0);
  // Preserve in-flight `connecting` flags: refresh rebuilds the vector from
  // scratch, which would otherwise clear the spinner the async Connect/Pair
  // path just set (and the UI would flicker back to idle mid-operation).
  {
    std::unordered_set<std::string> still_connecting;
    for (const auto& d : devices_)
      if (d.connecting && !d.connected) still_connecting.insert(d.path);
    if (!still_connecting.empty()) {
      for (auto& d : newDevices) {
        if (!d.connected && still_connecting.count(d.path)) d.connecting = true;
      }
    }
  }
  devices_ = std::move(newDevices);
  debug_log("bluetooth", "refresh adapter=%s powered=%d scanning=%d paired=%d connected=%d total=%d",
            snap_.adapter_path.c_str(), snap_.powered ? 1 : 0, snap_.scanning ? 1 : 0,
            snap_.paired_count, snap_.connected_count, static_cast<int>(devices_.size()));
  fire_change_locked();
}

void BluezService::connect_device(const std::string& path) {
  // Async: org.bluez.Device1.Connect routinely blocks 5-30s. The old code ran
  // it synchronously while holding mtx_, so every paint (full_state) blocked
  // behind it and the whole DE froze. Now we flag `connecting` for instant UI
  // feedback and do the D-Bus call on a detached thread.
  run_device_method_async(path, "Connect", true);
}

void BluezService::disconnect_device(const std::string& path) {
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    for (const auto& d : devices_) {
      if (d.path == path) {
        settle_on_disconnected(d.address);
        break;
      }
    }
  }
  run_device_method_async(path, "Disconnect", false);
}

void BluezService::forget_device(const std::string& path) {
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (!started_) return;
    for (const auto& d : devices_) {
      if (d.path == path && !d.address.empty()) {
        everConnected_.erase(d.address);
        reconnectCooldown_.erase(d.address);
        if (last_connected_address_ == d.address) last_connected_address_.clear();
        save_persisted_state_locked();
        break;
      }
    }
  }
  run_adapter_method_async("RemoveDevice", path);
}

void BluezService::pair_device(const std::string& path) {
  // Pairing can block on agent interaction; same async treatment as Connect.
  run_device_method_async(path, "Pair", true);
}

void BluezService::set_device_trust(const std::string& path, bool trusted) {
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (!started_) return;
  }
  std::thread([this, path, trusted]() {
    debug_log("bluetooth", "request Trusted=%d %s", trusted ? 1 : 0, path.c_str());
    try {
      auto bus = sdbus::createSystemBusConnection();
      auto dev = sdbus::createProxy(*bus, kBluez, sdbus::ObjectPath{path});
      dev->setProperty("Trusted").onInterface(kDevice).toValue(trusted);
      debug_log("bluetooth", "Trusted=%d %s ok", trusted ? 1 : 0, path.c_str());
    } catch (const std::exception& e) {
      debug_log("bluetooth", "Trusted %s FAILED: %s", path.c_str(), e.what());
    } catch (...) {
    }
    mark_refresh_needed();
  }).detach();
}

void BluezService::set_auto_reconnect(bool enabled) {
  autoReconnect_.store(enabled, std::memory_order_relaxed);
}

bool BluezService::auto_reconnect() const noexcept {
  return autoReconnect_.load(std::memory_order_relaxed);
}

// Caller must hold mtx_.
void BluezService::mark_connected_locked(const std::string& address) {
  if (address.empty()) return;
  // A live connection moots any prior Pair/Connect error banner (the 12:06:40
  // race showed "Pair failed: AuthenticationRejected" while Connected=1).
  if (!last_error_.empty()) last_error_.clear();
  bool changed = false;
  if (everConnected_.insert(address).second) changed = true;
  if (last_connected_address_ != address) {
    last_connected_address_ = address;
    changed = true;
  }
  // A freshly connected device is no longer in a cooldown; a later drop can be
  // retried promptly.
  reconnectCooldown_.erase(address);
  // Persist so "remember last device" survives restarts instead of living only
  // in this process' memory. Writes only when something actually changed (this
  // runs on every refresh while connected, so unconditional writes would churn
  // the disk every few seconds).
  if (changed) {
    debug_log("bluetooth", "remembered %s (last-connected, ever=%zu)",
              address.c_str(), everConnected_.size());
    save_persisted_state_locked();
  }
}

// Caller must hold mtx_. Pushes the next auto-reconnect attempt for this device
// well into the future so an explicit disconnect sticks.
void BluezService::settle_on_disconnected(const std::string& address) {
  if (address.empty()) return;
  reconnectCooldown_[address] = std::chrono::steady_clock::now() + std::chrono::minutes(5);
}

void BluezService::auto_reconnect_worker() {
  std::unique_lock<std::mutex> lock(worker_mtx_);
  while (!stopWorker_) {
    worker_cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return stopWorker_; });
    if (stopWorker_) break;
    lock.unlock();
    attempt_auto_reconnects();
    lock.lock();
  }
}

void BluezService::attempt_auto_reconnects() {
  if (!autoReconnect_.load(std::memory_order_relaxed)) return;

  struct Candidate {
    std::string path;
    std::string address;
  };
  std::vector<Candidate> candidates;
  std::string last;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (!started_ || !bus_ || !snap_.available || !snap_.powered || snap_.scanning) return;
    last = last_connected_address_;
    const auto now = std::chrono::steady_clock::now();
    for (const auto& d : devices_) {
      if (!d.paired || d.connected || d.connecting || d.address.empty()) continue;
      // Only nudge devices that are currently visible (RSSI present) or have
      // been connected before (persisted ever-connected set). This avoids
      // paging paired-but-never-used devices forever.
      if (!d.has_rssi && everConnected_.count(d.address) == 0) continue;
      auto it = reconnectCooldown_.find(d.address);
      if (it != reconnectCooldown_.end() && now < it->second) continue;
      candidates.push_back({d.path, d.address});
    }
    if (!candidates.empty())
      debug_log("bluetooth", "auto-reconnect: %zu candidate(s) last=%s",
                candidates.size(), last.empty() ? "-" : last.c_str());
    // The last-used device connects first so it wins the race when several
    // remembered devices are in range.
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](const Candidate& a, const Candidate& b) {
                       const bool a_last = (!last.empty() && a.address == last);
                       const bool b_last = (!last.empty() && b.address == last);
                       return a_last && !b_last;
                     });
  }

  for (const auto& c : candidates) {
    // A manual Pair/Connect in another process holds this lock for the whole
    // D-Bus round-trip; skip instead of colliding into AuthenticationRejected.
    const int lock_fd = acquire_bt_op_lock(false);
    if (lock_fd < 0) {
      debug_log("bluetooth", "auto-reconnect %s skipped (op in progress)",
                c.address.c_str());
      continue;
    }
    // Cached state may be stale: re-read before paging. Another process may
    // have just connected/paired it.
    const BtFreshState fresh = read_bt_device_fresh(c.path);
    if (fresh.ok && (fresh.connected || !fresh.paired)) {
      release_bt_op_lock(lock_fd);
      const auto after = std::chrono::steady_clock::now();
      std::lock_guard<std::recursive_mutex> lock(mtx_);
      if (fresh.connected && !c.address.empty()) mark_connected_locked(c.address);
      reconnectCooldown_[c.address] = after + std::chrono::seconds(60);
      continue;
    }
    std::string attempt_err;
    try {
      // Fresh connection per attempt: never touch the shared bus_/objmgr_
      // proxies from this worker thread.
      auto bus = sdbus::createSystemBusConnection();
      auto dev = sdbus::createProxy(*bus, kBluez, sdbus::ObjectPath{c.path});
      dev->callMethod("Connect").onInterface(kDevice).withTimeout(std::chrono::seconds(15));
    } catch (const sdbus::Error& e) {
      attempt_err = e.what();
    } catch (const std::exception& e) {
      attempt_err = e.what();
    }
    release_bt_op_lock(lock_fd);
    if (attempt_err.empty())
      debug_log("bluetooth", "auto-reconnect %s ok", c.address.c_str());
    else
      debug_log("bluetooth", "auto-reconnect %s FAILED: %s", c.address.c_str(),
                attempt_err.c_str());
    // One attempt per minute per device keeps the radio quiet while a device
    // is out of range, but reconnects promptly once it comes back.
    const auto after = std::chrono::steady_clock::now();
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    reconnectCooldown_[c.address] = after + std::chrono::seconds(60);
  }
}

std::string BluezService::persisted_state_path() const {
  try {
    return eh::config::state_component_toml_path("bluetooth");
  } catch (...) {
  }
  const char* xdg = std::getenv("XDG_STATE_HOME");
  std::string base;
  if (xdg && xdg[0])
    base = std::string(xdg) + "/event-horizon";
  else if (const char* h = std::getenv("HOME"))
    base = std::string(h) + "/.local/state/event-horizon";
  else
    base = "/tmp/event-horizon";
  return base + "/bluetooth/bluetooth.toml";
}

void BluezService::load_persisted_state_locked() {
  everConnected_.clear();
  last_connected_address_.clear();
  const std::string path = persisted_state_path();
  std::ifstream f(path);
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    const size_t s = line.find_first_not_of(" \t\r\n");
    if (s == std::string::npos || line[s] == '#') continue;
    if (line.compare(s, 14, "last_connected") == 0) {
      const size_t q1 = line.find('"', s);
      const size_t q2 = (q1 == std::string::npos) ? std::string::npos : line.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
        const std::string addr = line.substr(q1 + 1, q2 - q1 - 1);
        if (!addr.empty()) {
          last_connected_address_ = addr;
          everConnected_.insert(addr);
        }
      }
    } else if (line.compare(s, 14, "ever_connected") == 0) {
      size_t pos = s;
      while (true) {
        const size_t q1 = line.find('"', pos);
        if (q1 == std::string::npos) break;
        const size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        const std::string addr = line.substr(q1 + 1, q2 - q1 - 1);
        if (!addr.empty() && addr.find(':') != std::string::npos) everConnected_.insert(addr);
        pos = q2 + 1;
      }
    }
  }
  debug_log("bluetooth", "memory loaded from %s: last=%s ever=%zu",
            persisted_state_path().c_str(),
            last_connected_address_.empty() ? "-" : last_connected_address_.c_str(),
            everConnected_.size());
}

void BluezService::save_persisted_state_locked() {
  const std::string path = persisted_state_path();
  try {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
      std::ofstream f(tmp, std::ios::trunc);
      if (!f) return;
      f << "# Event Horizon Bluetooth memory (auto-generated)\n";
      f << "last_connected = \"" << last_connected_address_ << "\"\n";
      f << "ever_connected = [";
      bool first = true;
      for (const auto& a : everConnected_) {
        if (!first) f << ", ";
        f << "\"" << a << "\"";
        first = false;
      }
      f << "]\n";
      f.flush();
      if (!f) return;
    }
    std::error_code rec;
    std::filesystem::rename(tmp, path, rec);
    debug_log("bluetooth", "memory saved to %s: last=%s ever=%zu", path.c_str(),
              last_connected_address_.empty() ? "-" : last_connected_address_.c_str(),
              everConnected_.size());
  } catch (...) {
  }
}

void BluezService::set_connecting_locked(const std::string& path, bool connecting) {
  for (auto& d : devices_) {
    if (d.path == path) {
      d.connecting = connecting;
      return;
    }
  }
}

void BluezService::set_last_error_locked(std::string msg) {
  last_error_ = std::move(msg);
  last_error_time_ = std::chrono::steady_clock::now();
}

std::string BluezService::last_connected_address() const {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  return last_connected_address_;
}

std::string BluezService::last_error() const {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  // Errors older than 2 minutes are stale; report empty so the UI clears.
  if (!last_error_.empty() &&
      std::chrono::steady_clock::now() - last_error_time_ > std::chrono::minutes(2))
    return {};
  return last_error_;
}

void BluezService::clear_error() {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  last_error_.clear();
}

void BluezService::fire_change_locked() {
  const auto now = std::chrono::steady_clock::now();
  if (wake_fd_ >= 0 && now - last_wake_kick_ >= std::chrono::milliseconds(250)) {
    last_wake_kick_ = now;
    uint64_t v = 1;
    (void)write(wake_fd_, &v, sizeof(v));
  }
  if (on_change_) on_change_();
}

void BluezService::mark_refresh_needed() {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  refresh_needed_ = true;
  fire_change_locked();
}

void BluezService::refresh_if_needed() {
  bool do_it = false;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    const auto now = std::chrono::steady_clock::now();
    if (refresh_needed_ && now - last_dirty_refresh_ >= std::chrono::milliseconds(500)) {
      refresh_needed_ = false;
      last_dirty_refresh_ = now;
      do_it = true;
    }
  }
  if (do_it) refresh();
}

int BluezService::wake_fd() const {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  return wake_fd_;
}

void BluezService::handle_wake() {
  int fd = -1;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    fd = wake_fd_;
  }
  if (fd >= 0) {
    // Drain the counter; concurrent fires coalesce into one wakeup.
    uint64_t v = 0;
    while (read(fd, &v, sizeof(v)) == static_cast<ssize_t>(sizeof(v))) {
    }
  }
}

void BluezService::run_device_method_async(std::string path, std::string method, bool mark_connecting) {
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (!started_) return;
    debug_log("bluetooth", "request %s %s", method.c_str(), path.c_str());
    if (mark_connecting) {
      for (auto& d : devices_) {
        if (d.path == path) {
          if (d.connected || d.connecting) return;  // already there / in flight
          d.connecting = true;
          break;
        }
      }
      last_error_.clear();
      fire_change_locked();
    }
  }
  std::thread([this, path = std::move(path), method = std::move(method), mark_connecting]() {
    std::string err;
    std::string addr;
    std::string effective = method;
    {
      std::lock_guard<std::recursive_mutex> lock(mtx_);
      for (const auto& d : devices_) {
        if (d.path == path) {
          addr = d.address;
          break;
        }
      }
    }
    // Serialize with auto-reconnect workers in other processes (blocking:
    // this is already off the UI thread and shows a spinner).
    const int lock_fd = acquire_bt_op_lock(true);
    // Paging while a scan is active destabilizes many adapters (the 12:26:23
    // Pair succeeded with scanning=1, then dropped 6s later). Stop the scan
    // first; it is cheap to restart from the UI.
    {
      std::string adapter;
      {
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        adapter = snap_.adapter_path;
      }
      if (!adapter.empty() && (effective == "Pair" || effective == "Connect")) {
        std::lock_guard<std::mutex> dlock(discovery_mtx_);
        if (discovery_bus_) {
          try {
            auto proxy =
                sdbus::createProxy(*discovery_bus_, kBluez, sdbus::ObjectPath{adapter});
            proxy->callMethod("StopDiscovery")
                .onInterface(kAdapter)
                .withTimeout(std::chrono::seconds(5));
            debug_log("bluetooth", "StopDiscovery before %s ok", effective.c_str());
          } catch (const std::exception& e) {
            debug_log("bluetooth", "StopDiscovery before %s: %s", effective.c_str(),
                      e.what());
          }
        }
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        if (snap_.scanning) {
          snap_.scanning = false;
          fire_change_locked();
        }
      }
    }
    // Correct stale UI decisions against live BlueZ state before paging.
    {
      const BtFreshState fresh = read_bt_device_fresh(path);
      if (fresh.ok) {
        if (!fresh.address.empty()) addr = fresh.address;
        if (effective == "Pair" && fresh.paired) {
          debug_log("bluetooth", "%s %s already paired -> Connect instead",
                    method.c_str(), path.c_str());
          effective = "Connect";
        }
        if (effective == "Connect" && fresh.connected) {
          debug_log("bluetooth", "%s %s already connected -> ok", method.c_str(),
                    path.c_str());
          release_bt_op_lock(lock_fd);
          std::lock_guard<std::recursive_mutex> lock(mtx_);
          if (mark_connecting) set_connecting_locked(path, false);
          if (!addr.empty()) mark_connected_locked(addr);
          fire_change_locked();
          mark_refresh_needed();
          return;
        }
      }
    }
    try {
      // Fresh bus per operation: the shared bus_/objmgr_ proxies stay on the
      // service/event-loop threads, and the UI thread never blocks here.
      auto bus = sdbus::createSystemBusConnection();
      if (effective == "Pair") {
        // Belt and braces: pairing is rejected while the adapter itself is
        // not pairable (seen `Pairable: no` + `No agent available`).
        try {
          std::string adapter;
          {
            std::lock_guard<std::recursive_mutex> lock(mtx_);
            adapter = snap_.adapter_path;
          }
          if (!adapter.empty()) {
            auto ap = sdbus::createProxy(*bus, kBluez, sdbus::ObjectPath{adapter});
            ap->setProperty("Pairable").onInterface(kAdapter).toValue(true);
          }
        } catch (...) {
        }
      }
      auto dev = sdbus::createProxy(*bus, kBluez, sdbus::ObjectPath{path});
      dev->callMethod(effective).onInterface(kDevice).withTimeout(std::chrono::seconds(25));
    } catch (const sdbus::Error& e) {
      err = e.what();
    } catch (const std::exception& e) {
      err = e.what();
    }
    // A failed op can still leave a usable bond (cross-process Pair vs
    // Connect race). Only suppress the error when BlueZ shows a real bond:
    // Connected AND Paired. Connected-alone with Paired=no is the classic
    // "headphones not in pairing mode" pattern (brief link, then drop ~2s
    // later) and must keep its error banner.
    if (!err.empty() && (effective == "Pair" || effective == "Connect")) {
      const BtFreshState after = read_bt_device_fresh(path);
      if (after.ok && after.connected && after.paired) {
        debug_log("bluetooth", "%s %s failed but device bonded+connected -> ok",
                  effective.c_str(), path.c_str());
        err.clear();
        if (!after.address.empty()) addr = after.address;
      }
    }
    release_bt_op_lock(lock_fd);
    {
      std::lock_guard<std::recursive_mutex> lock(mtx_);
      if (mark_connecting) set_connecting_locked(path, false);
      debug_log("bluetooth", "%s %s %s", effective.c_str(), path.c_str(),
                  err.empty() ? "ok" : ("FAILED: " + err).c_str());
      if (!err.empty()) {
        set_last_error_locked(effective + " failed: " + err);
        // Back off this device for a bit so auto-reconnect doesn't hammer it.
        if (!addr.empty())
          reconnectCooldown_[addr] = std::chrono::steady_clock::now() + std::chrono::seconds(60);
      } else if (effective == "Connect" && !addr.empty()) {
        mark_connected_locked(addr);
      }
      fire_change_locked();
    }
    if (err.empty() && effective == "Pair") {
      // Best-effort: trust freshly paired devices so later auto-reconnects and
      // incoming connections work without another prompt.
      try {
        auto bus = sdbus::createSystemBusConnection();
        auto dev = sdbus::createProxy(*bus, kBluez, sdbus::ObjectPath{path});
        dev->setProperty("Trusted").onInterface(kDevice).toValue(true);
      } catch (...) {
      }
    }
    // State sync arrives via signals; the flag covers anything missed. Workers
    // must not block in D-Bus on shared connections.
    mark_refresh_needed();
  }).detach();
}

void BluezService::run_adapter_method_async(std::string method, std::string device_path) {
  std::string adapter;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (!started_ || snap_.adapter_path.empty()) return;
    adapter = snap_.adapter_path;
    if (method == "StartDiscovery" && !snap_.scanning) {
      snap_.scanning = true;
      fire_change_locked();
    } else if (method == "StopDiscovery" && snap_.scanning) {
      snap_.scanning = false;
      fire_change_locked();
    }
    debug_log("bluetooth", "request %s adapter=%s%s", method.c_str(), adapter.c_str(),
              device_path.empty() ? "" : (" dev=" + device_path).c_str());
  }
  const bool discovery_session = (method == "StartDiscovery" || method == "StopDiscovery");
  std::thread([this, adapter = std::move(adapter), method = std::move(method),
               device_path = std::move(device_path), discovery_session]() {
    std::string err;
    try {
      if (discovery_session) {
        // Discovery sessions are owned by the issuing D-Bus client: BlueZ
        // kills the scan when that client disconnects, so a throwaway per-call
        // connection suicides the scan on thread exit. discovery_bus_ lives as
        // long as the service and is used ONLY here (serialized), so sessions
        // survive and no connection is ever touched by two threads at once.
        // Still off the UI thread, so no DE freeze.
        std::lock_guard<std::mutex> dlock(discovery_mtx_);
        if (!discovery_bus_) throw sdbus::Error(sdbus::Error::Name{"org.bluez.Error.Failed"},
                                                "no discovery bus");
        auto proxy = sdbus::createProxy(*discovery_bus_, kBluez, sdbus::ObjectPath{adapter});
        proxy->callMethod(method).onInterface(kAdapter).withTimeout(std::chrono::seconds(10));
      } else {
        auto bus = sdbus::createSystemBusConnection();
        auto proxy = sdbus::createProxy(*bus, kBluez, sdbus::ObjectPath{adapter});
        proxy->callMethod("RemoveDevice")
            .onInterface(kAdapter)
            .withArguments(sdbus::ObjectPath{device_path})
            .withTimeout(std::chrono::seconds(10));
      }
    } catch (const sdbus::Error& e) {
      err = e.what();
    } catch (const std::exception& e) {
      err = e.what();
    }
    debug_log("bluetooth", "%s %s", method.c_str(), err.empty() ? "ok" : ("FAILED: " + err).c_str());
    if (!err.empty()) {
      std::lock_guard<std::recursive_mutex> lock(mtx_);
      set_last_error_locked(method + " failed: " + err);
      fire_change_locked();
    }
    mark_refresh_needed();
  }).detach();
}

void BluezService::run_set_powered_async(bool on) {
  std::string adapter;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (!started_ || snap_.adapter_path.empty()) return;
    adapter = snap_.adapter_path;
    debug_log("bluetooth", "request Power %s", on ? "on" : "off");
    snap_.powered = on;  // optimistic; PropertiesChanged/refresh corrects it
    last_error_.clear();
    fire_change_locked();
  }
  std::thread([this, adapter = std::move(adapter), on]() {
    std::string err;
    try {
      auto bus = sdbus::createSystemBusConnection();
      auto proxy = sdbus::createProxy(*bus, kBluez, sdbus::ObjectPath{adapter});
      proxy->setProperty("Powered").onInterface(kAdapter).toValue(on);
    } catch (const sdbus::Error& e) {
      err = e.what();
    } catch (const std::exception& e) {
      err = e.what();
    }
    debug_log("bluetooth", "Power %s %s", on ? "on" : "off",
                err.empty() ? "ok" : ("FAILED: " + err).c_str());
    {
      std::lock_guard<std::recursive_mutex> lock(mtx_);
      if (!err.empty())
        set_last_error_locked(std::string("Power ") + (on ? "on" : "off") + " failed: " + err);
      fire_change_locked();
    }
    mark_refresh_needed();
  }).detach();
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
    // One distinct Material Symbols Rounded ligature per kind (all verified
    // present in the vendored font via hb-shape). Audio devices must NOT
    // collapse: A2DP sinks are headphones, HSP/HFP voice links are headset_mic,
    // LE buds are earbuds — only actual speakers get speaker.
    case BluetoothDeviceKind::Headset: return "headset_mic";
    case BluetoothDeviceKind::Headphones: return "headphones";
    case BluetoothDeviceKind::Earbuds: return "earbuds";
    case BluetoothDeviceKind::Speaker: return "speaker";
    case BluetoothDeviceKind::Microphone: return "mic";
    case BluetoothDeviceKind::Mouse: return "mouse";
    case BluetoothDeviceKind::Keyboard: return "keyboard";
    case BluetoothDeviceKind::Phone: return "smartphone";
    case BluetoothDeviceKind::Computer: return "computer";
    case BluetoothDeviceKind::Gamepad: return "gamepad";
    case BluetoothDeviceKind::Watch: return "watch";
    case BluetoothDeviceKind::Tv: return "tv";
    default: return "bluetooth";
  }
}

} // namespace eh::bt
