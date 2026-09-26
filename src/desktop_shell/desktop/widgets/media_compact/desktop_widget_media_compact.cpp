#include "desktop_shell/desktop/widgets/media_compact/desktop_widget_media_compact.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "services/mpris/mpris_player.hpp"
#include "m3/core/primitives/box.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

namespace eh::shell::desktop {
namespace {

// Layout (matches the media-player popup card).
constexpr double kW = 400.0;
constexpr double kH = 188.0;
constexpr double kOuterR = 28.0;
constexpr double kPad = 20.0;

constexpr double kArtS = 64.0;
constexpr double kArtX = kPad;
constexpr double kArtY = kPad;
constexpr double kArtR = 12.0;

constexpr double kTextX = kArtX + kArtS + 14.0;
constexpr double kTextX1 = kW - kPad;
constexpr double kTextW = kTextX1 - kTextX;

constexpr double kTitleY = 28.0;
constexpr double kArtistY = 56.0;

constexpr double kSeekX0 = kPad;
constexpr double kSeekX1 = kW - kPad;
constexpr double kSeekW = kSeekX1 - kSeekX0;
constexpr double kBarY = 102.0;
constexpr double kBarH = 4.0;
constexpr double kKnobR = 5.0;

constexpr double kStampsY = 114.0;

constexpr double kCtrlCy = 154.0;
constexpr double kShuffleCx = 80.0;
constexpr double kPrevCx = 136.0;
constexpr double kPlayCx = 200.0;
constexpr double kNextCx = 264.0;
constexpr double kRepeatCx = 320.0;

constexpr double kMarqueePeriodSec = 9.0;

double marquee_triangle_offset(double range_px, double tsec, double period_sec) {
  if (range_px <= 0.5 || period_sec <= 0.0) return 0.0;
  const double u = std::fmod(tsec / period_sec, 1.0);
  if (u < 0.0) return 0.0;
  const double tri = u < 0.5 ? (u * 2.0) : (2.0 - u * 2.0);
  return tri * range_px;
}

int natural_text_width_px(cairo_t* cr, const char* text, const char* fd_str) {
  auto* l = pango_cairo_create_layout(cr);
  auto* fd = pango_font_description_from_string(fd_str);
  pango_layout_set_font_description(l, fd);
  pango_font_description_free(fd);
  pango_layout_set_text(l, text, -1);
  int w = 0;
  pango_layout_get_pixel_size(l, &w, nullptr);
  g_object_unref(l);
  return w;
}

} // anonymous namespace

using eh::shell::shared::rounded_rect;

DesktopMediaCompactWidget::DesktopMediaCompactWidget(eh::mpris::DockMpris* mpris)
    : m_mpris(mpris) {}

void DesktopMediaCompactWidget::create() {
  if (!m_mpris) {
    m_active = false;
    return;
  }
  m_mpris->poll_refresh();
  const auto snap = m_mpris->snapshot();
  m_active = snap.active && (!snap.title.empty() || !snap.artist.empty());
  m_canPrev = m_active && snap.can_go_previous;
  m_canNext = m_active && snap.can_go_next;
  m_durationUs = m_active ? snap.duration_us : 0;
  m_isStream = m_active && snap.track_url_raw.find("twitch.tv") != std::string::npos;
}

uint32_t DesktopMediaCompactWidget::animIntervalMs() const {
  // Smooth marquee while the title/artist scrolls; otherwise fully idle.
  if (m_marqueeActive && m_active) return 33;
  return 0;
}

int DesktopMediaCompactWidget::hit_test_btn(double x, double y) const {
  if (!m_active || !m_mpris) return -1;

  if (m_durationUs > 0 && !m_isStream) {
    if (y >= 92.0 && y <= 112.0 && x >= kSeekX0 && x <= kSeekX1) return 3;
  }

  auto hit_circle = [](double px, double py, double cx, double cy, double r) {
    const double dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r;
  };
  if (hit_circle(x, y, kPlayCx, kCtrlCy, 24.0)) return 1;
  if (hit_circle(x, y, kPrevCx, kCtrlCy, 20.0)) return 0;
  if (hit_circle(x, y, kNextCx, kCtrlCy, 20.0)) return 2;
  if (hit_circle(x, y, kShuffleCx, kCtrlCy, 20.0)) return 4;
  if (hit_circle(x, y, kRepeatCx, kCtrlCy, 20.0)) return 5;
  return -1;
}

bool DesktopMediaCompactWidget::on_click(double x, double y) {
  if (!m_mpris) return false;
  const int zone = hit_test_btn(x, y);

  if (zone == 3) {
    const auto snap = m_mpris->snapshot();
    if (snap.duration_us > 0) {
      const double frac = std::clamp((x - kSeekX0) / kSeekW, 0.0, 1.0);
      m_mpris->set_position(static_cast<int64_t>(static_cast<double>(snap.duration_us) * frac));
    }
    return true;
  }

  if (zone < 0) return false;
  switch (zone) {
    case 0: if (m_canPrev) m_mpris->previous(); return true;
    case 1: m_mpris->play_pause(); return true;
    case 2: if (m_canNext) m_mpris->next(); return true;
    case 4: m_mpris->toggle_shuffle(); return true;
    case 5: m_mpris->cycle_loop_status(); return true;
    default: return false;
  }
}

void DesktopMediaCompactWidget::on_motion(double x, double y) {
  if (x < 0 || y < 0) {
    m_hoverBtn = -1;
    m_hoverProg = false;
    return;
  }
  const int btn = hit_test_btn(x, y);
  if (btn == 3) {
    m_hoverBtn = -1;
    m_hoverProg = true;
  } else {
    m_hoverBtn = btn;
    m_hoverProg = false;
  }
}

void DesktopMediaCompactWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
  if (!m_mpris) return;

