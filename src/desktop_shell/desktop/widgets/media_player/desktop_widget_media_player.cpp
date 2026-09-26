#include "desktop_shell/desktop/widgets/media_player/desktop_widget_media_player.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"
#include "services/mpris/mpris_player.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "m3/core/primitives/box.hpp"
#include "m3/core/primitives/box.hpp"

namespace eh::shell::desktop {
namespace {

using eh::shell::shared::rounded_rect;

// Liquid Glass now-playing card, fixed 340x440.
constexpr double kW = 340.0;
constexpr double kH = 440.0;
constexpr double kOuterR = 28.0;
constexpr double kPad = 20.0;

constexpr double kArtS = 264.0;
constexpr double kArtX = (kW - kArtS) * 0.5;
constexpr double kArtY = 20.0;
constexpr double kArtR = 20.0;

constexpr double kTextX = 30.0;
constexpr double kTextX1 = kW - 30.0;
constexpr double kTextW = kTextX1 - kTextX;
constexpr double kTitleY = 292.0;
constexpr double kArtistY = 316.0;

constexpr double kWaveX0 = 30.0;
constexpr double kWaveX1 = kW - 30.0;
constexpr double kWaveW = kWaveX1 - kWaveX0;
constexpr double kWaveMidY = 351.0;
constexpr double kWaveHalfH = 11.0;

constexpr double kSeekX0 = 30.0;
constexpr double kSeekX1 = kW - 30.0;
constexpr double kSeekW = kSeekX1 - kSeekX0;
constexpr double kBarY = 370.0;
constexpr double kBarH = 4.0;
constexpr double kKnobR = 5.0;
constexpr double kSeekTop = 362.0;
constexpr double kSeekBot = 380.0;

constexpr double kStampsY = 380.0;

constexpr double kCtrlCy = 410.0;
constexpr double kShuffleCx = 65.0;
constexpr double kPrevCx = 113.0;
constexpr double kPlayCx = 170.0;
constexpr double kNextCx = 227.0;
constexpr double kRepeatCx = 275.0;

constexpr double kMarqueePeriodSec = 9.0;
constexpr int kWaveSegments = 28;

void fill_rounded_rect(cairo_t* cr,
                       double x, double y, double w, double h, double r,
                       double rr, double gg, double bb, double aa) {
  rounded_rect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, rr, gg, bb, aa);
  cairo_fill(cr);
}

void format_time_mmss(char* buf, size_t sz, int64_t us, bool neg) {
  if (us < 0) us = 0;
  const auto total = static_cast<uint64_t>(us) / 1'000'000ULL;
  const auto mins = total / 60ULL;
  const auto secs = total % 60ULL;
  if (neg)
    std::snprintf(buf, sz, "-%llu:%02llu",
                  static_cast<unsigned long long>(mins),
                  static_cast<unsigned long long>(secs));
  else
    std::snprintf(buf, sz, "%llu:%02llu",
                  static_cast<unsigned long long>(mins),
                  static_cast<unsigned long long>(secs));
}

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
  const auto snap = m_mpris->snapshot();
  m_active = snap.active && (!snap.title.empty() || !snap.artist.empty());
  if (m_active) {
    m_title = snap.title.empty() ? "Unknown Track" : snap.title;
    m_artist = snap.artist;
    m_playbackStatus = snap.playback_status;
    m_canPrev = snap.can_go_previous;
    m_canNext = snap.can_go_next;
    m_canPlay = snap.can_play;
    m_canPause = snap.can_pause;
    m_shuffle = snap.shuffle;
    m_loopMode = snap.loop_status;
  } else {
    m_title.clear();
    m_artist.clear();
    m_playbackStatus.clear();
    m_canPrev = m_canNext = m_canPlay = m_canPause = false;
    m_shuffle = false;
    m_loopMode.clear();
  }
}

