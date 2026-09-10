#pragma once

#include <cstdint>

typedef struct _cairo cairo_t;
struct DockApp;
namespace eh::config { struct ShellConfig; }

namespace eh::shell::dock::popup::volume_mixer {

constexpr int kVolumeMixerPopupW = 600;

template<typename A>
void dock_volume_mixer_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc);

void dock_volume_mixer_popup_handle_click(DockApp& app, double x, double y, uint32_t serial);
}

namespace eh::widgets::popup::volume_mixer {
using eh::shell::dock::popup::volume_mixer::kVolumeMixerPopupW;
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
}

#include "desktop_shell/widgets/popup/volume_mixer/volume_mixer_popup.tpp"
