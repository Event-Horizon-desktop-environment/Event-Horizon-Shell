#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "services/mpris/mpris_player.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "m3/core/primitives/box.hpp"
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

namespace eh::shell::dock::popup::media_player {
namespace {

// ════════════════════════════════════════════════════════════════════════════
// iOS-style now-playing card (400x196): art + scrolling title/artist, wave
// bars, seekbar with elapsed/remaining stamps, 5 transport controls.
// ════════════════════════════════════════════════════════════════════════════

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

// Triangle-wave marquee offset: rests at 0, traverses `range` and back.
double marquee_triangle_offset(double range_px, double tsec, double period_sec) {
  if (range_px <= 0.5 || period_sec <= 0.0) return 0.0;
  const double u = std::fmod(tsec / period_sec, 1.0);
  if (u < 0.0) return 0.0;
  const double tri = u < 0.5 ? (u * 2.0) : (2.0 - u * 2.0);
  return tri * range_px;
}

double marquee_now_sec() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int natural_text_width_px(cairo_t* cr, const char* text, const char* fd_str) {
  auto* l  = pango_cairo_create_layout(cr);
  auto* fd = pango_font_description_from_string(fd_str);
  pango_layout_set_font_description(l, fd);
  pango_font_description_free(fd);
  pango_layout_set_text(l, text, -1);
  int w = 0;
  pango_layout_get_pixel_size(l, &w, nullptr);
  g_object_unref(l);
  return w;
}

// Single-line text that scrolls when wider than `max_w`. Reports scrolling
// through the shared marquee flag so timer drivers keep repainting.
void draw_scrolled_line(cairo_t* cr, double x, double y, double max_w,
                        const std::string& text, const char* fd_str,
                        double r, double g, double b, double a,
                        double tsec, double period_sec) {
  const int nat_w = natural_text_width_px(cr, text.c_str(), fd_str);
  auto* l  = pango_cairo_create_layout(cr);
  auto* fd = pango_font_description_from_string(fd_str);
  pango_layout_set_font_description(l, fd);
  pango_font_description_free(fd);
  pango_layout_set_text(l, text.c_str(), -1);
  int line_h = 0;
  pango_layout_get_pixel_size(l, nullptr, &line_h);
  cairo_set_source_rgba(cr, r, g, b, a);
  if (static_cast<double>(nat_w) > max_w + 0.5) {
    media_popup_marquee_state() = true;
    const double range = static_cast<double>(nat_w) - max_w;
    const double off = marquee_triangle_offset(range, tsec, period_sec);
    cairo_save(cr);
    cairo_rectangle(cr, x, y - 2.0, max_w, static_cast<double>(line_h) + 4.0);
    cairo_clip(cr);
    cairo_move_to(cr, x - off, y);
    pango_cairo_show_layout(cr, l);
    cairo_restore(cr);
  } else {
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
  }
  g_object_unref(l);
}

} // anonymous namespace

// Shared paint/hit geometry (single source; also used by
// media_player_popup.cpp click handling — see Docs/hit-testing.md).
// ════════════════════════════════════════════════════════════════════════════

constexpr double kW = 400.0;
constexpr double kH = 188.0;

constexpr double kOuterR = 28.0;
constexpr double kPad    = 20.0;

constexpr double kArtS = 64.0;
constexpr double kArtX = kPad;
constexpr double kArtY = kPad;
constexpr double kArtR = 12.0;

constexpr double kTextX = kArtX + kArtS + 14.0;   // 98
constexpr double kTextX1 = kW - kPad;             // 380
constexpr double kTextW  = kTextX1 - kTextX;      // 282

constexpr double kTitleY  = 28.0;
constexpr double kArtistY = 56.0;

constexpr double kSeekX0 = kPad;
constexpr double kSeekX1 = kW - kPad;
constexpr double kSeekW  = kSeekX1 - kSeekX0;     // 360
constexpr double kBarY   = 102.0;
constexpr double kBarH   = 4.0;
constexpr double kKnobR  = 5.0;
constexpr double kSeekTop = 92.0;
constexpr double kSeekBot = 112.0;

constexpr double kStampsY = 114.0;

constexpr double kCtrlCy = 154.0;
constexpr double kShuffleCx = 80.0;
constexpr double kPrevCx    = 136.0;
constexpr double kPlayCx    = 200.0;
constexpr double kNextCx    = 264.0;
constexpr double kRepeatCx  = 320.0;
constexpr double kBtnHitR   = 20.0;
constexpr double kPlayHitR  = 24.0;

constexpr double kMarqueePeriodSec = 9.0;

// Hover part enum.

enum class MediaHover : int { None = 0, Close, Shuffle, Prev, Play, Next, Repeat, Seekbar };

struct Layout {
  double titleY  = kTitleY;
  double artistY = kArtistY;
  double seekY   = kSeekTop;
  double stampsY = kStampsY;
  double ctrlY   = kCtrlCy;
};


inline Layout compute_layout() {
  return Layout{};
}


inline bool hit_circle(double px, double py, double cx, double cy, double r) {
  const double dx = px - cx, dy = py - cy;
  return dx * dx + dy * dy <= r * r;
}


inline MediaHover compute_hover(double px, double py,
                          const Layout&,
                          bool active,
                          bool canPrev, bool canNext,
                          bool canPlay, bool canPause,
                          const std::string& playbackStatus) {
  (void)canPrev; (void)canNext; (void)canPlay; (void)canPause; (void)playbackStatus;
  if (!active) return MediaHover::None;

  if (hit_circle(px, py, kPlayCx, kCtrlCy, kPlayHitR)) return MediaHover::Play;
  if (hit_circle(px, py, kPrevCx, kCtrlCy, kBtnHitR)) return MediaHover::Prev;
  if (hit_circle(px, py, kNextCx, kCtrlCy, kBtnHitR)) return MediaHover::Next;
  if (hit_circle(px, py, kShuffleCx, kCtrlCy, kBtnHitR)) return MediaHover::Shuffle;
  if (hit_circle(px, py, kRepeatCx, kCtrlCy, kBtnHitR)) return MediaHover::Repeat;

  if (py >= kSeekTop && py < kSeekBot && px >= kSeekX0 && px <= kSeekX1)
    return MediaHover::Seekbar;

  return MediaHover::None;
}

// ════════════════════════════════════════════════════════════════════════════
// Paint
// ════════════════════════════════════════════════════════════════════════════

template<typename A>
void dock_media_player_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);

  cairo_save(cr);

  // Layer 1 — Outer card (dark glass).
  {
    const float outerAlpha = static_cast<float>(0.92 * sc.appearance.overlayOpacityWidgetCard);
    m3::Box box;
    box.setColor(static_cast<float>(mc.dockFillR * 0.30), static_cast<float>(mc.dockFillG * 0.30),
                 static_cast<float>(mc.dockFillB * 0.30), outerAlpha);
    box.setRadius(static_cast<float>(kOuterR));
    box.setGeometry(0, 0, kW, kH);
    box.setGlassy(true);
    box.paint(cr);
  }

  rounded_rect(cr, 0.5, 0.5, kW - 1.0, kH - 1.0, kOuterR);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Determine active state + snapshot. Marquee starts settled each frame;
  // draw_scrolled_line re-arms it when text actually overflows.
  media_popup_marquee_state() = false;

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
  const std::string playStatus = realActive ? snap.playback_status : std::string{};
  const bool canPrev  = realActive && snap.can_go_previous;
  const bool canNext  = realActive && snap.can_go_next;
  const bool canPlay  = realActive && snap.can_play;
  const bool canPause = realActive && snap.can_pause;
  const bool playing  = (playStatus == "Playing");
  const bool shuffleOn = realActive && snap.shuffle;
  const std::string loopMode = realActive ? snap.loop_status : std::string{};
  const bool loopOn = (loopMode == "Track" || loopMode == "Playlist");

  const Layout L = compute_layout();

  // Cache for click handler.
  const_cast<A&>(app).mediaPopupCacheSeekY = L.seekY;
  const_cast<A&>(app).mediaPopupCacheCtrlY = L.ctrlY;

  const MediaHover realHover = compute_hover(px, py, L,
                                              realActive, canPrev, canNext, canPlay, canPause, playStatus);
  const bool seekHover = (realHover == MediaHover::Seekbar);

  // Idle state.
  if (!realActive) {
    fill_rounded_rect(cr, kArtX, kArtY, kArtS, kArtS, kArtR, 0.12, 0.14, 0.16, 1.0);
    eh::shell::draw_material_glyph(cr, kArtX + kArtS * 0.5, kArtY + kArtS * 0.5, 28.0, "music_note",
                                    0.70, 0.75, 0.80, 0.60);
    auto* l  = pango_cairo_create_layout(cr);
    auto* fd = pango_font_description_from_string("Inter 15");
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(l, "No media playing", -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(l, &tw, &th);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.85);
    cairo_move_to(cr, (kW - static_cast<double>(tw)) * 0.5, 108.0);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
    cairo_restore(cr);
    return;
  }

  // Album art (rounded square; dim placeholder when missing).
  if (snap.art && cairo_surface_status(snap.art.get()) == CAIRO_STATUS_SUCCESS) {
    const int iw = cairo_image_surface_get_width(snap.art.get());
    const int ih = cairo_image_surface_get_height(snap.art.get());
    if (iw > 0 && ih > 0) {
      const double s2 = std::max(kArtS / static_cast<double>(iw),
                                 kArtS / static_cast<double>(ih));
      cairo_save(cr);
      rounded_rect(cr, kArtX, kArtY, kArtS, kArtS, kArtR);
      cairo_clip(cr);
      cairo_translate(cr, kArtX + (kArtS - iw * s2) * 0.5, kArtY + (kArtS - ih * s2) * 0.5);
      cairo_scale(cr, s2, s2);
      cairo_set_source_surface(cr, snap.art.get(), 0, 0);
      cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      fill_rounded_rect(cr, kArtX, kArtY, kArtS, kArtS, kArtR, 0.12, 0.14, 0.16, 1.0);
    }
  } else {
    fill_rounded_rect(cr, kArtX, kArtY, kArtS, kArtS, kArtR, 0.12, 0.14, 0.16, 1.0);
    eh::shell::draw_material_glyph(cr, kArtX + kArtS * 0.5, kArtY + kArtS * 0.5, 28.0, "music_note",
                                    0.70, 0.75, 0.80, 0.60);
  }

  // Title + artist (scroll when overflowing).
  const double tsec = marquee_now_sec();
  draw_scrolled_line(cr, kTextX, L.titleY, kTextW, title_m, "Inter Bold 18",
                     mc.textR, mc.textG, mc.textB, 0.95, tsec, kMarqueePeriodSec);
  if (!artist_m.empty())
    draw_scrolled_line(cr, kTextX, L.artistY, kTextW, artist_m, "Inter 14",
                       mc.textR, mc.textG, mc.textB, 0.62, tsec + 2.7, kMarqueePeriodSec);

  // Seek bar.
  double pct = 0.0;
  if (snap.duration_us > 0)
    pct = std::clamp(static_cast<double>(snap.position_us) /
                     static_cast<double>(snap.duration_us), 0.0, 1.0);
  const double barY = kBarY;
  const double fillW = kSeekW * pct;
  const double thumbX = kSeekX0 + fillW;
  fill_rounded_rect(cr, kSeekX0, barY, kSeekW, kBarH, kBarH / 2.0, 1.0, 1.0, 1.0, 0.22);
  if (fillW > 0.5)
    fill_rounded_rect(cr, kSeekX0, barY, fillW, kBarH, kBarH / 2.0, 1.0, 1.0, 1.0,
                      seekHover ? 1.0 : 0.95);
  cairo_new_path(cr);
  cairo_arc(cr, thumbX, barY + kBarH / 2.0, kKnobR, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, seekHover ? 1.0 : 0.95);
  cairo_fill(cr);

  // Timestamps: elapsed left, remaining right.
  char startBuf[24] = "0:00", remainBuf[32] = "-0:00";
  format_time_us(startBuf, sizeof(startBuf), snap.position_us);
  {
    int64_t remain_us = 0;
    if (snap.duration_us > 0)
      remain_us = std::max<int64_t>(0, snap.duration_us - snap.position_us);
    char tmp[24] = "0:00";
    format_time_us(tmp, sizeof(tmp), remain_us);
    std::snprintf(remainBuf, sizeof(remainBuf), "-%s", tmp);
  }
  {
    auto* l  = pango_cairo_create_layout(cr);
    auto* fd = pango_font_description_from_string("Inter 11");
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(l, startBuf, -1);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.55);
    cairo_move_to(cr, kSeekX0, L.stampsY);
    pango_cairo_show_layout(cr, l);
    pango_layout_set_text(l, remainBuf, -1);
    int tw = 0;
    pango_layout_get_pixel_size(l, &tw, nullptr);
    cairo_move_to(cr, kSeekX1 - static_cast<double>(tw), L.stampsY);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
  }

  // Transport controls: shuffle / prev / play / next / repeat.
  const double shuffleA = shuffleOn ? 1.0 : 0.45;
  eh::shell::draw_material_glyph(cr, kShuffleCx, kCtrlCy, 26.0, "shuffle",
                                  1.0, 1.0, 1.0, shuffleA);
  const double prevA = canPrev ? 1.0 : 0.30;
  eh::shell::draw_material_glyph(cr, kPrevCx, kCtrlCy, 30.0, "skip_previous",
                                  1.0, 1.0, 1.0, prevA);
  eh::shell::draw_material_glyph(cr, kPlayCx, kCtrlCy, 40.0,
                                  playing ? "pause" : "play_arrow",
                                  1.0, 1.0, 1.0, (canPlay || canPause || playing) ? 1.0 : 0.30);
  const double nextA = canNext ? 1.0 : 0.30;
  eh::shell::draw_material_glyph(cr, kNextCx, kCtrlCy, 30.0, "skip_next",
                                  1.0, 1.0, 1.0, nextA);
  eh::shell::draw_material_glyph(cr, kRepeatCx, kCtrlCy, 26.0,
                                  loopMode == "Track" ? "repeat_one" : "repeat",
                                  1.0, 1.0, 1.0, loopOn ? 1.0 : 0.45);

  cairo_restore(cr);
}

} // namespace eh::shell::dock::popup::media_player
