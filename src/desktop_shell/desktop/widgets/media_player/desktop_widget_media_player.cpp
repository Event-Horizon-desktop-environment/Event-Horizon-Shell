#include "desktop_shell/desktop/widgets/media_player/desktop_widget_media_player.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "services/mpris/mpris_player.hpp"
#include "desktop_shell/dock/core/dock_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"

namespace eh::shell::desktop {
namespace {

using eh::shell::shared::rounded_rect;

// Transport boost (multiplier, NOT scaled).
constexpr double kBoost = 1.24;

// ════════════════════════════════════════════════════════════════════════════
// Helpers
// ════════════════════════════════════════════════════════════════════════════

void fill_rounded_rect(cairo_t* cr,
                       double x, double y, double w, double h, double r,
                       double rr, double gg, double bb, double aa) {
   
  rounded_rect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, rr, gg, bb, aa);
  cairo_fill(cr);
}

void stroke_rounded_rect(cairo_t* cr,
                         double x, double y, double w, double h, double r,
                         double lw,
                         double rr, double gg, double bb, double aa) {
   
  rounded_rect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, rr, gg, bb, aa);
  cairo_set_line_width(cr, lw);
  cairo_stroke(cr);
}

void format_time_us(char* buf, size_t sz, int64_t us) {
   
  if (us < 0) us = 0;
  const auto total = static_cast<uint64_t>(us) / 1'000'000ULL;
  const auto mins  = total / 60ULL;
  const auto secs  = total % 60ULL;
  std::snprintf(buf, sz, "%llu:%02llu",
                static_cast<unsigned long long>(mins),
                static_cast<unsigned long long>(secs));
}

// Measure single-line height for a font description string
int measure_one_line_h(cairo_t* cr, const char* fd_str) {
   
  auto* l  = pango_cairo_create_layout(cr);
  auto* fd = pango_font_description_from_string(fd_str);
  pango_layout_set_font_description(l, fd);
  pango_layout_set_text(l, "Ay", -1);
  int h = 0;
  pango_layout_get_pixel_size(l, nullptr, &h);
  pango_font_description_free(fd);
  g_object_unref(l);
  return h;
}

// Measure rendered pixel height of text block (no draw)
int measure_text_h(cairo_t* cr,
                   double max_w, const char* text,
                   const char* fd_str, int max_lines) {
   
  auto* l  = pango_cairo_create_layout(cr);
  auto* fd = pango_font_description_from_string(fd_str);
  pango_layout_set_font_description(l, fd);
  pango_font_description_free(fd);
  pango_layout_set_text(l, text, -1);
  pango_layout_set_width(l, static_cast<int>(max_w * PANGO_SCALE));
  pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
  if (max_lines > 0) {
    int one = measure_one_line_h(cr, fd_str);
    pango_layout_set_height(l, max_lines * one * PANGO_SCALE);
  }
  int h = 0;
  pango_layout_get_pixel_size(l, nullptr, &h);
  g_object_unref(l);
  return h;
}

// Draw text block, returns rendered pixel height. y = top of block.
int draw_text(cairo_t* cr,
              double x, double y, double max_w,
              const char* text, const char* fd_str,
              PangoAlignment align, int max_lines,
              double r, double g, double b, double a) {
   
  auto* l  = pango_cairo_create_layout(cr);
  auto* fd = pango_font_description_from_string(fd_str);
  pango_layout_set_font_description(l, fd);
  pango_font_description_free(fd);
  pango_layout_set_text(l, text, -1);
  pango_layout_set_width(l, static_cast<int>(max_w * PANGO_SCALE));
  pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
  pango_layout_set_alignment(l, align);
  if (max_lines > 0) {
    int one = measure_one_line_h(cr, fd_str);
    pango_layout_set_height(l, max_lines * one * PANGO_SCALE);
  }
  int pw = 0, ph = 0;
  pango_layout_get_pixel_size(l, &pw, &ph);
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_move_to(cr, x, y);
  pango_cairo_show_layout(cr, l);
  g_object_unref(l);
  return ph;
}

// EHAlbumArt accent ring: outer glow + sharp accent stroke
void draw_art_ring(cairo_t* cr, double cx, double cy, double r,
                   double ar, double ag, double ab, double us) {
   
  cairo_save(cr);
  // Soft outer glow
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r + 4.0 * us, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, ar, ag, ab, 0.18);
  cairo_set_line_width(cr, 7.0 * us);
  cairo_stroke(cr);
  // Sharp accent ring
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r + 1.0 * us, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, ar, ag, ab, 0.60);
  cairo_set_line_width(cr, 2.0 * us);
  cairo_stroke(cr);
  cairo_restore(cr);
}

