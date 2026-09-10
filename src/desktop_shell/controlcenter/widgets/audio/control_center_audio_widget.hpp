#pragma once

#include <string>
#include <vector>

#include <cairo/cairo.h>

namespace eh::shell::dock::control_center {

struct ControlCenterState;

struct ControlCenterAudioOutputState {
  bool muted = false;
  int volume_pct = 0;
  double volume_fill_t_override = -1.0;
  std::string device_name{};
};

struct ControlCenterOutputDevice {
  std::string sink_name{};
  std::string display_name{};
  bool is_default = false;
};

struct ControlCenterAudioInputState {
  bool muted = false;
  int volume_pct = 0;
  double volume_fill_t_override = -1.0;
  std::string device_name{};
};

struct ControlCenterInputDevice {
  std::string source_name{};
  std::string display_name{};
  bool is_default = false;
};

[[nodiscard]] ControlCenterAudioOutputState control_center_audio_output_state(const ControlCenterState* cs = nullptr);
[[nodiscard]] std::vector<ControlCenterOutputDevice> control_center_output_devices();
void paint_control_center_audio_output_card(cairo_t* cr, double x, double y, double w, double h,
                                            const ControlCenterAudioOutputState& as, double inner_glass_scale);
void control_center_set_audio_output_volume(double normalized_0_1);
void control_center_set_audio_output_mute(bool muted);
void control_center_set_default_sink(std::string_view sink_name);

[[nodiscard]] ControlCenterAudioInputState control_center_audio_input_state(const ControlCenterState* cs = nullptr);
[[nodiscard]] std::vector<ControlCenterInputDevice> control_center_input_devices();
void paint_control_center_audio_input_card(cairo_t* cr, double x, double y, double w, double h,
                                           const ControlCenterAudioInputState& as, double inner_glass_scale);
void control_center_set_audio_input_volume(double normalized_0_1);
void control_center_set_audio_input_mute(bool muted);
void control_center_set_default_source(std::string_view source_name);

} // namespace eh::shell::dock::control_center
