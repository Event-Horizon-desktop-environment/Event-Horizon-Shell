#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sdbus {
class IConnection;
class IProxy;
class Message;
}

namespace eh::bt {
class BluetoothAgent;

enum class BluetoothPeerKind {
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
  BluetoothPeerKind kind = BluetoothPeerKind::Unknown;
  bool paired = false;
  bool trusted = false;
  bool connected = false;
  bool connecting = false;
  bool has_rssi = false;
  int16_t rssi = 0;
  bool has_battery = false;
  uint8_t battery_percent = 0;
};

struct BluetoothSummary {
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
  BluetoothSummary snap;
  std::vector<DeviceInfo> devices;
};

class BluezService {
public:
  using StateChangeHook = std::function<void()>;

  static BluezService& instance();

  void start();
  void start_discovery();
  void stop_discovery();
  void set_powered(bool on);
  void refresh();
  void set_change_callback(StateChangeHook cb);

  [[nodiscard]] BluetoothSummary snapshot() const;
  [[nodiscard]] std::vector<DeviceInfo> devices() const;
  [[nodiscard]] FullState full_state() const;

  // Device operations
  void connect_device(const std::string& path);
  void disconnect_device(const std::string& path);
  void forget_device(const std::string& path);
  void pair_device(const std::string& path);
  void set_device_trust(const std::string& path, bool trusted);

  // Auto-reconnect: while enabled (and the adapter is powered and not
  // scanning) the service pages paired devices back when they are in range
  // (RSSI present) or have been connected before (persisted across restarts), with
  // a per-device cooldown. Devices the user disconnected manually are left
  // alone for a few minutes to honour explicit disconnects.
  void set_auto_reconnect(bool enabled);
  [[nodiscard]] bool auto_reconnect() const noexcept;

  [[nodiscard]] std::string last_connected_address() const;
  [[nodiscard]] std::string last_error() const;
  void clear_error();

  // Main-loop integration (eventfd wakeup, same pattern as
  // NetworkManagerService): kicked on every state change alongside
  // on_change_, so poll-driven loops (settings standalone/embed, shell
  // supervisor) wake promptly even when no input fd has events. Without this,
  // BlueZ signals arriving on the sdbus worker thread would sit unseen until
  // the user happens to move the pointer.
  [[nodiscard]] int wake_fd() const;
  void handle_wake();
  // Runs a full re-enumeration iff an event/worker thread flagged one needed
  // (throttled; no-op otherwise). Called from UI-thread paint/poll paths —
  // sync D-Bus must never run on the sdbus event thread or op workers.
  void refresh_if_needed();

private:
  BluezService();
  ~BluezService();
  BluezService(const BluezService&) = delete;
  BluezService& operator=(const BluezService&) = delete;
  BluezService(BluezService&&) = delete;
  BluezService& operator=(BluezService&&) = delete;

  void refresh_locked();
  void bind_signals_locked();
  void on_bluez_properties_changed(sdbus::Message msg);
  void mark_connected_locked(const std::string& address);
  void settle_on_disconnected(const std::string& address);
  void auto_reconnect_worker();
  void attempt_auto_reconnects();
  void set_connecting_locked(const std::string& path, bool connecting);
  void set_last_error_locked(std::string msg);
  void load_persisted_state_locked();
  void save_persisted_state_locked();
  [[nodiscard]] std::string persisted_state_path() const;
  void run_device_method_async(std::string path, std::string method, bool mark_connecting);
  void run_adapter_method_async(std::string method, std::string device_path);
  void run_set_powered_async(bool on);
  // Flag a re-enumeration + wake loops. Used wherever a refresh used to run
  // inline on threads that must never block in D-Bus (event thread, workers).
  // Caller need not hold mtx_.
  void mark_refresh_needed();
  // Invoke on_change_ and kick wake_fd_. Caller must hold mtx_.
  void fire_change_locked();

  mutable std::recursive_mutex mtx_{};
  bool started_ = false;
  StateChangeHook on_change_{};

  // Auto-reconnect state. `everConnected_` and `reconnectCooldown_` are
  // guarded by mtx_; `autoReconnect_` is atomic so the worker thread can read
  // it without taking the lock.
  std::atomic<bool> autoReconnect_{true};
  std::unordered_set<std::string> everConnected_{};
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> reconnectCooldown_{};
  // Last device that completed a successful Connect (persisted to disk so
  // auto-reconnect truly remembers across restarts, not just this process).
  int wake_fd_ = -1;
  // Set by event/worker threads instead of refreshing inline (they must not
  // block in D-Bus); consumed by refresh_if_needed() on UI threads.
  bool refresh_needed_ = false;
  std::chrono::steady_clock::time_point last_dirty_refresh_{};
  // Paces eventfd wakeups during signal storms (LE scans); on_change_ still
  // fires immediately every time.
  std::chrono::steady_clock::time_point last_wake_kick_{};
  std::string last_connected_address_{};
  // Last async D-Bus failure for UI display (Connect/Pair/... never block).
  std::string last_error_{};
  std::chrono::steady_clock::time_point last_error_time_{};

  // Worker thread lifecycle.
  std::thread worker_{};
  std::mutex worker_mtx_{};
  std::condition_variable worker_cv_{};
  bool stopWorker_ = false;

  // rfkill cache.
  mutable std::chrono::steady_clock::time_point lastRfkillCheck_{};
  mutable bool cachedSoftBlocked_ = false;
  mutable bool cachedHardBlocked_ = false;

  std::unique_ptr<sdbus::IConnection> bus_{};
  // Dedicated connection for Start/StopDiscovery. Discovery sessions are owned
  // by the issuing D-Bus client, so the starter must outlive the scan; this
  // object lives as long as the service and is touched ONLY by discovery
  // workers (serialized by discovery_mtx_), so no connection is ever used
  // from two threads at once.
  std::unique_ptr<sdbus::IConnection> discovery_bus_{};
  std::mutex discovery_mtx_{};
  std::unique_ptr<sdbus::IProxy> objmgr_{};
  std::unique_ptr<sdbus::IProxy> adapter_proxy_{};
  std::string cached_adapter_path_{};
  // BlueZ pairing agent. Never wired up before (dead code) — without it every
  // SSP confirmation fails with "No agent available / Operation not permitted"
  // and the D-Bus side surfaces AuthenticationFailed. NoInputNoOutput keeps
  // headphone JustWorks pairing prompt-free in every process.
  std::unique_ptr<BluetoothAgent> agent_;

  BluetoothSummary snap_{};
  std::vector<DeviceInfo> devices_{};
};

const char* bluetooth_device_kind_name(BluetoothPeerKind kind);
const char* bluetooth_device_kind_glyph(BluetoothPeerKind kind);

}