// Y layout (shared between paint and on_click).
struct Layout {
  double titleY  = 0;
  double artistY = 0;
  double albumY  = 0;
  double seekY   = 0;
  double stampsY = 0;
  double ctrlY   = 0;
};

Layout compute_layout(int titleH, int artistH, int albumH,
                      bool hasArtist, bool hasAlbum, double us) {
   
  Layout L;
  const double kArtTop = 14.0 * us + 8.0 * us;
  const double kColW = (340.0 * us - 2.0 * 14.0 * us) - 28.0 * us;
  const double kArtSize = std::clamp(kColW * 0.52, 80.0 * us, 220.0 * us);
  const double kSpacing = 3.0 * us;
  const double kSeekAreaH = 22.0 * us;

  const double artBottom = kArtTop + kArtSize;

  L.titleY  = artBottom + kSpacing;
  L.artistY = L.titleY + titleH + kSpacing;
  L.albumY  = L.artistY + (hasArtist ? artistH : 0) + kSpacing;

  if (!hasArtist && !hasAlbum)
    L.seekY = L.titleY + titleH + kSpacing;
  else if (!hasAlbum)
    L.seekY = L.artistY + artistH + kSpacing;
  else
    L.seekY = L.albumY + albumH + kSpacing;

  L.seekY   += 1.0 * us;
  L.stampsY  = L.seekY + kSeekAreaH + kSpacing;
  L.ctrlY    = L.stampsY + 13.0 * us + kSpacing - 2.0 * us;

  return L;
}

bool hit_circle(double px, double py, double cx, double cy, double r) {
   
  const double dx = px - cx, dy = py - cy;
  return dx * dx + dy * dy <= r * r;
}

} // namespace

// ════════════════════════════════════════════════════════════════════════════

DesktopMediaPlayerWidget::DesktopMediaPlayerWidget(eh::mpris::DockMpris* mpris)
    : m_mpris(mpris)
    , m_cfg(CavaConfig::load_or_default()) {
    
  m_cava = std::make_unique<CavaReader>(m_cfg);
  m_cava->start();
}

DesktopMediaPlayerWidget::~DesktopMediaPlayerWidget() {
   
  if (m_cava) m_cava->stop();
}

void DesktopMediaPlayerWidget::create() {
  // Realtime config reload check (runs every ~1s via timer)
  {
    auto ch = m_cfg.check_and_reload();
    if (ch == ConfigChange::CavaChanged) {
      if (m_cava) m_cava->reload(m_cfg);
    }
  }

  if (!m_mpris) return;
  m_mpris->poll_refresh();
  const auto snap  = m_mpris->snapshot();
  m_active = snap.active && (!snap.title.empty() || !snap.artist.empty());
  if (m_active) {
    m_title          = snap.title.empty() ? "Unknown Track" : snap.title;
    m_artist         = snap.artist;
    m_album          = snap.album;
    m_playbackStatus = snap.playback_status;
    m_canPrev        = snap.can_go_previous;
    m_canNext        = snap.can_go_next;
    m_canPlay        = snap.can_play;
    m_canPause       = snap.can_pause;
  } else {
    m_title.clear(); m_artist.clear(); m_album.clear();
    m_playbackStatus.clear();
    m_canPrev = m_canNext = m_canPlay = m_canPause = false;
  }
}

