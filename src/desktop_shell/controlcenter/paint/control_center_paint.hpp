#pragma once

#include <functional>
#include <string_view>
#include <vector>
#include <string>

#include <cairo/cairo.h>

#include "desktop_shell/controlcenter/widgets/network/control_center_network_widget.hpp"
#include "desktop_shell/controlcenter/widgets/bluetooth/control_center_bluetooth_widget.hpp"
#include "desktop_shell/controlcenter/widgets/audio/control_center_audio_widget.hpp"
#include "desktop_shell/controlcenter/widgets/mixer/control_center_mixer_widget.hpp"
#include "desktop_shell/controlcenter/widgets/weather/control_center_weather_widget.hpp"

namespace eh::config {
struct ShellConfig;
}

namespace eh::shell::dock::control_center {

struct ControlCenterState;

[[nodiscard]] bool widget_list_contains_control_center(const eh::config::ShellConfig& sc,
                                                       const std::vector<std::string>& widgets);

[[nodiscard]] double dock_control_center_slot_width(const eh::config::ShellConfig& sc, std::string_view instance_id,
                                                    double icon_ref_px, double bar_height);

[[nodiscard]] bool paint_control_center_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                                             double x, double y, double slot_w, double slot_h, double icon_ref_px,
                                             bool hovered, bool pressed);

[[nodiscard]] bool control_center_tick_signature_changed(int& io_signature, const eh::config::ShellConfig& sc,
                                                         std::string_view instance_id);
}

namespace eh::widgets {
using eh::shell::dock::control_center::ControlCenterMixerStream;
using eh::shell::dock::control_center::control_center_audio_input_state;
using eh::shell::dock::control_center::control_center_audio_output_state;
using eh::shell::dock::control_center::control_center_bluetooth_state;
using eh::shell::dock::control_center::control_center_input_devices;
using eh::shell::dock::control_center::control_center_input_mixer_streams;
using eh::shell::dock::control_center::control_center_mixer_streams;
using eh::shell::dock::control_center::control_center_network_state;
using eh::shell::dock::control_center::control_center_output_devices;
using eh::shell::dock::control_center::control_center_set_audio_input_mute;
using eh::shell::dock::control_center::control_center_set_audio_input_volume;
using eh::shell::dock::control_center::control_center_set_audio_output_mute;
using eh::shell::dock::control_center::control_center_set_audio_output_volume;
using eh::shell::dock::control_center::control_center_set_default_sink;
using eh::shell::dock::control_center::control_center_set_default_source;
using eh::shell::dock::control_center::control_center_set_input_mixer_stream_volume;
using eh::shell::dock::control_center::control_center_set_mixer_stream_volume;
using eh::shell::dock::control_center::control_center_tick_signature_changed;
using eh::shell::dock::control_center::control_center_weather_async_handle_wake;
using eh::shell::dock::control_center::control_center_weather_async_init;
using eh::shell::dock::control_center::control_center_weather_async_register_redraw;
using eh::shell::dock::control_center::control_center_weather_async_wake_fd;
using eh::shell::dock::control_center::control_center_weather_drive_curl_multi;
using eh::shell::dock::control_center::control_center_weather_service_start;
using eh::shell::dock::control_center::control_center_weather_service_stop;
using eh::shell::dock::control_center::control_center_weather_startup_dock;
using eh::shell::dock::control_center::ControlCenterWeatherState;
using eh::shell::dock::control_center::control_center_weather_state;
using eh::shell::dock::control_center::control_center_wifi_connect;
using eh::shell::dock::control_center::control_center_wifi_scan;
using eh::shell::dock::control_center::dock_control_center_slot_width;
using eh::shell::dock::control_center::paint_control_center_audio_input_card;
using eh::shell::dock::control_center::paint_control_center_audio_output_card;
using eh::shell::dock::control_center::paint_control_center_bluetooth_card;
using eh::shell::dock::control_center::paint_control_center_network_card;
using eh::shell::dock::control_center::paint_control_center_slot;
using eh::shell::dock::control_center::widget_list_contains_control_center;
}