uint32_t DesktopMediaPlayerWidget::animIntervalMs() const {
  // Smooth CAVA + marquee while playing/scrolling; fully idle otherwise.
  if (m_marqueeActive && m_active) return 33;
  if (m_cavaActive && m_playbackStatus == "Playing") return 33;
  return 0;
}

DesktopMediaPlayerWidget::HoverPart
DesktopMediaPlayerWidget::hit_test_hover(double x, double y) const {
  if (!m_active) return HoverNone;

  if (hit_circle(x, y, kPlayCx, kCtrlCy, 24.0)) return HoverPlay;
  if (hit_circle(x, y, kPrevCx, kCtrlCy, 20.0)) return HoverPrev;
  if (hit_circle(x, y, kNextCx, kCtrlCy, 20.0)) return HoverNext;
  if (hit_circle(x, y, kShuffleCx, kCtrlCy, 20.0)) return HoverShuffle;
  if (hit_circle(x, y, kRepeatCx, kCtrlCy, 20.0)) return HoverRepeat;

  if (y >= kSeekTop && y < kSeekBot && x >= kSeekX0 && x <= kSeekX1)
    return HoverSeekbar;

  return HoverNone;
}

bool DesktopMediaPlayerWidget::on_click(double x, double y) {
  const HoverPart part = hit_test_hover(x, y);
  if (!m_active || !m_mpris) {
    // A button can be visible while m_active is false, so this is the one place
    // that explains a click vanishing without a D-Bus call ever being made.
    eh::shell_log::mpris_dbus("desktop media click dropped: part=", static_cast<int>(part),
                              " active=", m_active ? 1 : 0, " mpris=", m_mpris ? 1 : 0);
    return false;
  }
  eh::shell_log::mpris_dbus("desktop media click: part=", static_cast<int>(part), " at ", x, ",", y);

  switch (part) {
    case HoverPlay: m_mpris->play_pause(); return true;
    case HoverPrev: if (m_canPrev) m_mpris->previous(); return true;
    case HoverNext: if (m_canNext) m_mpris->next(); return true;
    case HoverShuffle: m_mpris->toggle_shuffle(); return true;
    case HoverRepeat: m_mpris->cycle_loop_status(); return true;
    case HoverSeekbar: {
      m_mpris->poll_position();
      const auto snap = m_mpris->snapshot();
      if (snap.duration_us > 0) {
        const double pct = std::clamp((x - kSeekX0) / kSeekW, 0.0, 1.0);
        m_mpris->set_position(static_cast<int64_t>(pct * snap.duration_us));
        return true;
      }
      return false;
    }
    case HoverNone:
      return false;
  }
  return false;
}

