#include "../../../dock/core/dock_app.h"
#include "../../../shared/popup/session/session.hpp"
#include "desktop_shell/widgets/popup/media_player/media_player_popup.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

namespace eh::shell::dock::popup::media_player {



int measure_one_line_h(const char* fd_str) {
   
  auto* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  auto* cr = cairo_create(surf);
  auto* l = pango_cairo_create_layout(cr);
  auto* fd = pango_font_description_from_string(fd_str);
  pango_layout_set_font_description(l, fd);
  pango_layout_set_text(l, "Ay", -1);
  int h = 0;
  pango_layout_get_pixel_size(l, nullptr, &h);
  pango_font_description_free(fd);
  g_object_unref(l);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return h;
}

int measure_text_h(const char* text, const char* fd_str, int max_lines, double max_w) {
   
  auto* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  auto* cr = cairo_create(surf);
  auto* l = pango_cairo_create_layout(cr);
  auto* fd = pango_font_description_from_string(fd_str);
  pango_layout_set_font_description(l, fd);
  pango_font_description_free(fd);
  pango_layout_set_text(l, text, -1);
  pango_layout_set_width(l, static_cast<int>(max_w * PANGO_SCALE));
  pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
  if (max_lines > 0) {
    int one = measure_one_line_h(fd_str);
    pango_layout_set_height(l, max_lines * one * PANGO_SCALE);
  }
  int h = 0;
  pango_layout_get_pixel_size(l, nullptr, &h);
  g_object_unref(l);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return h;
}

void dock_media_player_popup_handle_click(DockApp& app, double x, double y, uint32_t) {
  // Geometry comes from the shared paint/hit implementation in the .tpp
  // (single source; see Docs/hit-testing.md).
  if (hit_circle(x, y, kCloseBtnCx, kCloseBtnCy, kCloseBtnSize / 2.0)) {
    popup_close(app);
    return;
  }

  if (!app.mpris) return;
  app.mpris->poll_refresh();
  const auto snap = app.mpris->snapshot();
  const bool realActive = snap.active && (!snap.title.empty() || !snap.artist.empty());
  if (!realActive) return;

  const std::string title_m  = snap.title.empty() ? "Unknown Track" : snap.title;
  const std::string artist_m = snap.artist;
  const std::string album_m  = snap.album;

  const int titleH  = measure_text_h(title_m.c_str(),  "Sans DemiBold 15", 2, kColW);
  const int artistH = artist_m.empty() ? 0 : measure_text_h(artist_m.c_str(), "Sans 13", 1, kColW);
  const int albumH  = album_m.empty()  ? 0 : measure_text_h(album_m.c_str(),  "Sans 11", 1, kColW);

  const Layout L = compute_layout(titleH, artistH, albumH,
                                  !artist_m.empty(), !album_m.empty());
  switch (compute_hover(x, y, L.seekY, L.ctrlY, realActive,
                        snap.can_go_previous, snap.can_go_next,
                        snap.can_play, snap.can_pause, snap.playback_status)) {
    case MediaHover::Play:
      app.mpris->play_pause();
      return;
    case MediaHover::Prev:
      if (snap.can_go_previous) app.mpris->previous();
      return;
    case MediaHover::Next:
      if (snap.can_go_next) app.mpris->next();
      return;
    case MediaHover::Seekbar:
      if (snap.duration_us > 0) {
        const double pct = std::clamp((x - kColX) / kColW, 0.0, 1.0);
        app.mpris->set_position(static_cast<int64_t>(pct * snap.duration_us));
      }
      return;
    case MediaHover::Close:
    case MediaHover::None:
      return;
  }
}

} // namespace eh::shell::dock::popup::media_player