bool DesktopMediaPlayerWidget::on_click(double x, double y) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);

  const double kW = 340.0 * us;
  const double kOuterMargin = 14.0 * us;
  const double kCloseBtnSize = 32.0 * us;
  const double kCloseBtnMarg = 8.0 * us;
  const double kInnerX = kOuterMargin;
  const double kInnerY = kOuterMargin;
  const double kInnerW = kW - 2.0 * kOuterMargin;
  const double kCloseBtnCx = kInnerX + kInnerW - kCloseBtnMarg - kCloseBtnSize / 2.0;
  const double kCloseBtnCy = kInnerY + kCloseBtnMarg + kCloseBtnSize / 2.0;

  const double kColW = kInnerW - 28.0 * us;
  const double kColPadX = 14.0 * us;
  const double kColX = kInnerX + kColPadX;

  const double kBtnSmall = 42.0 * kBoost * us;
  const double kBtnPlay  = 52.0 * kBoost * us;
  const double kBtnGap   = 14.0 * kBoost * us;

  const double kSeekAreaH = 22.0 * us;

  // Font strings
  char titleFont[64], artistFont[64], albumFont[64];
  std::snprintf(titleFont, sizeof(titleFont), "Inter DemiBold %.1f", 15.0 * us);
  std::snprintf(artistFont, sizeof(artistFont), "Inter %.1f", 13.0 * us);
  std::snprintf(albumFont, sizeof(albumFont), "Inter %.1f", 11.0 * us);

  // Close button — anchored to inner card
  if (hit_circle(x, y, kCloseBtnCx, kCloseBtnCy, kCloseBtnSize / 2.0))
    return true;   // host handles hide

  if (!m_active || !m_mpris) return false;

  int titleH = 0, artistH = 0, albumH = 0;
  {
    auto* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    auto* scr  = cairo_create(surf);
    titleH  = measure_text_h(scr, kColW, m_title.c_str(),  titleFont, 2);
    artistH = m_artist.empty() ? 0 : measure_text_h(scr, kColW, m_artist.c_str(), artistFont, 1);
    albumH  = m_album.empty()  ? 0 : measure_text_h(scr, kColW, m_album.c_str(),  albumFont, 1);
    cairo_destroy(scr);
    cairo_surface_destroy(surf);
  }

  const auto L = compute_layout(titleH, artistH, albumH,
                                 !m_artist.empty(), !m_album.empty(), us);

  const double totalW  = kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall;
  const double cLeft   = (kW - totalW) / 2.0;
  const double ccy     = L.ctrlY + kBtnPlay / 2.0;
  const double prevCx  = cLeft + kBtnSmall / 2.0;
  const double playCx  = cLeft + kBtnSmall + kBtnGap + kBtnPlay / 2.0;
  const double nextCx  = cLeft + kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall / 2.0;

  if (hit_circle(x, y, playCx, ccy, kBtnPlay / 2.0))  { m_mpris->play_pause(); return true; }
  if (hit_circle(x, y, prevCx, ccy, kBtnSmall / 2.0)) { if (m_canPrev) m_mpris->previous(); return true; }
  if (hit_circle(x, y, nextCx, ccy, kBtnSmall / 2.0)) { if (m_canNext) m_mpris->next();     return true; }

  // Seekbar.
  {
    m_mpris->poll_position();
    const auto seekSnap = m_mpris->snapshot();
    if (y >= L.seekY && y < L.seekY + kSeekAreaH &&
        x >= kColX && x <= kColX + kColW && seekSnap.duration_us > 0) {
      const double pct = std::clamp((x - kColX) / kColW, 0.0, 1.0);
      m_mpris->set_position(static_cast<int64_t>(pct * seekSnap.duration_us));
      return true;
    }
  }

  return false;
}

DesktopMediaPlayerWidget::HoverPart
DesktopMediaPlayerWidget::hit_test_hover(double x, double y) const {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);

  const double kW = 340.0 * us;
  const double kOuterMargin = 14.0 * us;
  const double kCloseBtnSize = 32.0 * us;
  const double kCloseBtnMarg = 8.0 * us;
  const double kInnerX = kOuterMargin;
  const double kInnerY = kOuterMargin;
  const double kInnerW = kW - 2.0 * kOuterMargin;
  const double kCloseBtnCx = kInnerX + kInnerW - kCloseBtnMarg - kCloseBtnSize / 2.0;
  const double kCloseBtnCy = kInnerY + kCloseBtnMarg + kCloseBtnSize / 2.0;

  const double kBtnSmall = 42.0 * kBoost * us;
  const double kBtnPlay  = 52.0 * kBoost * us;
  const double kBtnGap   = 14.0 * kBoost * us;

  const double kColW = kInnerW - 28.0 * us;
  const double kColPadX = 14.0 * us;
  const double kColX = kInnerX + kColPadX;
  const double kSeekAreaH = 22.0 * us;

  if (hit_circle(x, y, kCloseBtnCx, kCloseBtnCy, kCloseBtnSize / 2.0))
    return HoverClose;
  if (!m_active) return HoverNone;

  const double totalW  = kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall;
  const double cLeft   = (kW - totalW) / 2.0;
  const double ccy     = m_cacheCtrlY + kBtnPlay / 2.0;
  const double prevCx  = cLeft + kBtnSmall / 2.0;
  const double playCx  = cLeft + kBtnSmall + kBtnGap + kBtnPlay / 2.0;
  const double nextCx  = cLeft + kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall / 2.0;

  if (hit_circle(x, y, playCx, ccy, kBtnPlay / 2.0)) return HoverPlay;
  if (hit_circle(x, y, prevCx, ccy, kBtnSmall / 2.0)) return HoverPrev;
  if (hit_circle(x, y, nextCx, ccy, kBtnSmall / 2.0)) return HoverNext;

  if (y >= m_cacheSeekY && y < m_cacheSeekY + kSeekAreaH &&
      x >= kColX && x <= kColX + kColW)
    return HoverSeekbar;

  return HoverNone;
}

