#pragma once

#include "desktop_shell/widgets/clock/clock_paint.hpp"
#include "desktop_shell/widgets/weather/weather_paint.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"
#include "desktop_shell/widgets/media/media_paint.hpp"
#include "desktop_shell/widgets/workspaces/workspaces_paint.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "desktop_shell/widgets/battery/battery_paint.hpp"
#include "desktop_shell/widgets/bluetooth/bluetooth_paint.hpp"
#include "desktop_shell/widgets/world_clock/world_clock_paint.hpp"

namespace eh::shell::dock_slot_hooks {

using eh::widgets::ControlCenterMixerStream;

using eh::widgets::clock_tick_signature_changed;
using eh::widgets::world_clock_tick_signature_changed;
using eh::widgets::control_center_tick_signature_changed;

using eh::widgets::control_center_audio_input_state;
using eh::widgets::control_center_audio_output_state;
using eh::widgets::control_center_bluetooth_state;
using eh::widgets::control_center_input_devices;
using eh::widgets::control_center_input_mixer_streams;
using eh::widgets::control_center_mixer_streams;
using eh::widgets::control_center_network_state;
using eh::widgets::control_center_output_devices;
using eh::widgets::control_center_set_audio_input_mute;
using eh::widgets::control_center_set_audio_input_volume;
using eh::widgets::control_center_set_audio_output_mute;
using eh::widgets::control_center_set_audio_output_volume;
using eh::widgets::control_center_set_default_sink;
using eh::widgets::control_center_set_default_source;
using eh::widgets::control_center_set_input_mixer_stream_volume;
using eh::widgets::control_center_set_mixer_stream_volume;
using eh::widgets::control_center_weather_async_handle_wake;
using eh::widgets::control_center_weather_async_init;
using eh::widgets::control_center_weather_async_register_redraw;
using eh::widgets::control_center_weather_async_wake_fd;
using eh::widgets::control_center_weather_drive_curl_multi;
using eh::widgets::control_center_weather_service_start;
using eh::widgets::control_center_weather_service_stop;
using eh::widgets::control_center_weather_startup_dock;
using eh::widgets::control_center_weather_state;
using eh::widgets::control_center_wifi_connect;
using eh::widgets::control_center_wifi_scan;

using eh::widgets::dock_clock_slot_width;
using eh::widgets::dock_weather_slot_width;
using eh::widgets::dock_control_center_slot_width;
using eh::widgets::dock_media_slot_width;
using eh::widgets::dock_workspaces_slot_width;
using eh::widgets::dock_battery_slot_width;
using eh::widgets::dock_bluetooth_slot_width;
using eh::widgets::dock_world_clock_slot_width;

using eh::widgets::paint_clock_slot;
using eh::widgets::paint_weather_slot;
using eh::widgets::paint_control_center_audio_input_card;
using eh::widgets::paint_control_center_audio_output_card;
using eh::widgets::paint_control_center_bluetooth_card;
using eh::widgets::paint_control_center_network_card;
using eh::widgets::paint_control_center_slot;
using eh::widgets::paint_media_slot;
using eh::widgets::paint_workspaces_slot;
using eh::widgets::paint_battery_slot;
using eh::widgets::paint_bluetooth_slot;
using eh::widgets::paint_world_clock_slot;

using eh::widgets::workspace_activate_entry;
using eh::widgets::workspaces_pick_index;
using eh::widgets::workspace_strip_poll;
using eh::widgets::battery_widget_init;
using eh::widgets::battery_widget_poll;
using eh::widgets::battery_widget_shutdown;
using eh::widgets::BluetoothDevice;
using eh::widgets::BluetoothDeviceKind;
using eh::widgets::BluetoothSnapshot;
using eh::widgets::bluetooth_connect_device;
using eh::widgets::bluetooth_device_kind_glyph;
using eh::widgets::bluetooth_disconnect_device;
using eh::widgets::bluetooth_ensure_service;
using eh::widgets::bluetooth_forget_device;
using eh::widgets::bluetooth_devices;
using eh::widgets::bluetooth_pair_device;
using eh::widgets::bluetooth_snapshot;
using eh::widgets::bluetooth_start_discovery;
using eh::widgets::bluetooth_stop_discovery;
using eh::widgets::bluetooth_widget_init;
using eh::widgets::bluetooth_widget_poll;
using eh::widgets::bluetooth_widget_needs_immediate_draw;
using eh::widgets::bluetooth_widget_shutdown;

namespace slot_pill {
using eh::widgets::slot_pill_style::corner_radius;
using eh::widgets::slot_pill_style::set_fill_for_state;
using eh::widgets::slot_pill_style::stroke_pill_after_fill_preserve;
}

}