void DesktopMediaPlayerWidget::on_motion(double x, double y) {
  if (x < 0 || y < 0) {
    m_hoverPart = HoverNone;
    return;
  }
  m_hoverPart = hit_test_hover(x, y);
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

  const int nCava = std::min(static_cast<int>(m_cavaValues.size()), m_cfg.bars);
  if (nCava <= 0) {
    m_cavaActive = false;
    return false;
  }
  bool changed = false;
  for (int i = 0; i < kWaveSegments; ++i) {
    const int cavaIdx = (i * nCava) / kWaveSegments;
    const int raw = m_cavaValues[static_cast<size_t>(cavaIdx)];
    const double t = std::sqrt(std::clamp(static_cast<double>(raw) / 100.0, 0.0, 1.0));
    if (std::abs(t - m_waveTarget[static_cast<size_t>(i)]) > 1.0 / 512.0) changed = true;
    m_waveTarget[static_cast<size_t>(i)] = t;
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
  for (int i = 0; i < kWaveSegments; ++i) {
    const double target = m_waveTarget[static_cast<size_t>(i)];
    double& display = m_waveSmoothed[static_cast<size_t>(i)];
    const double delta = target - display;
    if (std::fabs(delta) < kEpsilon) {
      if (display != target) display = target;
      continue;
    }
    display += delta * alpha;
    if (std::fabs(target - display) >= kEpsilon) converged = false;
  }
  m_waveSettled = converged;
}

void DesktopMediaPlayerWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);

  cairo_save(cr);

  // ── Liquid Glass card ────────────────────────────────────────────────
  {
    const float outerAlpha = static_cast<float>(0.78 * sc.appearance.overlayOpacityWidgetCard);
    m3::Box box;
    box.setColor(static_cast<float>(mc.dockFillR * 0.45), static_cast<float>(mc.dockFillG * 0.45),
                 static_cast<float>(mc.dockFillB * 0.45), outerAlpha);
    box.setRadius(static_cast<float>(kOuterR));
    box.setGeometry(0, 0, kW, kH);
    box.setGlassy(true);
    box.paint(cr);
  }
  rounded_rect(cr, 0.5, 0.5, kW - 1.0, kH - 1.0, kOuterR);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.16);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  // Specular top-edge sheen (the Liquid Glass highlight).
  cairo_save(cr);
  rounded_rect(cr, 0, 0, kW, kH, kOuterR);
  cairo_clip(cr);
  {
    cairo_pattern_t* sheen = cairo_pattern_create_linear(0, 0, 0, 64.0);
    cairo_pattern_add_color_stop_rgba(sheen, 0.0, 1.0, 1.0, 1.0, 0.14);
    cairo_pattern_add_color_stop_rgba(sheen, 1.0, 1.0, 1.0, 1.0, 0.0);
    cairo_rectangle(cr, 0, 0, kW, 64.0);
    cairo_set_source(cr, sheen);
    cairo_fill(cr);
    cairo_pattern_destroy(sheen);
  }
  cairo_restore(cr);
  rounded_rect(cr, 0, 0, kW, kH, kOuterR);
  cairo_clip(cr);

  // Idle state.
  if (!m_active) {
    fill_rounded_rect(cr, kArtX, kArtY, kArtS, kArtS, kArtR, 0.12, 0.14, 0.16, 1.0);
    eh::shell::draw_material_glyph(cr, kArtX + kArtS * 0.5, kArtY + kArtS * 0.5, 56.0, "music_note",
                                    0.70, 0.75, 0.80, 0.40);
    auto* l = pango_cairo_create_layout(cr);
    auto* fd = pango_font_description_from_string("Inter 15");
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(l, "No media playing", -1);
    int tw = 0;
    pango_layout_get_pixel_size(l, &tw, nullptr);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.60);
    cairo_move_to(cr, (kW - static_cast<double>(tw)) * 0.5, kArtY + kArtS + 16.0);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
    cairo_restore(cr);
    return;
  }

  if (m_mpris) m_mpris->poll_position();
  const auto snap = m_mpris ? m_mpris->snapshot() : eh::mpris::PlayerSnapshot{};
  const bool playing = (m_playbackStatus == "Playing");

  // Advance CAVA smoothing (time-delta based).
  tick_cava();
  {
    const auto now = std::chrono::steady_clock::now();
    const double dt = m_lastTickTime.time_since_epoch().count() == 0
        ? 16.0
        : std::chrono::duration<double, std::milli>(now - m_lastTickTime).count();
    m_lastTickTime = now;
    tick(dt);
    if (!playing) m_idlePhase += dt * 0.001;
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
    cairo_pattern_t* grad = cairo_pattern_create_linear(kArtX, kArtY, kArtX + kArtS, kArtY + kArtS);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, 0.55, 0.25, 0.65, 1.0);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, 0.25, 0.35, 0.80, 1.0);
    rounded_rect(cr, kArtX, kArtY, kArtS, kArtS, kArtR);
    cairo_set_source(cr, grad);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);
    eh::shell::draw_material_glyph(cr, kArtX + kArtS * 0.5, kArtY + kArtS * 0.5, 56.0, "music_note",
                                    1.0, 1.0, 1.0, 0.70);
  }
  rounded_rect(cr, kArtX, kArtY, kArtS, kArtS, kArtR);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Title + artist (scroll when overflowing).
  m_marqueeActive = false;
  const double tsec =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  auto draw_scrolled_line = [&](double y, const std::string& text, const char* fd,
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
    if (static_cast<double>(nat_w) > kTextW + 0.5) {
      m_marqueeActive = true;
      const double range = static_cast<double>(nat_w) - kTextW;
      const double off = marquee_triangle_offset(range, tsec + phase, kMarqueePeriodSec);
      cairo_save(cr);
      cairo_rectangle(cr, kTextX, y - 2.0, kTextW, static_cast<double>(line_h) + 4.0);
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
  draw_scrolled_line(kTitleY, m_title, "Inter Bold 17",
                     mc.textR, mc.textG, mc.textB, 0.95, 0.0);
  if (!m_artist.empty())
    draw_scrolled_line(kArtistY, m_artist, "Inter 14",
                       mc.textR, mc.textG, mc.textB, 0.62, 2.7);

  // CAVA wave strip (mirrored accent line + faint fill; idle sine drift).
  {
    struct Pt { double x, y; };
    Pt pts[28]{};
    const double midY = kWaveMidY;
    if (m_cavaActive && playing) {
      for (int i = 0; i < 28; ++i) {
        const double level = m_waveSmoothed[static_cast<size_t>(i)];
        pts[i].x = kWaveX0 + kWaveW * static_cast<double>(i) / 27.0;
        pts[i].y = midY - kWaveHalfH * std::clamp(level, 0.0, 1.0);
      }
    } else {
      for (int i = 0; i < 28; ++i) {
        const double x01 = static_cast<double>(i) / 27.0;
        pts[i].x = kWaveX0 + kWaveW * x01;
        pts[i].y = midY + std::sin(m_idlePhase * 2.0 + x01 * 6.2831) * 1.5;
      }
    }
    constexpr double kTension = 0.5;
    const double waveA = (m_cavaActive && playing) ? 0.95 : 0.25;
    // Faint fill under the curve.
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_move_to(cr, pts[0].x, midY + kWaveHalfH);
    cairo_line_to(cr, pts[0].x, pts[0].y);
    for (int i = 0; i < 27; ++i) {
      const Pt& p0 = (i > 0) ? pts[i - 1] : pts[0];
      const Pt& p1 = pts[i];
      const Pt& p2 = pts[i + 1];
      const Pt& p3 = (i + 2 < 28) ? pts[i + 2] : pts[27];
      cairo_curve_to(cr,
        p1.x + (p2.x - p0.x) * kTension / 3.0, p1.y + (p2.y - p0.y) * kTension / 3.0,
        p2.x - (p3.x - p1.x) * kTension / 3.0, p2.y - (p3.y - p1.y) * kTension / 3.0,
        p2.x, p2.y);
    }
    cairo_line_to(cr, pts[27].x, midY + kWaveHalfH);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.10 * waveA / 0.95);
    cairo_fill(cr);
    cairo_restore(cr);
    // Glow passes on the crest.
    for (int pass = 1; pass >= 0; --pass) {
      cairo_save(cr);
      cairo_new_path(cr);
      cairo_move_to(cr, pts[0].x, pts[0].y);
      for (int i = 0; i < 27; ++i) {
        const Pt& p0 = (i > 0) ? pts[i - 1] : pts[0];
        const Pt& p1 = pts[i];
        const Pt& p2 = pts[i + 1];
        const Pt& p3 = (i + 2 < 28) ? pts[i + 2] : pts[27];
        cairo_curve_to(cr,
          p1.x + (p2.x - p0.x) * kTension / 3.0, p1.y + (p2.y - p0.y) * kTension / 3.0,
          p2.x - (p3.x - p1.x) * kTension / 3.0, p2.y - (p3.y - p1.y) * kTension / 3.0,
          p2.x, p2.y);
      }
      if (pass == 1) {
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.18 * waveA);
        cairo_set_line_width(cr, 5.0);
      } else {
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, waveA);
        cairo_set_line_width(cr, 2.0);
      }
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
      cairo_stroke(cr);
      cairo_restore(cr);
    }
  }

  // Seek bar.
  double pct = 0.0;
  if (snap.duration_us > 0)
    pct = std::clamp(static_cast<double>(snap.position_us) /
                     static_cast<double>(snap.duration_us), 0.0, 1.0);
  const double fillW = kSeekW * pct;
  const double thumbX = kSeekX0 + fillW;
  const bool seekHover = (m_hoverPart == HoverSeekbar);
  fill_rounded_rect(cr, kSeekX0, kBarY, kSeekW, kBarH, kBarH / 2.0, 1.0, 1.0, 1.0, 0.22);
  if (fillW > 0.5)
    fill_rounded_rect(cr, kSeekX0, kBarY, fillW, kBarH, kBarH / 2.0, 1.0, 1.0, 1.0,
                      seekHover ? 1.0 : 0.95);
  cairo_new_path(cr);
  cairo_arc(cr, thumbX, kBarY + kBarH / 2.0, kKnobR, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, seekHover ? 1.0 : 0.95);
  cairo_fill(cr);

  // Timestamps: elapsed left, remaining right.
  {
    char startBuf[24] = "0:00", remainBuf[32] = "-0:00";
    format_time_mmss(startBuf, sizeof(startBuf), snap.position_us, false);
    int64_t remain_us = 0;
    if (snap.duration_us > 0)
      remain_us = std::max<int64_t>(0, snap.duration_us - snap.position_us);
    char tmp[24] = "0:00";
    format_time_mmss(tmp, sizeof(tmp), remain_us, false);
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
  const bool shuffleOn = m_shuffle;
  const bool loopOn = (m_loopMode == "Track" || m_loopMode == "Playlist");
  eh::shell::draw_material_glyph(cr, kShuffleCx, kCtrlCy, 24.0, "shuffle",
                                  1.0, 1.0, 1.0, shuffleOn ? 1.0 : 0.45);
  eh::shell::draw_material_glyph(cr, kPrevCx, kCtrlCy, 28.0, "skip_previous",
                                  1.0, 1.0, 1.0, m_canPrev ? 1.0 : 0.30);
  // Play button: frosted glass disc.
  {
    m3::Box disc;
    disc.setColor(static_cast<float>(mc.dockFillR), static_cast<float>(mc.dockFillG),
                  static_cast<float>(mc.dockFillB), 0.55f);
    disc.setRadius(22.0f);
    disc.setGeometry(static_cast<float>(kPlayCx - 22.0), static_cast<float>(kCtrlCy - 22.0),
                     44.0f, 44.0f);
    disc.setGlassy(true);
    disc.paint(cr);
    rounded_rect(cr, kPlayCx - 22.0, kCtrlCy - 22.0, 44.0, 44.0, 22.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.20);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }
  eh::shell::draw_material_glyph(cr, kPlayCx, kCtrlCy, 34.0,
                                  playing ? "pause" : "play_arrow",
                                  1.0, 1.0, 1.0,
                                  (m_canPlay || m_canPause || playing) ? 1.0 : 0.30);
  eh::shell::draw_material_glyph(cr, kNextCx, kCtrlCy, 28.0, "skip_next",
                                  1.0, 1.0, 1.0, m_canNext ? 1.0 : 0.30);
  eh::shell::draw_material_glyph(cr, kRepeatCx, kCtrlCy, 24.0,
                                  m_loopMode == "Track" ? "repeat_one" : "repeat",
                                  1.0, 1.0, 1.0, loopOn ? 1.0 : 0.45);

  cairo_restore(cr);
}

} // namespace eh::shell::desktop