void DesktopMediaPlayerWidget::on_motion(double x, double y) {
   
  if (x < 0 || y < 0) {
    m_hoverPart = HoverNone;
    return;
  }
  m_hoverPart = hit_test_hover(x, y);
}

void DesktopMediaPlayerWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);

  const double kW = 340.0 * us;
  const double kH = 420.0 * us;
  const double kOuterR = 22.0 * us;
  const double kOuterMargin = 14.0 * us;
  const double kInnerX = kOuterMargin;
  const double kInnerY = kOuterMargin;
  const double kInnerW = kW - 2.0 * kOuterMargin;
  const double kInnerH = kH - 2.0 * kOuterMargin;
  const double kInnerR = 18.0 * us;
  const double kColW = kInnerW - 28.0 * us;
  const double kColPadX = 14.0 * us;
  const double kColX = kInnerX + kColPadX;
  const double kContentTopPad = 8.0 * us;
  const double kCloseBtnSize = 32.0 * us;
  const double kCloseBtnIcon = 18.0 * us;
  const double kCloseBtnMarg = 8.0 * us;
  const double kCloseBtnCx = kInnerX + kInnerW - kCloseBtnMarg - kCloseBtnSize / 2.0;
  const double kCloseBtnCy = kInnerY + kCloseBtnMarg + kCloseBtnSize / 2.0;
  const double kArtSize = std::clamp(kColW * 0.52, 80.0 * us, 220.0 * us);
  const double kArtR = kArtSize / 2.0;
  const double kArtTop = kInnerY + kContentTopPad;
  const double kArtCy = kArtTop + kArtR;
  const double kBtnSmall = 42.0 * kBoost * us;
  const double kBtnPlay = 52.0 * kBoost * us;
  const double kBtnGap = 14.0 * kBoost * us;
  const double kSeekAreaH = 22.0 * us;
  const double kBarH = 4.0 * us;
  const double kThumbR = 4.0 * us;
  const double kSpacing = 3.0 * us;

  // Font strings
  char titleFont[64], artistFont[64], albumFont[64], stampFont[64];
  std::snprintf(titleFont, sizeof(titleFont), "Inter DemiBold %.1f", 15.0 * us);
  std::snprintf(artistFont, sizeof(artistFont), "Inter %.1f", 13.0 * us);
  std::snprintf(albumFont, sizeof(albumFont), "Inter %.1f", 11.0 * us);
  std::snprintf(stampFont, sizeof(stampFont), "Inter %.1f", 11.0 * us);

  cairo_save(cr);

  // ══════════════════════════════════════════════════════════════════════════
  // LAYER 1 — Outer card (PopupSurface)
  // ══════════════════════════════════════════════════════════════════════════
  eh::shell::shared::paint_glass_card(cr, 0, 0, kW, kH, kOuterR, mc,
                                      sc.appearance.overlayOpacityWidgetCard);

  // ══════════════════════════════════════════════════════════════════════════
  // LAYER 2 — Inner card (MediaPopupContent Rectangle)
  // ══════════════════════════════════════════════════════════════════════════
  rounded_rect(cr, kInnerX, kInnerY, kInnerW, kInnerH, kInnerR);
  // Clip subsequent content to inner card shape
  cairo_save(cr);
  rounded_rect(cr, kInnerX, kInnerY, kInnerW, kInnerH, kInnerR);
  cairo_clip(cr);

  // Inner card fill — Theme.surface @0.55 (slightly lighter/different from outer)
  cairo_set_source_rgba(cr, mc.dockFillR * 1.15, mc.dockFillG * 1.15, mc.dockFillB * 1.15, 0.55);
  cairo_paint(cr);
  cairo_restore(cr);

  // Inner card border — outline @0.20, 1px (line width, not scaled)
  stroke_rounded_rect(cr, kInnerX, kInnerY, kInnerW, kInnerH, kInnerR,
                       1.0, mc.outlineR, mc.outlineG, mc.outlineB, 0.20);

  const double cx = kW / 2.0;

  // Close button (top-right of the inner card, margin 8).
  const double closeBtnA = (m_hoverPart == HoverClose) ? 0.25 : 0.12;
  fill_rounded_rect(cr,
                    kCloseBtnCx - kCloseBtnSize / 2.0,
                    kCloseBtnCy - kCloseBtnSize / 2.0,
                    kCloseBtnSize, kCloseBtnSize, kCloseBtnSize / 2.0,
                    mc.outlineR, mc.outlineG, mc.outlineB, closeBtnA);
  {
    double old_op = eh::widgets::slot_pill_style::g_opacityScale;
    eh::widgets::slot_pill_style::g_opacityScale = closeBtnA;
    eh::widgets::slot_pill_style::paint_glass_layers(cr,
      kCloseBtnCx - kCloseBtnSize / 2.0, kCloseBtnCy - kCloseBtnSize / 2.0,
      kCloseBtnSize, kCloseBtnSize, 1.0, kCloseBtnSize / 2.0);
    eh::widgets::slot_pill_style::g_opacityScale = old_op;
  }
  const double closeIconA = (m_hoverPart == HoverClose) ? 0.90 : 0.70;
  eh::shell::draw_material_glyph(cr, kCloseBtnCx, kCloseBtnCy,
                                  kCloseBtnIcon, "close",
                                  mc.textR, mc.textG, mc.textB, closeIconA);

  // Idle state.
  if (!m_active) {
    cairo_new_path(cr);
    cairo_arc(cr, cx, kArtCy, kArtR, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.12, 0.14, 0.16, 1.0);
    cairo_fill(cr);
    eh::shell::draw_material_glyph(cr, cx, kArtCy, 48.0 * us, "music_note",
                                    mc.outlineR, mc.outlineG, mc.outlineB, 0.30);
    draw_text(cr, kColX, kArtTop + kArtSize + kSpacing, kColW,
              "No media playing", titleFont,
              PANGO_ALIGN_CENTER, 1,
              mc.textR, mc.textG, mc.textB, 0.92);
    cairo_restore(cr);
    return;
  }

  // Poll position.
  if (m_mpris) m_mpris->poll_position();
  const auto snap = m_mpris ? m_mpris->snapshot() : eh::mpris::PlayerSnapshot{};

  // Advance cava smoothing (time-delta based).
  tick_cava();
  {
    const auto now = std::chrono::steady_clock::now();
    const double dt = m_lastTickTime.time_since_epoch().count() == 0
        ? 16.0
        : std::chrono::duration<double, std::milli>(now - m_lastTickTime).count();
    m_lastTickTime = now;
    tick(dt);
  }

  // Cava morphing blob (behind the album art).
  if (m_cavaActive && snap.playback_status == "Playing" && !m_blobConverged) {
    constexpr int kSegments = 28;

    // Compute control points.
    const double blobCx = cx;
    const double blobCy = kArtCy;
    const double baseR = kArtR * 1.35;

    struct Point { double x, y; };
    Point pts[28]{};

    for (int i = 0; i < kSegments; ++i) {
      const double angle = static_cast<double>(i) / static_cast<double>(kSegments) * 2.0 * M_PI - M_PI_2;
      const double level = std::max(0.15, m_blobSmoothed[i]) * 0.5;
      const double r = baseR * (1.0 + level);
      pts[i].x = blobCx + r * std::cos(angle);
      pts[i].y = blobCy + r * std::sin(angle);
    }

    // Draw the filled blob via Catmull-Rom to cubic Bezier.
    cairo_save(cr);
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, m_cfg.blob_opacity);

    cairo_new_path(cr);
    cairo_move_to(cr, pts[0].x, pts[0].y);

    const double tension = 0.5;
    for (int i = 0; i < kSegments; ++i) {
      const Point& p0 = pts[(i - 1 + kSegments) % kSegments];
      const Point& p1 = pts[i];
      const Point& p2 = pts[(i + 1) % kSegments];
      const Point& p3 = pts[(i + 2) % kSegments];

      const double c1x = p1.x + (p2.x - p0.x) * tension / 3.0;
      const double c1y = p1.y + (p2.y - p0.y) * tension / 3.0;
      const double c2x = p2.x - (p3.x - p1.x) * tension / 3.0;
      const double c2y = p2.y - (p3.y - p1.y) * tension / 3.0;

      cairo_curve_to(cr, c1x, c1y, c2x, c2y, p2.x, p2.y);
    }
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
  }

  // Album art.
  bool drewArt = false;
  if (snap.art && cairo_surface_status(snap.art.get()) == CAIRO_STATUS_SUCCESS) {
    const int iw = cairo_image_surface_get_width(snap.art.get());
    const int ih = cairo_image_surface_get_height(snap.art.get());
    if (iw > 0 && ih > 0) {
      const double s2 = std::max(kArtSize / static_cast<double>(iw),
                                  kArtSize / static_cast<double>(ih));
      cairo_save(cr);
      cairo_new_path(cr);
      cairo_arc(cr, cx, kArtCy, kArtR, 0, 2 * M_PI);
      cairo_clip(cr);
      cairo_translate(cr, cx - (iw * s2) / 2.0, kArtCy - (ih * s2) / 2.0);
      cairo_scale(cr, s2, s2);
      cairo_set_source_surface(cr, snap.art.get(), 0, 0);
      cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
      cairo_paint(cr);
      cairo_restore(cr);
      drewArt = true;
    }
  }
  if (!drewArt) {
    cairo_new_path(cr);
    cairo_arc(cr, cx, kArtCy, kArtR, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.12, 0.14, 0.16, 1.0);
    cairo_fill(cr);
    eh::shell::draw_material_glyph(cr, cx, kArtCy, 48.0 * us, "music_note",
                                    mc.outlineR, mc.outlineG, mc.outlineB, 0.40);
  }
  // Accent ring (EHAlbumArt chrome)
  draw_art_ring(cr, cx, kArtCy, kArtR, mc.accentR, mc.accentG, mc.accentB, us);

  // Measure text heights.
  const int titleH  = measure_text_h(cr, kColW, m_title.c_str(),  titleFont, 2);
  const int artistH = m_artist.empty() ? 0
                    : measure_text_h(cr, kColW, m_artist.c_str(), artistFont, 1);
  const int albumH  = m_album.empty()  ? 0
                    : measure_text_h(cr, kColW, m_album.c_str(),  albumFont, 1);
  const auto L = compute_layout(titleH, artistH, albumH,
                                 !m_artist.empty(), !m_album.empty(), us);

  // Cache layout for hover hit-testing
  m_cacheSeekY = L.seekY;
  m_cacheCtrlY = L.ctrlY;

  // Title, DemiBold 15, centered, 2 lines, 0.92 alpha.
  draw_text(cr, kColX, L.titleY, kColW,
            m_title.c_str(), titleFont,
            PANGO_ALIGN_CENTER, 2,
            mc.textR, mc.textG, mc.textB, 0.92);

  // Artist, Regular 13, centered, 1 line, 0.75 alpha.
  if (!m_artist.empty())
    draw_text(cr, kColX, L.artistY, kColW,
              m_artist.c_str(), artistFont,
              PANGO_ALIGN_CENTER, 1,
              mc.textR, mc.textG, mc.textB, 0.75);

  // Album, Regular 11, centered, 1 line, 0.55 alpha.
  if (!m_album.empty())
    draw_text(cr, kColX, L.albumY, kColW,
              m_album.c_str(), albumFont,
              PANGO_ALIGN_CENTER, 1,
              mc.textR, mc.textG, mc.textB, 0.55);

  // Seek bar (EHSeekbar).
  double pct = 0.0;
  if (snap.duration_us > 0)
    pct = std::clamp(static_cast<double>(snap.position_us) /
                     static_cast<double>(snap.duration_us), 0.0, 1.0);

  const double barY   = L.seekY + (kSeekAreaH - kBarH) / 2.0;
  const double barW   = kColW;
  const double fillW  = barW * pct;
  const double thumbX = kColX + fillW;

  const bool seekHover = (m_hoverPart == HoverSeekbar);

  // Track background
  fill_rounded_rect(cr, kColX, barY, barW, kBarH, kBarH / 2.0,
                    mc.outlineR, mc.outlineG, mc.outlineB, seekHover ? 0.28 : 0.18);
  {
    double old_op = eh::widgets::slot_pill_style::g_opacityScale;
    eh::widgets::slot_pill_style::g_opacityScale = seekHover ? 0.28 : 0.18;
    eh::widgets::slot_pill_style::paint_glass_layers(cr, kColX, barY, barW, kBarH, 1.0, kBarH / 2.0);
    eh::widgets::slot_pill_style::g_opacityScale = old_op;
  }
  // Filled portion (clip to left of thumb)
  if (fillW > 0.5) {
    cairo_save(cr);
    cairo_rectangle(cr, kColX, barY - 1.0 * us, fillW, kBarH + 2.0 * us);
    cairo_clip(cr);
    fill_rounded_rect(cr, kColX, barY, barW, kBarH, kBarH / 2.0,
                      mc.accentR, mc.accentG, mc.accentB, seekHover ? 1.0 : 0.90);
    {
      double old_op = eh::widgets::slot_pill_style::g_opacityScale;
      eh::widgets::slot_pill_style::g_opacityScale = seekHover ? 1.0 : 0.90;
      eh::widgets::slot_pill_style::paint_glass_layers(cr, kColX, barY, barW, kBarH, 1.0, kBarH / 2.0);
      eh::widgets::slot_pill_style::g_opacityScale = old_op;
    }
    cairo_restore(cr);
  }
  // Thumb knob — accent filled circle with white center
  const double thumbR = seekHover ? kThumbR + 2.0 * us : kThumbR;
  const double thumbOuterA = seekHover ? 1.0 : 0.85;
  cairo_new_path(cr);
  cairo_arc(cr, thumbX, barY + kBarH / 2.0, thumbR, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, thumbOuterA);
  cairo_fill(cr);
  cairo_new_path(cr);
  cairo_arc(cr, thumbX, barY + kBarH / 2.0, thumbR - 2.0 * us, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, seekHover ? 0.95 : 0.85);
  cairo_fill(cr);

  // Timestamps.
  char startBuf[24] = "0:00", endBuf[24] = "0:00";
  format_time_us(startBuf, sizeof(startBuf), snap.position_us);
  format_time_us(endBuf,   sizeof(endBuf),   snap.duration_us);
  const double stampA = 0.50;

  draw_text(cr, kColX, L.stampsY, kColW / 2.0,
            startBuf, stampFont, PANGO_ALIGN_LEFT, 1,
            mc.textR, mc.textG, mc.textB, stampA);

  // Right-align end time
  {
    auto* tl  = pango_cairo_create_layout(cr);
    auto* tfd = pango_font_description_from_string(stampFont);
    pango_layout_set_font_description(tl, tfd);
    pango_font_description_free(tfd);
    pango_layout_set_text(tl, endBuf, -1);
    int tw = 0;
    pango_layout_get_pixel_size(tl, &tw, nullptr);
    g_object_unref(tl);
    draw_text(cr, kColX + barW - static_cast<double>(tw), L.stampsY, barW,
              endBuf, stampFont, PANGO_ALIGN_LEFT, 1,
              mc.textR, mc.textG, mc.textB, stampA);
  }

  // Transport controls.
  const double totalCtrlW = kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall;
  const double cLeft      = (kW - totalCtrlW) / 2.0;
  const double ccy        = L.ctrlY + kBtnPlay / 2.0;
  const double prevCx     = cLeft + kBtnSmall / 2.0;
  const double playCx     = cLeft + kBtnSmall + kBtnGap + kBtnPlay / 2.0;
  const double nextCx     = cLeft + kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall / 2.0;

  const bool prevHover  = (m_hoverPart == HoverPrev);
  const bool playHover  = (m_hoverPart == HoverPlay);
  const bool nextHover  = (m_hoverPart == HoverNext);

  // Prev
  const double prevBgA = m_canPrev ? (prevHover ? 0.30 : 0.15) : 0.06;
  fill_rounded_rect(cr,
                    prevCx - kBtnSmall / 2.0, ccy - kBtnSmall / 2.0,
                    kBtnSmall, kBtnSmall, kBtnSmall / 2.0,
                    mc.outlineR, mc.outlineG, mc.outlineB, prevBgA);
  {
    double old_op = eh::widgets::slot_pill_style::g_opacityScale;
    eh::widgets::slot_pill_style::g_opacityScale = prevBgA;
    eh::widgets::slot_pill_style::paint_glass_layers(cr,
      prevCx - kBtnSmall / 2.0, ccy - kBtnSmall / 2.0,
      kBtnSmall, kBtnSmall, 1.0, kBtnSmall / 2.0);
    eh::widgets::slot_pill_style::g_opacityScale = old_op;
  }
  const double prevIconA = m_canPrev ? (prevHover ? 1.0 : 0.85) : 0.30;
  eh::shell::draw_material_glyph(cr, prevCx, ccy, 22.0 * kBoost * us, "skip_previous",
                                  mc.textR, mc.textG, mc.textB, prevIconA);

  // Play/Pause — Theme.primary fill, Theme.background icon
  const bool   playing   = (m_playbackStatus == "Playing");
  const double playAlpha = (playing || m_canPlay || m_canPause) ? (playHover ? 1.0 : 0.95) : 0.30;
  fill_rounded_rect(cr,
                    playCx - kBtnPlay / 2.0, ccy - kBtnPlay / 2.0,
                    kBtnPlay, kBtnPlay, kBtnPlay / 2.0,
                    mc.accentR, mc.accentG, mc.accentB, playAlpha);
  {
    double old_op = eh::widgets::slot_pill_style::g_opacityScale;
    eh::widgets::slot_pill_style::g_opacityScale = playAlpha;
    eh::widgets::slot_pill_style::paint_glass_layers(cr,
      playCx - kBtnPlay / 2.0, ccy - kBtnPlay / 2.0,
      kBtnPlay, kBtnPlay, 1.0, kBtnPlay / 2.0);
    eh::widgets::slot_pill_style::g_opacityScale = old_op;
  }
  eh::shell::draw_material_glyph(cr, playCx, ccy, 28.0 * kBoost * us,
                                  playing ? "pause" : "play_arrow",
                                  mc.dockFillR, mc.dockFillG, mc.dockFillB, playHover ? 1.0 : 0.95);

  // Next
  const double nextBgA = m_canNext ? (nextHover ? 0.30 : 0.15) : 0.06;
  fill_rounded_rect(cr,
                    nextCx - kBtnSmall / 2.0, ccy - kBtnSmall / 2.0,
                    kBtnSmall, kBtnSmall, kBtnSmall / 2.0,
                    mc.outlineR, mc.outlineG, mc.outlineB, nextBgA);
  {
    double old_op = eh::widgets::slot_pill_style::g_opacityScale;
    eh::widgets::slot_pill_style::g_opacityScale = nextBgA;
    eh::widgets::slot_pill_style::paint_glass_layers(cr,
      nextCx - kBtnSmall / 2.0, ccy - kBtnSmall / 2.0,
      kBtnSmall, kBtnSmall, 1.0, kBtnSmall / 2.0);
    eh::widgets::slot_pill_style::g_opacityScale = old_op;
  }
  const double nextIconA = m_canNext ? (nextHover ? 1.0 : 0.85) : 0.30;
  eh::shell::draw_material_glyph(cr, nextCx, ccy, 22.0 * kBoost * us, "skip_next",
                                  mc.textR, mc.textG, mc.textB, nextIconA);

  cairo_restore(cr);
}

