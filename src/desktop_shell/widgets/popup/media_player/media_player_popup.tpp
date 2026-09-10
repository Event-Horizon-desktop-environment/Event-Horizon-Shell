#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "services/mpris/mpris_player.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "m3/core/primitives/box.hpp"
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

namespace eh::shell::dock::popup::media_player {
namespace {

// ════════════════════════════════════════════════════════════════════════════
// Constants (matching DesktopMediaPlayerWidget)
// ════════════════════════════════════════════════════════════════════════════

constexpr double kW = 340.0;
constexpr double kH = 420.0;

constexpr double kOuterR      = 22.0;
constexpr double kOuterMargin = 14.0;

constexpr double kInnerX = kOuterMargin;
constexpr double kInnerY = kOuterMargin;
constexpr double kInnerW = kW - 2.0 * kOuterMargin;
constexpr double kInnerH = kH - 2.0 * kOuterMargin;
constexpr double kInnerR = 18.0;

constexpr double kColW    = kInnerW - 28.0;
constexpr double kColPadX = 14.0;
constexpr double kColX    = kInnerX + kColPadX;

constexpr double kContentTopPad    =  8.0;
constexpr double kContentBottomPad = 10.0;

constexpr double kCloseBtnSize = 32.0;
constexpr double kCloseBtnIcon = 18.0;
constexpr double kCloseBtnMarg =  8.0;
constexpr double kCloseBtnCx = kInnerX + kInnerW - kCloseBtnMarg - kCloseBtnSize / 2.0;
constexpr double kCloseBtnCy = kInnerY + kCloseBtnMarg + kCloseBtnSize / 2.0;

constexpr double kArtSize = std::clamp(kColW * 0.52, 80.0, 220.0);
constexpr double kArtR    = kArtSize / 2.0;
constexpr double kArtTop  = kInnerY + kContentTopPad;
constexpr double kArtCy   = kArtTop + kArtR;

constexpr double kBoost    = 1.24;
constexpr double kBtnSmall = 42.0 * kBoost;
constexpr double kBtnPlay  = 52.0 * kBoost;
constexpr double kBtnGap   = 14.0 * kBoost;

constexpr double kSeekAreaH = 22.0;
constexpr double kBarH      =  4.0;
constexpr double kThumbR    =  4.0;

constexpr double kSpacing = 3.0;

// Hover part enum.
enum class MediaHover : int { None = 0, Close, Prev, Play, Next, Seekbar };

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double rad = std::min({r, w * 0.5, h * 0.5});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - rad, y + rad,     rad, -M_PI_2,      0.0       );
  cairo_arc(cr, x + w - rad, y + h - rad, rad,  0.0,         M_PI_2    );
  cairo_arc(cr, x + rad,     y + h - rad, rad,  M_PI_2,      M_PI      );
  cairo_arc(cr, x + rad,     y + rad,     rad,  M_PI,  3.0 * M_PI_2    );
  cairo_close_path(cr);
}

void fill_rounded_rect(cairo_t* cr,
                       double x, double y, double w, double h, double r,
                       double rr, double gg, double bb, double aa) {
  rounded_rect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, rr, gg, bb, aa);
  cairo_fill(cr);
}

[[maybe_unused]] void format_time_us(char* buf, size_t sz, int64_t us) {
  if (us < 0) us = 0;
  const auto total = static_cast<uint64_t>(us) / 1'000'000ULL;
  const auto mins  = total / 60ULL;
  const auto secs  = total % 60ULL;
  std::snprintf(buf, sz, "%llu:%02llu",
                static_cast<unsigned long long>(mins),
                static_cast<unsigned long long>(secs));
}

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

void draw_art_ring(cairo_t* cr, double cx, double cy, double r,
                   double ar, double ag, double ab) {
  cairo_save(cr);
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r + 4.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, ar, ag, ab, 0.18);
  cairo_set_line_width(cr, 7.0);
  cairo_stroke(cr);
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, r + 1.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, ar, ag, ab, 0.60);
  cairo_set_line_width(cr, 2.0);
  cairo_stroke(cr);
  cairo_restore(cr);
}

