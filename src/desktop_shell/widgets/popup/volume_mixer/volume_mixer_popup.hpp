#pragma once

#include <cstdint>

typedef struct _cairo cairo_t;
struct DockApp;
namespace eh::config { struct ShellConfig; }
namespace eh::icons { class IconCache; }

namespace eh::shell::dock::popup::volume_mixer {

constexpr int kVolumeMixerPopupW = 600;
// Fixed popup height: header + master + devices + scrollable app viewport +
// engine footer always fit; the app list scrolls instead of growing.
constexpr int kVolumeMixerPopupH = 620;

// Content height for the current mixer state (devices + streams + expanded
// router + engine footer). Used at open time and after expand/collapse so the
// layer surface always fits the content. Clamped to [320, 760].
int volume_mixer_popup_content_height();

// Called when the popup opens: starts the PipeWire service, clears any stale
// slider drag, preserves the Output/Input tab.
void volume_mixer_notify_popup_open();

void dock_volume_mixer_popup_handle_click(DockApp& app, double x, double y, uint32_t serial);
// Press-and-drag support: call from pointer motion / button release / axis.
void dock_volume_mixer_popup_handle_motion(DockApp& app);
void dock_volume_mixer_popup_handle_release(DockApp& app);
// Vertical scroll over a slider: ±5%. Returns true when consumed.
bool dock_volume_mixer_popup_handle_axis(DockApp& app, double deltaPx);

template<typename A>
void dock_volume_mixer_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc);

namespace detail {
// Minimal view so one non-template paint/input implementation in the .cpp
// serves Dock, Panel and Taskbar popups (no per-TU static duplication).
struct MixerView {
  int popupW = 0;
  int popupH = 0;
  double px = 0.0;
  double py = 0.0;
  bool onPopup = false;
  eh::icons::IconCache* icons = nullptr;
};
void mixer_paint(cairo_t* cr, const eh::config::ShellConfig& sc, const MixerView& v);
// Coordinate-only input core. Returns true when the content height may have
// changed (tab switch / router expand-collapse) so the caller can resize.
bool mixer_click_at(double x, double y, double popupH);
void mixer_motion_at(double x, double y);
void mixer_release_at(double x);
bool mixer_axis_at(double x, double y, double deltaPx);
}  // namespace detail

}  // namespace eh::shell::dock::popup::volume_mixer

namespace eh::widgets::popup::volume_mixer {
using eh::shell::dock::popup::volume_mixer::kVolumeMixerPopupW;
using eh::shell::dock::popup::volume_mixer::kVolumeMixerPopupH;
using eh::shell::dock::popup::volume_mixer::volume_mixer_popup_content_height;
using eh::shell::dock::popup::volume_mixer::volume_mixer_notify_popup_open;
template<typename A>
inline void volume_mixer_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  eh::shell::dock::popup::volume_mixer::dock_volume_mixer_popup_paint(app, cr, sc);
}
inline void volume_mixer_popup_handle_click(DockApp& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::volume_mixer::dock_volume_mixer_popup_handle_click(app, x, y, serial);
}
template<typename A>
inline void volume_mixer_popup_handle_click(A& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::volume_mixer::dock_volume_mixer_popup_handle_click(app, x, y, serial);
}
template<typename A>
inline void volume_mixer_popup_handle_motion(A& app) {
  eh::shell::dock::popup::volume_mixer::dock_volume_mixer_popup_handle_motion(app);
}
template<typename A>
inline void volume_mixer_popup_handle_release(A& app) {
  eh::shell::dock::popup::volume_mixer::dock_volume_mixer_popup_handle_release(app);
}
template<typename A>
inline bool volume_mixer_popup_handle_axis(A& app, double deltaPx) {
  return eh::shell::dock::popup::volume_mixer::dock_volume_mixer_popup_handle_axis(app, deltaPx);
}
}  // namespace eh::widgets::popup::volume_mixer

#include "desktop_shell/widgets/popup/volume_mixer/volume_mixer_popup.tpp"
