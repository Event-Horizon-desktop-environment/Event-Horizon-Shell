#pragma once

// Single source of truth for the dashboard's geometry.
//
// dashboard_compute_layout() is a pure function of (cached config, cached
// service snapshots, surface width). Paint runs it and stores the result on
// DashboardState::layout; hit-testing and dispatch only ever read that stored
// copy. Configure/input-region/sizing may call it directly — that is still one
// shared implementation, so nothing can drift (Docs/hit-testing.md).

#include "desktop_shell/dashboard/dashboard_types.hpp"
#include "desktop_shell/controlcenter/widgets/network/control_center_network_widget.hpp"
#include "desktop_shell/widgets/bluetooth/bluetooth_paint.hpp"

#include <string>
#include <string_view>
#include <vector>

struct DockApp;

namespace eh::shell::dashboard {

[[nodiscard]] DashCardKind dash_kind_from_id(std::string_view id);

// True when the configured widget list contains `id` ("clock", "media", ...).
[[nodiscard]] bool dashboard_has_card(const DockApp& app, std::string_view id);

// Mixer row geometry, derived from the card box alone so paint and layout
// agree without re-measuring each other.
struct DashMixerGeom {
  double nameX = 0.0;
  double nameBudget = 0.0;
  double trackX = 0.0;
  double trackW = 0.0;
};
[[nodiscard]] DashMixerGeom dashboard_mixer_geom(double cardX, double cardW);

[[nodiscard]] DashboardLayout dashboard_compute_layout(DockApp& app, double surfaceW);
[[nodiscard]] double dashboard_desired_height(DockApp& app, double surfaceW);

// Row sources shared by layout/paint/hit/dispatch (single order everywhere):
// wifi APs with the active network first, bluetooth with connected devices
// first, so neither is buried past the 6-row cap by scan spam.
[[nodiscard]] std::vector<eh::shell::dock::control_center::ControlCenterWifiAp> dashboard_sorted_wifi_aps();
[[nodiscard]] std::vector<eh::widgets::BluetoothDevice> dashboard_sorted_bt_devices();

// Compact audio (volume/mic) row geometry from the card box alone: eyebrow
// label on top, then icon + flat bar + percent. Shared paint/hit.
struct DashAudioGeom {
  double muteX = 0.0, muteY = 0.0, muteS = 28.0;  // mute hit box
  double iconCX = 0.0, iconCY = 0.0;              // glyph center
  double trackX = 0.0, trackY = 0.0, trackW = 0.0, trackH = 3.0;
  double hitX = 0.0, hitY = 0.0, hitW = 0.0, hitH = 0.0;
  double pctRightX = 0.0, pctBase = 0.0;  // percent label right edge + baseline
};
[[nodiscard]] DashAudioGeom dashboard_audio_geom(const CardRect& c);

// Media progress bar geometry from the card box alone (shared paint/hit).
// The bar lives in the text column; transport glyphs cluster right.
struct DashMediaProgressGeom {
  double hitX = 0.0, hitY = 0.0, hitW = 0.0, hitH = 0.0;
  double trackX = 0.0, trackY = 0.0, trackW = 0.0, trackH = 0.0;
};
[[nodiscard]] DashMediaProgressGeom dashboard_media_progress_geom(const CardRect& c);
// Media transport row: vertical center shared by art/text/buttons.
[[nodiscard]] double dashboard_media_row_cy(const CardRect& c);
[[nodiscard]] double dashboard_media_btn_cx(const CardRect& c, int idx);

// Clock text honouring [time] (use24h / showSeconds / showDate / dateFormat /
// customFormat / timezone). Never mutates the process TZ.
struct DashClockText {
  std::string time{};
  std::string date{};
  std::string zone{};
};
[[nodiscard]] DashClockText dashboard_clock_text(const eh::config::ShellConfig& sc);

// Samples /proc + /sys into the dashboard state (>=1s cache).
void dashboard_sample_system(DockApp& app);

// Weather widget instance: explicit id, else the first weather widget in the
// dock layout, else the control-center instance.
[[nodiscard]] std::string dashboard_weather_instance_id(const DockApp& app);

}  // namespace eh::shell::dashboard
