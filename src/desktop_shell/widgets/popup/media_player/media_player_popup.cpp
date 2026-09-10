#include "../../../dock/core/dock_app.h"
#include "../../../shared/popup/session/session.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

namespace eh::shell::dock::popup::media_player {

// Layout constants (mirrored from the tpp for click hit-testing).
namespace {
constexpr double kW           = 340.0;
constexpr double kOuterMargin = 14.0;
constexpr double kInnerX      = kOuterMargin;
constexpr double kInnerY      = kOuterMargin;
constexpr double kInnerW      = kW - 2.0 * kOuterMargin;
constexpr double kInnerH      = kW - 2.0 * kOuterMargin;

constexpr double kCloseBtnSize = 32.0;
constexpr double kCloseBtnMarg =  8.0;
constexpr double kCloseBtnCx = kInnerX + kInnerW - kCloseBtnMarg - kCloseBtnSize / 2.0;
constexpr double kCloseBtnCy = kInnerY + kCloseBtnMarg + kCloseBtnSize / 2.0;

constexpr double kColW    = kInnerW - 28.0;
constexpr double kColX    = kInnerX + 14.0;

constexpr double kArtSize = std::clamp(kColW * 0.52, 80.0, 220.0);
constexpr double kArtTop  = kInnerY + 8.0;

constexpr double kBoost    = 1.24;
constexpr double kBtnSmall = 42.0 * kBoost;
constexpr double kBtnPlay  = 52.0 * kBoost;
constexpr double kBtnGap   = 14.0 * kBoost;

constexpr double kSeekAreaH = 22.0;
constexpr double kSpacing   = 3.0;

bool hit_circle(double px, double py, double cx, double cy, double r) {
   
  const double dx = px - cx, dy = py - cy;
  return dx * dx + dy * dy <= r * r;
}

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
}

void dock_media_player_popup_handle_click(DockApp& app, double x, double y, uint32_t) {
   
  // Close button
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

  const double artBottom = kArtTop + kArtSize;

  double titleY  = artBottom + kSpacing;
  double artistY = titleY + titleH + kSpacing;
  double albumY  = artistY + (artist_m.empty() ? 0 : artistH) + kSpacing;

  double seekY;
  if (artist_m.empty() && album_m.empty())
    seekY = titleY + titleH + kSpacing;
  else if (album_m.empty())
    seekY = artistY + artistH + kSpacing;
  else
    seekY = albumY + albumH + kSpacing;

  seekY += 1.0;

  double stampsY = seekY + kSeekAreaH + kSpacing;
  double ctrlY   = stampsY + 13.0 + kSpacing - 2.0;

  // Transport buttons
  const double totalW  = kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall;
  const double cLeft   = (kW - totalW) / 2.0;
  const double ccy     = ctrlY + kBtnPlay / 2.0;
  const double prevCx  = cLeft + kBtnSmall / 2.0;
  const double playCx  = cLeft + kBtnSmall + kBtnGap + kBtnPlay / 2.0;
  const double nextCx  = cLeft + kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall / 2.0;

  if (hit_circle(x, y, playCx, ccy, kBtnPlay / 2.0))  { app.mpris->play_pause(); return; }
  if (hit_circle(x, y, prevCx, ccy, kBtnSmall / 2.0)) { if (snap.can_go_previous) app.mpris->previous(); return; }
  if (hit_circle(x, y, nextCx, ccy, kBtnSmall / 2.0)) { if (snap.can_go_next) app.mpris->next(); return; }

  // Seekbar
  if (y >= seekY && y < seekY + kSeekAreaH &&
      x >= kColX && x <= kColX + kColW && snap.duration_us > 0) {
    const double pct = std::clamp((x - kColX) / kColW, 0.0, 1.0);
    app.mpris->set_position(static_cast<int64_t>(pct * snap.duration_us));
  }
}

} // namespace eh::shell::dock::popup::media_player