struct Layout {
  double titleY  = 0;
  double artistY = 0;
  double albumY  = 0;
  double seekY   = 0;
  double stampsY = 0;
  double ctrlY   = 0;
};

Layout compute_layout(int titleH, int artistH, int albumH,
                      bool hasArtist, bool hasAlbum) {
  Layout L;
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

  L.seekY   += 1.0;
  L.stampsY  = L.seekY + kSeekAreaH + kSpacing;
  L.ctrlY    = L.stampsY + 13.0 + kSpacing - 2.0;

  return L;
}

bool hit_circle(double px, double py, double cx, double cy, double r) {
  const double dx = px - cx, dy = py - cy;
  return dx * dx + dy * dy <= r * r;
}

MediaHover compute_hover(double px, double py,
                          double seekY, double ctrlY,
                          bool active,
                          bool canPrev, bool canNext,
                          bool canPlay, bool canPause,
                          const std::string& playbackStatus) {
  if (hit_circle(px, py, kCloseBtnCx, kCloseBtnCy, kCloseBtnSize / 2.0))
    return MediaHover::Close;
  if (!active) return MediaHover::None;

  const double totalW  = kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall;
  const double cLeft   = (kW - totalW) / 2.0;
  const double ccy     = ctrlY + kBtnPlay / 2.0;
  const double prevCx  = cLeft + kBtnSmall / 2.0;
  const double playCx  = cLeft + kBtnSmall + kBtnGap + kBtnPlay / 2.0;
  const double nextCx  = cLeft + kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall / 2.0;

  (void)canPrev; (void)canNext; (void)canPlay; (void)canPause; (void)playbackStatus;

  if (hit_circle(px, py, playCx, ccy, kBtnPlay / 2.0)) return MediaHover::Play;
  if (hit_circle(px, py, prevCx, ccy, kBtnSmall / 2.0)) return MediaHover::Prev;
  if (hit_circle(px, py, nextCx, ccy, kBtnSmall / 2.0)) return MediaHover::Next;

  if (py >= seekY && py < seekY + kSeekAreaH &&
      px >= kColX && px <= kColX + kColW)
    return MediaHover::Seekbar;

  return MediaHover::None;
}

} // anonymous namespace

// ════════════════════════════════════════════════════════════════════════════
// Paint
// ════════════════════════════════════════════════════════════════════════════

