#pragma once

#include "desktop_shell/controlcenter/state/control_center_state.hpp"

#include <cstdint>

struct DockApp;

enum class CcAudioSliderKind : uint8_t { Output = 0, Input = 1 };

struct CcAudioSliderLayout {
  double track_x = 0;
  double track_y = 0;
  double track_w = 0;
  double track_h = 0;
  double hit_x = 0;
  double hit_y = 0;
  double hit_w = 0;
  double hit_h = 0;
};

struct CcHitContext {
  double popupW;
  int popupH;
  const eh::shell::dock::control_center::ControlCenterState& state;
};

// Shared hit helpers (take CcHitContext — usable from both dock and taskbar)
[[nodiscard]] bool control_center_audio_slider_layout(const CcHitContext& ctx, CcAudioSliderKind kind, CcAudioSliderLayout* out);
[[nodiscard]] bool control_center_audio_slider_hit(const CcHitContext& ctx, CcAudioSliderKind kind, double px, double py, double* out_t);
[[nodiscard]] bool control_center_audio_mute_icon_hit(const CcHitContext& ctx, CcAudioSliderKind kind, double px, double py);
[[nodiscard]] bool control_center_network_card_hit(const CcHitContext& ctx, double px, double py);
[[nodiscard]] bool control_center_bluetooth_card_hit(const CcHitContext& ctx, double px, double py);
[[nodiscard]] bool control_center_audio_card_hit(const CcHitContext& ctx, CcAudioSliderKind kind, double px, double py);
[[nodiscard]] bool control_center_network_row_hit(const CcHitContext& ctx, double px, double py, int* out_index);
[[nodiscard]] bool control_center_output_devices_row_hit(const CcHitContext& ctx, double px, double py, int* out_index);
[[nodiscard]] bool control_center_input_devices_row_hit(const CcHitContext& ctx, double px, double py, int* out_index);
[[nodiscard]] bool control_center_mixer_slider_hit(const CcHitContext& ctx, int row, double px, double py, double* out_t);
[[nodiscard]] bool control_center_mixer_expanded_slider_hit(const CcHitContext& ctx, double px, double py, int* out_stream_id, bool* out_is_input, double* out_sx, double* out_sw, double* out_t);
[[nodiscard]] bool control_center_mixer_settings_hit(const CcHitContext& ctx, double px, double py);
[[nodiscard]] double control_center_media_card_y(const CcHitContext& ctx);
[[nodiscard]] int control_center_media_button_hit(const CcHitContext& ctx, double px, double py);
[[nodiscard]] bool control_center_weather_card_hit(const CcHitContext& ctx, double px, double py);
[[nodiscard]] bool control_center_bluetooth_row_hit(const CcHitContext& ctx, double px, double py, int* out_index);
[[nodiscard]] bool control_center_bluetooth_row_forget_hit(const CcHitContext& ctx, double px, double py, int* out_index);
[[nodiscard]] bool control_center_modal_backdrop_hit(const CcHitContext& ctx, double px, double py);
[[nodiscard]] bool control_center_modal_close_hit(const CcHitContext& ctx, double px, double py);

// Backward-compatible DockApp overloads (delegate to CcHitContext versions)
[[nodiscard]] bool control_center_audio_slider_layout(const DockApp& app, CcAudioSliderKind kind, CcAudioSliderLayout* out);

[[nodiscard]] bool control_center_audio_slider_geom(const DockApp& app, CcAudioSliderKind kind, double* out_sx, double* out_sy,
                                                     double* out_sw, double* out_sh);
[[nodiscard]] bool control_center_audio_slider_hit(const DockApp& app, CcAudioSliderKind kind, double px, double py, double* out_t);
[[nodiscard]] bool control_center_audio_mute_icon_hit(const DockApp& app, CcAudioSliderKind kind, double px, double py);
[[nodiscard]] bool control_center_network_card_hit(const DockApp& app, double px, double py);
[[nodiscard]] bool control_center_audio_card_hit(const DockApp& app, CcAudioSliderKind kind, double px, double py);
[[nodiscard]] bool control_center_network_row_hit(const DockApp& app, double px, double py, int* out_index);
[[nodiscard]] bool control_center_output_devices_row_hit(const DockApp& app, double px, double py, int* out_index);
[[nodiscard]] bool control_center_input_devices_row_hit(const DockApp& app, double px, double py, int* out_index);
[[nodiscard]] double control_center_audio_slider_value_from_x(const DockApp& app, CcAudioSliderKind kind, double px);
[[nodiscard]] int control_center_audio_pct_from_x(const DockApp& app, CcAudioSliderKind kind, double px);
[[nodiscard]] bool control_center_mixer_slider_geom(const DockApp& app, int row, double* out_sx, double* out_sy, double* out_sw,
                                                     double* out_sh);
[[nodiscard]] bool control_center_mixer_slider_hit(const DockApp& app, int row, double px, double py, double* out_t);
[[nodiscard]] bool control_center_mixer_expanded_slider_hit(const DockApp& app, double px, double py, int* out_stream_id, bool* out_is_input,
                                                             double* out_sx, double* out_sw, double* out_t);
[[nodiscard]] bool control_center_mixer_settings_hit(const DockApp& app, double px, double py);
[[nodiscard]] double control_center_media_card_y(const DockApp& app);
[[nodiscard]] int control_center_media_button_hit(const DockApp& app, double px, double py);
[[nodiscard]] bool control_center_weather_card_hit(const DockApp& app, double px, double py);

[[nodiscard]] bool control_center_bluetooth_card_hit(const DockApp& app, double px, double py);
[[nodiscard]] bool control_center_bluetooth_row_hit(const DockApp& app, double px, double py, int* out_index);
[[nodiscard]] bool control_center_bluetooth_row_forget_hit(const DockApp& app, double px, double py, int* out_index);

[[nodiscard]] bool control_center_modal_backdrop_hit(const DockApp& app, double px, double py);
[[nodiscard]] bool control_center_modal_close_hit(const DockApp& app, double px, double py);