  m_mpris->poll_position();
  const auto snap = m_mpris->snapshot();

  const bool realActive = snap.active && (!snap.title.empty() || !snap.artist.empty());
  const std::string title_m =
      realActive ? (snap.title.empty() ? std::string("Unknown Track") : snap.title) : std::string{};
  const std::string artist_m = realActive ? snap.artist : std::string{};
  const bool playing = (snap.playback_status == "Playing");
  const bool shuffleOn = realActive && snap.shuffle;
  const std::string loopMode = realActive ? snap.loop_status : std::string{};
  const bool loopOn = (loopMode == "Track" || loopMode == "Playlist");

  const auto mc = eh::config::derived_chrome_colors(sc.appearance);

  m_marqueeActive = false;
  const double tsec =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

  auto draw_scrolled_line = [&](double y, double max_w, const std::string& text, const char* fd,
                                double r, double g, double b, double a, double phase) {
    const int nat_w = natural_text_width_px(cr, text.c_str(), fd);
    auto* l = pango_cairo_create_layout(cr);
    auto* fdd = pango_font_description_from_string(fd);
    pango_layout_set_font_description(l, fdd);
    pango_font_description_free(fdd);
    pango_layout_set_text(l, text.c_str(), -1);
    int line_h = 0;
    pango_layout_get_pixel_size(l, nullptr, &line_h);
    cairo_set_source_rgba(cr, r, g, b, a);
    if (static_cast<double>(nat_w) > max_w + 0.5) {
      m_marqueeActive = true;
      const double range = static_cast<double>(nat_w) - max_w;
      const double off = marquee_triangle_offset(range, tsec + phase, kMarqueePeriodSec);
      cairo_save(cr);
      cairo_rectangle(cr, kTextX, y - 2.0, max_w, static_cast<double>(line_h) + 4.0);
      cairo_clip(cr);
      cairo_move_to(cr, kTextX - off, y);
      pango_cairo_show_layout(cr, l);
      cairo_restore(cr);
    } else {
      cairo_move_to(cr, kTextX, y);
      pango_cairo_show_layout(cr, l);
    }
    g_object_unref(l);
  };

  // Card.
  cairo_save(cr);
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
  rounded_rect(cr, 0, 0, kW, kH, kOuterR);
  cairo_clip(cr);