bool DesktopMediaPlayerWidget::tick_cava() {
   
  if (m_cava && m_cava->available()) {
    m_cava->read_values(m_cavaValues);
    m_cavaActive = !m_cavaValues.empty();
  } else {
    m_cavaActive = false;
    return false;
  }
  if (!m_cavaActive) return false;

  constexpr int kSegments = 28;
  const int nCava = std::min(static_cast<int>(m_cavaValues.size()), m_cfg.bars);
  bool changed = false;
  for (int i = 0; i < kSegments; ++i) {
    const int cavaIdx = i % nCava;
    const int raw = m_cavaValues[static_cast<size_t>(cavaIdx)];
    const double clamped = std::clamp(static_cast<double>(raw), 0.0, 100.0);
    const double t = std::sqrt(clamped / 100.0);
    if (std::abs(t - m_blobTarget[i]) > 1.0 / 512.0) changed = true;
    m_blobTarget[i] = t;
  }
  return changed;
}

void DesktopMediaPlayerWidget::tick(double deltaMs) {
   
  if (!m_cavaActive) return;

  const double alpha = m_cfg.blob_tau_ms > 0.0
      ? 1.0 - std::exp(-std::max(0.0, deltaMs) / m_cfg.blob_tau_ms)
      : 1.0;
  constexpr double kEpsilon = 1.0 / 512.0;

  bool converged = true;
  for (int i = 0; i < 28; ++i) {
    const double target = m_blobTarget[i];
    double& display = m_blobSmoothed[i];
    const double delta = target - display;
    if (std::fabs(delta) < kEpsilon) {
      if (display != target) display = target;
      continue;
    }
    display += delta * alpha;
    if (std::fabs(target - display) >= kEpsilon) converged = false;
  }
  m_blobConverged = converged;
}

} // namespace eh::shell::desktop
