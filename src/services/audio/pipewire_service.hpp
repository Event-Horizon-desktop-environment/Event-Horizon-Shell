#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct pw_proxy;
struct pw_metadata;
struct spa_hook;

namespace eh::audio {

struct Device {
  std::uint32_t node_id = 0;
  std::string name{};
  std::string display_name{};
  bool is_default = false;
  bool muted = false;
  int volume_pct = 0;
};

struct Stream {
  std::uint32_t node_id = 0;
  int volume_pct = 0;
  bool muted = false;
  std::string app_name{};
  std::string icon_name{};
  std::string app_id{};

  std::string process_binary{};
  std::string process_path{};

  std::string node_name{};

  std::string description{};
};

struct Snapshot {
  bool available = false;
  std::uint32_t default_sink = 0;
  std::uint32_t default_source = 0;
  std::vector<Device> sinks{};
  std::vector<Device> sources{};
  std::vector<Stream> output_streams{};
  std::vector<Stream> input_streams{};
};

[[nodiscard]] bool pw_link_list_capture_stdout(std::string* out_blob);
[[nodiscard]] std::string pw_link_list_playback_sink_for_stream(const std::string& link_blob,
                                                                const std::string& stream_node_name);
[[nodiscard]] std::string pw_link_list_capture_source_for_stream(const std::string& link_blob,
                                                                 const std::string& stream_node_name);

struct EngineSettings {
  bool available = false;
  int clock_rate_hz = 48000;
  int clock_force_rate_hz = 0;
  std::vector<int> allowed_rates_hz{};
};

struct CompatDefaultSinkFormat {
  bool available = false;
  std::uint32_t sink_index = 0;
  std::string sample_spec{};
  std::string format_encoding{};
  int channels = 2;
  int rate_hz = 48000;
};

struct BluetoothCardProfileOption {
  std::string key{};
  std::string description{};
};

struct BluetoothCardProfiles {
  std::uint32_t card_index = 0;
  std::string card_name{};
  std::string active_profile_key{};
  std::vector<BluetoothCardProfileOption> profiles{};
};

class PipeWireService {
public:
  using ChangeCallback = std::function<void()>;

  static PipeWireService& instance();

  void start();
  [[nodiscard]] bool started() const;
  void set_change_callback(ChangeCallback cb);

  [[nodiscard]] Snapshot snapshot() const;

  [[nodiscard]] EngineSettings query_engine_settings() const;

  void apply_engine_clock_rate_hz(int hz);

  void apply_engine_force_rate_hz(int hz);

  void apply_engine_allowed_rates_hz(const std::vector<int>& rates_hz);

  [[nodiscard]] CompatDefaultSinkFormat query_compat_default_sink_format() const;

  void apply_compat_default_sink_pcm_format(int choice_0_3);

  [[nodiscard]] std::vector<BluetoothCardProfiles> query_bluetooth_cards() const;
  void apply_bluetooth_card_profile(std::uint32_t card_index, const std::string& profile_key);

  void set_node_volume(std::uint32_t node_id, double normalized_0_1);
  void set_node_mute(std::uint32_t node_id, bool muted);
  void set_default_sink(std::uint32_t node_id);
  void set_default_source(std::uint32_t node_id);

  void route_playback_stream_to_sink(std::uint32_t stream_node_id, std::uint32_t sink_node_id);

  void route_capture_stream_to_source(std::uint32_t stream_node_id, std::uint32_t source_node_id);

  [[nodiscard]] std::string playback_route_sink_name(std::uint32_t stream_node_id) const;

  [[nodiscard]] std::string capture_route_source_name(std::uint32_t stream_node_id) const;

  struct SavedDefaults {
    std::string default_sink_name;
    std::string default_source_name;
    int default_sink_volume_pct = 100;
    bool default_sink_muted = false;
    int default_source_volume_pct = 100;
    bool default_source_muted = false;
    int engine_clock_rate_hz = 48000;
    int engine_force_rate_hz = 0;
    std::vector<int> engine_allowed_rates_hz;
    int compat_pcm_format = 0;
  };

  void apply_saved_defaults(const SavedDefaults& defaults);

private:
  PipeWireService();
  ~PipeWireService();
  PipeWireService(const PipeWireService&) = delete;
  PipeWireService& operator=(const PipeWireService&) = delete;
  PipeWireService(PipeWireService&&) = delete;
  PipeWireService& operator=(PipeWireService&&) = delete;

  void init_locked();
  void refresh_default_nodes_locked();
  void rebuild_snapshot_locked();
  void emit_change_locked();

  void sync_and_wait_locked();

  mutable std::recursive_mutex mtx_{};
  bool started_ = false;
  ChangeCallback on_change_{};

  pw_thread_loop* loop_ = nullptr;
  pw_context* ctx_ = nullptr;
  pw_core* core_ = nullptr;
  pw_registry* reg_ = nullptr;
  pw_metadata* metadata_ = nullptr;
  spa_hook* reg_hook_ = nullptr;
  spa_hook* core_hook_ = nullptr;

  struct ClientInfo {
    std::string app_name{};
    std::string app_id{};
    std::string process_binary{};
    std::string process_path{};
    std::string icon_name{};
  };

  struct NodeState {
    std::uint32_t id = 0;
    pw_proxy* proxy = nullptr;
    spa_hook* hook = nullptr;
    void* listener_data = nullptr;
    std::string name{};
    std::string media_class{};
    std::string description{};
    std::string nick{};
    std::string app_name{};
    std::string app_id{};
    std::string process_binary{};
    std::string process_path{};
    std::string icon_name{};
    std::uint32_t client_id = 0;
    bool muted = false;
    int volume_pct = 0;
    int channels = 2;

    NodeState() = default;
    NodeState(const NodeState&) = delete;
    NodeState& operator=(const NodeState&) = delete;
    NodeState(NodeState&&) = default;
    NodeState& operator=(NodeState&&) = default;
  };

  std::unordered_map<std::uint32_t, NodeState> nodes_{};
  std::unordered_map<std::uint32_t, ClientInfo> clients_{};
  std::uint32_t default_sink_ = 0;
  std::uint32_t default_source_ = 0;

  Snapshot snap_{};
};

}