  if (!realActive) {
    eh::shell::draw_material_glyph(cr, kW * 0.5, 74.0, 30.0, "music_note",
                                    mc.textR, mc.textG, mc.textB, 0.45);
    auto* l = pango_cairo_create_layout(cr);
    auto* fd = pango_font_description_from_string("Inter 15");
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(l, "No media playing", -1);
    int tw = 0;
    pango_layout_get_pixel_size(l, &tw, nullptr);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.70);
    cairo_move_to(cr, (kW - static_cast<double>(tw)) * 0.5, 112.0);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
    cairo_restore(cr);
    return;
  }

  // Album art.
  bool drewArt = false;
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
      drewArt = true;
    }
  }
  if (!drewArt) {
    rounded_rect(cr, kArtX, kArtY, kArtS, kArtS, kArtR);
    cairo_set_source_rgba(cr, 0.12, 0.14, 0.16, 1.0);
    cairo_fill(cr);
    eh::shell::draw_material_glyph(cr, kArtX + kArtS * 0.5, kArtY + kArtS * 0.5, 28.0, "music_note",
                                    0.70, 0.75, 0.80, 0.60);
  }

  // Title + artist (scroll when overflowing).
  draw_scrolled_line(kTitleY, kTextW, title_m, "Inter Bold 18",
                     mc.textR, mc.textG, mc.textB, 0.95, 0.0);
  if (!artist_m.empty())
    draw_scrolled_line(kArtistY, kTextW, artist_m, "Inter 14",
                       mc.textR, mc.textG, mc.textB, 0.62, 2.7);

  // Seek bar.
  double pct = 0.0;
  if (snap.duration_us > 0)
    pct = std::clamp(static_cast<double>(snap.position_us) /
                     static_cast<double>(snap.duration_us), 0.0, 1.0);
  const double fillW = kSeekW * pct;
  const double thumbX = kSeekX0 + fillW;
  rounded_rect(cr, kSeekX0, kBarY, kSeekW, kBarH, kBarH / 2.0);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.22);
  cairo_fill(cr);
  if (fillW > 0.5) {
    rounded_rect(cr, kSeekX0, kBarY, fillW, kBarH, kBarH / 2.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, m_hoverProg ? 1.0 : 0.95);
    cairo_fill(cr);
  }
  cairo_new_path(cr);
  cairo_arc(cr, thumbX, kBarY + kBarH / 2.0, kKnobR, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, m_hoverProg ? 1.0 : 0.95);
  cairo_fill(cr);

  // Timestamps: elapsed left, remaining right.
  {
    char startBuf[24] = "0:00", remainBuf[32] = "-0:00";
    auto fmt = [](char* buf, size_t sz, int64_t us) {
      if (us < 0) us = 0;
      const auto total = static_cast<uint64_t>(us) / 1'000'000ULL;
      std::snprintf(buf, sz, "%llu:%02llu",
                    static_cast<unsigned long long>(total / 60ULL),
                    static_cast<unsigned long long>(total % 60ULL));
    };
    fmt(startBuf, sizeof(startBuf), snap.position_us);
    int64_t remain_us = 0;
    if (snap.duration_us > 0)
      remain_us = std::max<int64_t>(0, snap.duration_us - snap.position_us);
    char tmp[24] = "0:00";
    fmt(tmp, sizeof(tmp), remain_us);
    std::snprintf(remainBuf, sizeof(remainBuf), "-%s", tmp);

    auto* l = pango_cairo_create_layout(cr);
    auto* fd = pango_font_description_from_string("Inter 11");
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(l, startBuf, -1);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.55);
    cairo_move_to(cr, kSeekX0, kStampsY);
    pango_cairo_show_layout(cr, l);
    pango_layout_set_text(l, remainBuf, -1);
    int tw = 0;
    pango_layout_get_pixel_size(l, &tw, nullptr);
    cairo_move_to(cr, kSeekX1 - static_cast<double>(tw), kStampsY);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
  }

  // Transport controls: shuffle / prev / play / next / repeat.
  eh::shell::draw_material_glyph(cr, kShuffleCx, kCtrlCy, 26.0, "shuffle",
                                  1.0, 1.0, 1.0, shuffleOn ? 1.0 : 0.45);
  eh::shell::draw_material_glyph(cr, kPrevCx, kCtrlCy, 30.0, "skip_previous",
                                  1.0, 1.0, 1.0, snap.can_go_previous ? 1.0 : 0.30);
  eh::shell::draw_material_glyph(cr, kPlayCx, kCtrlCy, 40.0,
                                  playing ? "pause" : "play_arrow",
                                  1.0, 1.0, 1.0,
                                  (snap.can_play || snap.can_pause || playing) ? 1.0 : 0.30);
  eh::shell::draw_material_glyph(cr, kNextCx, kCtrlCy, 30.0, "skip_next",
                                  1.0, 1.0, 1.0, snap.can_go_next ? 1.0 : 0.30);
  eh::shell::draw_material_glyph(cr, kRepeatCx, kCtrlCy, 26.0,
                                  loopMode == "Track" ? "repeat_one" : "repeat",
                                  1.0, 1.0, 1.0, loopOn ? 1.0 : 0.45);

  cairo_restore(cr);
}

} // namespace eh::shell::desktop