template<typename A>
void dock_media_player_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);

  cairo_save(cr);

  // Layer 1 — Outer card
  {
    const float outerAlpha = static_cast<float>(0.78 * sc.appearance.overlayOpacityWidgetCard);
    m3::Box box;
    box.setColor(static_cast<float>(mc.dockFillR * 0.35), static_cast<float>(mc.dockFillG * 0.35),
                 static_cast<float>(mc.dockFillB * 0.35), outerAlpha);
    box.setRadius(static_cast<float>(kOuterR));
    box.setGeometry(0, 0, kW, kH);
    box.setGlassy(true);
    box.paint(cr);
  }

  rounded_rect(cr, 0.5, 0.5, kW - 1.0, kH - 1.0, kOuterR);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Layer 2 — Inner card (glassy Tahoe frosted surface)
  {
    m3::Box inner;
    inner.setColor(static_cast<float>(mc.dockFillR * 1.15), static_cast<float>(mc.dockFillG * 1.15),
                   static_cast<float>(mc.dockFillB * 1.15), 0.55f);
    inner.setRadius(static_cast<float>(kInnerR));
    inner.setGeometry(static_cast<float>(kInnerX), static_cast<float>(kInnerY),
                      static_cast<float>(kInnerW), static_cast<float>(kInnerH));
    inner.setGlassy(true);
    inner.paint(cr);
  }

  const double cx = kW / 2.0;

  // Determine active state + snapshot
  const bool onPopup = (app.pointerSurface == app.popupSurface);
  const double px = onPopup ? app.pointerX : -1.0;
  const double py = onPopup ? app.pointerY : -1.0;

  auto* mpris = app.mpris.get();
  if (mpris) {
    mpris->poll_refresh();
    mpris->poll_position();
  }
  const auto snap = mpris ? mpris->snapshot() : eh::mpris::PlayerSnapshot{};
  const bool realActive = snap.active && (!snap.title.empty() || !snap.artist.empty());

  const std::string title_m  = realActive ? (snap.title.empty() ? "Unknown Track" : snap.title) : std::string{};
  const std::string artist_m = realActive ? snap.artist : std::string{};
  const std::string album_m  = realActive ? snap.album  : std::string{};
  const std::string playStatus = realActive ? snap.playback_status : std::string{};
  const bool canPrev  = realActive && snap.can_go_previous;
  const bool canNext  = realActive && snap.can_go_next;
  const bool canPlay  = realActive && snap.can_play;
  const bool canPause = realActive && snap.can_pause;

  // Close button
  const MediaHover hover = compute_hover(px, py, 0, 0, realActive, canPrev, canNext, canPlay, canPause, playStatus);

  const double closeBtnA = (hover == MediaHover::Close) ? 0.25 : 0.12;
  {
    m3::Box closeBox;
    closeBox.setColor(static_cast<float>(mc.outlineR), static_cast<float>(mc.outlineG),
                      static_cast<float>(mc.outlineB), static_cast<float>(closeBtnA));
    closeBox.setRadius(static_cast<float>(kCloseBtnSize / 2.0));
    closeBox.setGeometry(static_cast<float>(kCloseBtnCx - kCloseBtnSize / 2.0),
                         static_cast<float>(kCloseBtnCy - kCloseBtnSize / 2.0),
                         static_cast<float>(kCloseBtnSize), static_cast<float>(kCloseBtnSize));
    closeBox.setGlassy(true);
    closeBox.paint(cr);
  }
  const double closeIconA = (hover == MediaHover::Close) ? 0.90 : 0.70;
  eh::shell::draw_material_glyph(cr, kCloseBtnCx, kCloseBtnCy,
                                  kCloseBtnIcon, "close",
                                  mc.textR, mc.textG, mc.textB, closeIconA);

  // Idle state
  if (!realActive) {
    cairo_new_path(cr);
    cairo_arc(cr, cx, kArtCy, kArtR, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.12, 0.14, 0.16, 1.0);
    cairo_fill(cr);
    eh::shell::draw_material_glyph(cr, cx, kArtCy, 48.0, "music_note",
                                    mc.outlineR, mc.outlineG, mc.outlineB, 0.30);
    draw_text(cr, kColX, kArtTop + kArtSize + kSpacing, kColW,
              "No media playing", "Sans DemiBold 15",
              PANGO_ALIGN_CENTER, 1,
              mc.textR, mc.textG, mc.textB, 0.92);
    cairo_restore(cr);
    return;
  }

  // Album art
  bool drewArt = false;
  if (snap.art && cairo_surface_status(snap.art.get()) == CAIRO_STATUS_SUCCESS) {
    const int iw = cairo_image_surface_get_width(snap.art.get());
    const int ih = cairo_image_surface_get_height(snap.art.get());
    if (iw > 0 && ih > 0) {
      const double s2 = std::min(kArtSize / static_cast<double>(iw),
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
    eh::shell::draw_material_glyph(cr, cx, kArtCy, 48.0, "music_note",
                                    mc.outlineR, mc.outlineG, mc.outlineB, 0.40);
  }
  draw_art_ring(cr, cx, kArtCy, kArtR, mc.accentR, mc.accentG, mc.accentB);

  // Measure text
  const int titleH  = measure_text_h(cr, kColW, title_m.c_str(),  "Sans DemiBold 15", 2);
  const int artistH = artist_m.empty() ? 0 : measure_text_h(cr, kColW, artist_m.c_str(), "Sans 13", 1);
  const int albumH  = album_m.empty()  ? 0 : measure_text_h(cr, kColW, album_m.c_str(),  "Sans 11", 1);

  const auto L = compute_layout(titleH, artistH, albumH,
                                 !artist_m.empty(), !album_m.empty());

  // Cache for click handler
  const_cast<A&>(app).mediaPopupCacheSeekY = L.seekY;
  const_cast<A&>(app).mediaPopupCacheCtrlY = L.ctrlY;

  // Recompute hover with cached layout
  // (We need a real compute_hover with the layout values, but the function
  //  above only uses ctrlY/seekY from the computed layout, so we call again)
  // Actually compute_hover needs seekY and ctrlY. Let's compute a local one.
  const MediaHover realHover = compute_hover(px, py, L.seekY, L.ctrlY,
                                              realActive, canPrev, canNext, canPlay, canPause, playStatus);

  const bool seekHover = (realHover == MediaHover::Seekbar);
  const bool prevHover = (realHover == MediaHover::Prev);
  const bool playHover = (realHover == MediaHover::Play);
  const bool nextHover = (realHover == MediaHover::Next);

  // Title
  draw_text(cr, kColX, L.titleY, kColW,
            title_m.c_str(), "Sans DemiBold 15",
            PANGO_ALIGN_CENTER, 2,
            mc.textR, mc.textG, mc.textB, 0.92);

  // Artist
  if (!artist_m.empty())
    draw_text(cr, kColX, L.artistY, kColW,
              artist_m.c_str(), "Sans 13",
              PANGO_ALIGN_CENTER, 1,
              mc.textR, mc.textG, mc.textB, 0.75);

  // Album
  if (!album_m.empty())
    draw_text(cr, kColX, L.albumY, kColW,
              album_m.c_str(), "Sans 11",
              PANGO_ALIGN_CENTER, 1,
              mc.textR, mc.textG, mc.textB, 0.55);

  // Seek bar
  double pct = 0.0;
  if (snap.duration_us > 0)
    pct = std::clamp(static_cast<double>(snap.position_us) /
                     static_cast<double>(snap.duration_us), 0.0, 1.0);

  const double barY   = L.seekY + (kSeekAreaH - kBarH) / 2.0;
  const double barW   = kColW;
  const double fillW  = barW * pct;
  const double thumbX = kColX + fillW;

  // Track with glassy inner design (Tahoe frosted)
  {
    m3::Box track;
    track.setColor(static_cast<float>(mc.outlineR), static_cast<float>(mc.outlineG),
                   static_cast<float>(mc.outlineB), seekHover ? 0.28f : 0.18f);
    track.setRadius(static_cast<float>(kBarH / 2.0));
    track.setGeometry(static_cast<float>(kColX), static_cast<float>(barY),
                      static_cast<float>(barW), static_cast<float>(kBarH));
    track.setGlassy(true);
    track.paint(cr);
  }
  if (fillW > 0.5) {
    cairo_save(cr);
    cairo_rectangle(cr, kColX, barY - 1.0, fillW, kBarH + 2.0);
    cairo_clip(cr);
    fill_rounded_rect(cr, kColX, barY, barW, kBarH, kBarH / 2.0,
                      mc.accentR, mc.accentG, mc.accentB, seekHover ? 1.0 : 0.90);
    cairo_restore(cr);
  }
  const double thumbR = seekHover ? kThumbR + 2.0 : kThumbR;
  const double thumbOuterA = seekHover ? 1.0 : 0.85;
  cairo_new_path(cr);
  cairo_arc(cr, thumbX, barY + kBarH / 2.0, thumbR, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, thumbOuterA);
  cairo_fill(cr);
  cairo_new_path(cr);
  cairo_arc(cr, thumbX, barY + kBarH / 2.0, thumbR - 2.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, seekHover ? 0.95 : 0.85);
  cairo_fill(cr);

  // Timestamps
  char startBuf[24] = "0:00", endBuf[24] = "0:00";
  format_time_us(startBuf, sizeof(startBuf), snap.position_us);
  format_time_us(endBuf,   sizeof(endBuf),   snap.duration_us);
  const double stampA = 0.50;

  draw_text(cr, kColX, L.stampsY, kColW / 2.0,
            startBuf, "Sans 11", PANGO_ALIGN_LEFT, 1,
            mc.textR, mc.textG, mc.textB, stampA);

  {
    auto* tl  = pango_cairo_create_layout(cr);
    auto* tfd = pango_font_description_from_string("Sans 11");
    pango_layout_set_font_description(tl, tfd);
    pango_font_description_free(tfd);
    pango_layout_set_text(tl, endBuf, -1);
    int tw = 0;
    pango_layout_get_pixel_size(tl, &tw, nullptr);
    g_object_unref(tl);
    draw_text(cr, kColX + barW - static_cast<double>(tw), L.stampsY, barW,
              endBuf, "Sans 11", PANGO_ALIGN_LEFT, 1,
              mc.textR, mc.textG, mc.textB, stampA);
  }

  // Transport controls
  const double totalCtrlW = kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall;
  const double cLeft      = (kW - totalCtrlW) / 2.0;
  const double ccy        = L.ctrlY + kBtnPlay / 2.0;
  const double prevCx     = cLeft + kBtnSmall / 2.0;
  const double playCx     = cLeft + kBtnSmall + kBtnGap + kBtnPlay / 2.0;
  const double nextCx     = cLeft + kBtnSmall + kBtnGap + kBtnPlay + kBtnGap + kBtnSmall / 2.0;

  // Prev (glassy Tahoe button)
  {
    const double prevBgA = canPrev ? (prevHover ? 0.30 : 0.15) : 0.06;
    m3::Box b;
    b.setColor(static_cast<float>(mc.outlineR), static_cast<float>(mc.outlineG),
               static_cast<float>(mc.outlineB), static_cast<float>(prevBgA));
    b.setRadius(static_cast<float>(kBtnSmall / 2.0));
    b.setGeometry(static_cast<float>(prevCx - kBtnSmall / 2.0), static_cast<float>(ccy - kBtnSmall / 2.0),
                  static_cast<float>(kBtnSmall), static_cast<float>(kBtnSmall));
    b.setGlassy(true);
    b.paint(cr);
  }
  const double prevIconA = canPrev ? (prevHover ? 1.0 : 0.85) : 0.30;
  eh::shell::draw_material_glyph(cr, prevCx, ccy, 22.0 * kBoost, "skip_previous",
                                  mc.textR, mc.textG, mc.textB, prevIconA);

  // Play/Pause (glassy on accent)
  {
    const bool playing   = (playStatus == "Playing");
    const double playAlpha = (playing || canPlay || canPause) ? (playHover ? 1.0 : 0.95) : 0.30;
    m3::Box b;
    b.setColor(static_cast<float>(mc.accentR), static_cast<float>(mc.accentG),
               static_cast<float>(mc.accentB), static_cast<float>(playAlpha));
    b.setRadius(static_cast<float>(kBtnPlay / 2.0));
    b.setGeometry(static_cast<float>(playCx - kBtnPlay / 2.0), static_cast<float>(ccy - kBtnPlay / 2.0),
                  static_cast<float>(kBtnPlay), static_cast<float>(kBtnPlay));
    b.setGlassy(true);
    b.paint(cr);
  }
  {
    const bool playing   = (playStatus == "Playing");
    eh::shell::draw_material_glyph(cr, playCx, ccy, 28.0 * kBoost,
                                    playing ? "pause" : "play_arrow",
                                    mc.dockFillR, mc.dockFillG, mc.dockFillB, playHover ? 1.0 : 0.95);
  }

  // Next (glassy)
  {
    const double nextBgA = canNext ? (nextHover ? 0.30 : 0.15) : 0.06;
    m3::Box b;
    b.setColor(static_cast<float>(mc.outlineR), static_cast<float>(mc.outlineG),
               static_cast<float>(mc.outlineB), static_cast<float>(nextBgA));
    b.setRadius(static_cast<float>(kBtnSmall / 2.0));
    b.setGeometry(static_cast<float>(nextCx - kBtnSmall / 2.0), static_cast<float>(ccy - kBtnSmall / 2.0),
                  static_cast<float>(kBtnSmall), static_cast<float>(kBtnSmall));
    b.setGlassy(true);
    b.paint(cr);
  }
  const double nextIconA = canNext ? (nextHover ? 1.0 : 0.85) : 0.30;
  eh::shell::draw_material_glyph(cr, nextCx, ccy, 22.0 * kBoost, "skip_next",
                                  mc.textR, mc.textG, mc.textB, nextIconA);

  cairo_restore(cr);
}

} // namespace eh::shell::dock::popup::media_player
