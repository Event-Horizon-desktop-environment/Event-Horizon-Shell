#pragma once

#include <string>
#include <vector>

namespace eh::shell::dock::control_center {

struct ControlCenterState;

struct ControlCenterMixerStream {
  int sink_input_id = -1;
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

[[nodiscard]] std::vector<ControlCenterMixerStream> control_center_mixer_streams(const ControlCenterState* cs = nullptr);
[[nodiscard]] std::vector<ControlCenterMixerStream> control_center_input_mixer_streams(const ControlCenterState* cs = nullptr);
void control_center_set_mixer_stream_volume(int sink_input_id, double normalized_0_1);
void control_center_set_input_mixer_stream_volume(int source_output_id, double normalized_0_1);

} // namespace eh::shell::dock::control_center
