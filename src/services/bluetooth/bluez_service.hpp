#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sdbus {
class IConnection;
class IProxy;
}

namespace eh::bt {

enum class BluetoothDeviceKind {
  Unknown,
  Headset,
  Headphones,
  Earbuds,
  Speaker,
  Microphone,
  Mouse,
  Keyboard,
  Phone,
  Computer,
  Gamepad,
  Watch,
  Tv,
};

struct DeviceInfo {
  std::string path;
  std::string address;
  std::string alias;
  BluetoothDeviceKind kind = BluetoothDeviceKind::Unknown;
  bool paired = false;
  bool trusted = false;
  bool connected = false;
  bool connecting = false;
  bool has_rssi = false;
  int16_t rssi = 0;
  bool has_battery = false;
  uint8_t battery_percent = 0;
};

struct Snapshot {
  bool available = false;
  bool powered = false;
  bool rfkillSoftBlocked = false;
  bool rfkillHardBlocked = false;
  bool connected = false;
  bool scanning = false;
  int paired_count = 0;
  int connected_count = 0;
  std::string adapter_path{};
};

struct FullState {
  Snapshot snap;
  std::vector<DeviceInfo> devices;
};

class BluezService {
public:
  using ChangeCallback = std::function<void()>;

  static BluezService& instance();

  void start();
  void start_discovery();
  void stop_discovery();
  void set_powered(bool on);
  void refresh();
  void set_change_callback(ChangeCallback cb);

  [[nodiscard]] Snapshot snapshot() const;
  [[nodiscard]] std::vector<DeviceInfo> devices() const;
  [[nodiscard]] FullState full_state() const;

  // Device operations
  void connect_device(const std::string& path);
  void disconnect_device(const std::string& path);
  void forget_device(const std::string& path);
  void pair_device(const std::string& path);
  void set_device_trust(const std::string& path, bool trusted);

private:
  BluezService();
  ~BluezService();
  BluezService(const BluezService&) = delete;
  BluezService& operator=(const BluezService&) = delete;
  BluezService(BluezService&&) = delete;
  BluezService& operator=(BluezService&&) = delete;

  void refresh_locked();
  void bind_signals_locked();

  mutable std::mutex mtx_{};
  bool started_ = false;
  ChangeCallback on_change_{};
  std::chrono::steady_clock::time_point lastRefreshDebounce_{};

  // rfkill cache.
  mutable std::chrono::steady_clock::time_point lastRfkillCheck_{};
  mutable bool cachedSoftBlocked_ = false;
  mutable bool cachedHardBlocked_ = false;

  std::unique_ptr<sdbus::IConnection> bus_{};
  std::unique_ptr<sdbus::IProxy> objmgr_{};
  std::unique_ptr<sdbus::IProxy> adapter_proxy_{};
  std::string cached_adapter_path_{};

  Snapshot snap_{};
  std::vector<DeviceInfo> devices_{};
};

const char* bluetooth_device_kind_name(BluetoothDeviceKind kind);
const char* bluetooth_device_kind_glyph(BluetoothDeviceKind kind);

}
