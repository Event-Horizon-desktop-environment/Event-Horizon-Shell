#pragma once

#include <cstdint>

typedef struct _cairo cairo_t;
struct DockApp;
namespace eh::config { struct ShellConfig; }

namespace eh::shell::dock::popup::media_player {

// iOS-style now-playing card: art + scrolling title/artist, wave bars,
// seekbar with elapsed/remaining stamps, 5 transport controls.
constexpr int kMediaPlayerPopupW = 400;
constexpr int kMediaPlayerPopupH = 188;

// Process-shared marquee state (one popup per process): paint sets it when
// the title or artist is actually scrolling, timer drivers read it.
inline bool& media_popup_marquee_state() {
  static bool active = false;
  return active;
}
inline bool media_popup_marquee_active() { return media_popup_marquee_state(); }

template<typename A>
void dock_media_player_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc);

void dock_media_player_popup_handle_click(DockApp& app, double x, double y, uint32_t serial);

}

namespace eh::widgets::popup::media_player {
using eh::shell::dock::popup::media_player::kMediaPlayerPopupW;
using eh::shell::dock::popup::media_player::kMediaPlayerPopupH;
template<typename A>
inline void media_player_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  eh::shell::dock::popup::media_player::dock_media_player_popup_paint(app, cr, sc);
}
inline void media_player_popup_handle_click(DockApp& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::media_player::dock_media_player_popup_handle_click(app, x, y, serial);
}
template<typename A>
inline void media_player_popup_handle_click(A& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::media_player::dock_media_player_popup_handle_click(app, x, y, serial);
}
}

#include "desktop_shell/widgets/popup/media_player/media_player_popup.tpp"
