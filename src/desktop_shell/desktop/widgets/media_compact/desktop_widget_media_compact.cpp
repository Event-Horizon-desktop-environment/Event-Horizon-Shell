#include "desktop_shell/desktop/widgets/media_compact/desktop_widget_media_compact.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "services/mpris/mpris_player.hpp"
#include "m3/core/primitives/box.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

namespace eh::shell::desktop {
namespace {

using eh::shell::shared::rounded_rect;

// Layout.
constexpr double kPadH       = 22.0;
constexpr double kPadTop     = 18.0;

// header
constexpr double kArtSize    = 68.0;
constexpr double kArtRadius  = 16.0;
constexpr double kArtGap     = 18.0;

// progress
constexpr double kProgH      = 5.0;
constexpr double kProgRad    = 2.5;
constexpr double kProgTopPad = 8.0;

// time labels
constexpr double kTimeSize   = 12.2;
constexpr double kTimeMargin = 7.0;

// controls
constexpr double kPlayD      = 58.0;
constexpr double kPlayR      = kPlayD * 0.5;
constexpr double kNavD       = 44.0;
constexpr double kNavR       = kNavD * 0.5;
constexpr double kCtrlGap    = 24.0;
constexpr double kCtrlBotPad = 22.0;

// Color helpers.
constexpr double kWhiteR = 1.0, kWhiteG = 1.0, kWhiteB = 1.0;
constexpr double kBlackR = 0.0, kBlackG = 0.0, kBlackB = 0.0;

// Helpers.
void format_time_mmss(char* buf, size_t sz, int64_t us, bool neg) {
  if (us < 0) us = 0;
  const auto total = static_cast<uint64_t>(us) / 1'000'000ULL;
  const auto mins  = total / 60ULL;
  const auto secs  = total % 60ULL;
  if (neg)
    std::snprintf(buf, sz, "-%llu:%02llu",
                  static_cast<unsigned long long>(mins),
                  static_cast<unsigned long long>(secs));
  else
    std::snprintf(buf, sz, "%llu:%02llu",
                  static_cast<unsigned long long>(mins),
                  static_cast<unsigned long long>(secs));
}

PangoLayout* make_layout(cairo_t* cr, const char* desc) {
  PangoLayout* l = pango_cairo_create_layout(cr);
  PangoFontDescription* d = pango_font_description_from_string(desc);
  pango_layout_set_font_description(l, d);
  pango_font_description_free(d);
  pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
  pango_layout_set_alignment(l, PANGO_ALIGN_LEFT);
  return l;
}

void fill_rounded(cairo_t* cr, double x, double y, double w, double h, double r,
                  double rr, double gg, double bb, double aa) {
  rounded_rect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, rr, gg, bb, aa);
  cairo_fill(cr);
}

void stroke_rounded(cairo_t* cr, double x, double y, double w, double h, double r,
                    double lw, double rr, double gg, double bb, double aa) {
  rounded_rect(cr, x, y, w, h, r);
  cairo_set_line_width(cr, lw);
  cairo_set_source_rgba(cr, rr, gg, bb, aa);
  cairo_stroke(cr);
}

} // anonymous namespace

void DesktopMediaCompactWidget::ensure_wave_arrays() {
  const size_t n = static_cast<size_t>(m_cfg.bars) * 2;
  if (m_waveSmoothed.size() != n) {
    m_waveSmoothed.assign(n, 0.0);
    m_waveTarget.assign(n, 0.0);
    m_peak.assign(n, 0.0);
  }
}

void DesktopMediaCompactWidget::recompute_width() {
  constexpr double kMinW        = 240.0;
  constexpr double kMaxW        = 800.0;
  constexpr double kExtraMargin = 16.0;
  const double ctrlSpan = kNavR * 2.0 + kCtrlGap * 2.0 + kPlayD;
  const double minForControls = kPadH * 2.0 + ctrlSpan;

  if (!m_active) {
    m_desiredWidth = static_cast<int>(kMinW);
    return;
  }

  auto* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  auto* tmp  = cairo_create(surf);

  PangoLayout* lt = make_layout(tmp, "Inter SemiBold 15.5");
  pango_layout_set_text(lt, m_title.c_str(), -1);
  int tw = 0;
  pango_layout_get_pixel_size(lt, &tw, nullptr);
  g_object_unref(lt);

  int aw = 0;
  if (!m_artist.empty()) {
    PangoLayout* la = make_layout(tmp, "Inter SemiBold 13.5");
    pango_layout_set_text(la, m_artist.c_str(), -1);
    pango_layout_get_pixel_size(la, &aw, nullptr);
    g_object_unref(la);
  }

  cairo_destroy(tmp);
  cairo_surface_destroy(surf);

  const double textW = static_cast<double>(std::max(tw, aw));
  const double needed = kPadH + kArtSize + kArtGap + textW + kExtraMargin + kPadH;
  m_desiredWidth = static_cast<int>(std::clamp(needed, minForControls, kMaxW));
}

DesktopMediaCompactWidget::DesktopMediaCompactWidget(eh::mpris::DockMpris* mpris)
    : m_mpris(mpris)
    , m_cfg(CavaConfig::load_or_default()) {
  m_cava = std::make_unique<CavaReader>(m_cfg);
  m_cava->start();
  ensure_wave_arrays();
}

DesktopMediaCompactWidget::~DesktopMediaCompactWidget() {
  if (m_cava) m_cava->stop();
}

void DesktopMediaCompactWidget::create() {
  // Realtime config reload check (runs every ~1s via timer)
  {
    auto ch = m_cfg.check_and_reload();
    if (ch == ConfigChange::CavaChanged) {
      if (m_cava) m_cava->reload(m_cfg);
      ensure_wave_arrays();
    } else if (ch != ConfigChange::None) {
      ensure_wave_arrays();
    }
  }

  if (!m_mpris) {
    m_desiredWidth = 240;
    return;
  }
  m_mpris->poll_refresh();
  const auto snap = m_mpris->snapshot();

  const bool wasActive = m_active;
  const std::string oldTitle = m_title;
  const std::string oldArtist = m_artist;

  m_active = snap.active && (!snap.title.empty() || !snap.artist.empty());
  if (m_active) {
    m_title           = snap.title.empty() ? "Unknown Track" : snap.title;
    m_artist          = snap.artist;
    m_playbackStatus  = snap.playback_status;
    m_canPrev         = snap.can_go_previous;
    m_canNext         = snap.can_go_next;
    m_canPlay         = snap.can_play;
    m_canPause        = snap.can_pause;
    m_positionUs      = snap.position_us;
    m_durationUs      = snap.duration_us;
    m_isStream        = snap.track_url_raw.find("twitch.tv") != std::string::npos;
  } else {
    m_title.clear();
    m_artist.clear();
    m_playbackStatus.clear();
    m_canPrev = m_canNext = m_canPlay = m_canPause = false;
    m_positionUs = m_durationUs = 0;
    m_isStream = false;
  }

  if (m_active != wasActive || m_title != oldTitle || m_artist != oldArtist)
    recompute_width();

  ensure_wave_arrays();
}

int DesktopMediaCompactWidget::hit_test_btn(double x, double y) const {
  if (!m_active || !m_mpris) return -1;

  const double ctrlY = intrinsicHeight() - kCtrlBotPad - kPlayR;

  // Progress bar hit test  (return 3 as a special zone; no bar for live streams)
  if (m_durationUs > 0 && !m_isStream) {
    const double progY = kPadTop + kArtSize + kProgTopPad;
    if (y >= progY - 4 && y <= progY + kProgH + 4 &&
        x >= kPadH && x <= intrinsicWidth() - kPadH)
      return 3;
  }

  // Skip-previous
  {
    const double prevCx = intrinsicWidth() * 0.5 - kPlayR - kCtrlGap - kNavR;
    if (x >= prevCx - kNavR && x <= prevCx + kNavR &&
        y >= ctrlY - kNavR && y <= ctrlY + kNavR)
      return 0;
  }

  // Play/pause
  {
    const double playCx = intrinsicWidth() * 0.5;
    if (x >= playCx - kPlayR && x <= playCx + kPlayR &&
        y >= ctrlY - kPlayR && y <= ctrlY + kPlayR)
      return 1;
  }

  // Skip-next
  {
    const double nextCx = intrinsicWidth() * 0.5 + kPlayR + kCtrlGap + kNavR;
    if (x >= nextCx - kNavR && x <= nextCx + kNavR &&
        y >= ctrlY - kNavR && y <= ctrlY + kNavR)
      return 2;
  }

  return -1;
}

bool DesktopMediaCompactWidget::on_click(double x, double y) {
  if (!m_mpris) return false;
  const int zone = hit_test_btn(x, y);

  if (zone == 3) {
    // Seek on progress bar
    const auto snap = m_mpris->snapshot();
    if (snap.duration_us > 0) {
      const double frac = std::clamp((x - kPadH) / (intrinsicWidth() - kPadH * 2), 0.0, 1.0);
      m_mpris->set_position(static_cast<int64_t>(static_cast<double>(snap.duration_us) * frac));
    }
    return true;
  }

  if (zone < 0) return false;
  switch (zone) {
    case 0: if (m_canPrev) m_mpris->previous(); return true;
    case 1: m_mpris->play_pause(); return true;
    case 2: if (m_canNext) m_mpris->next(); return true;
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

void DesktopMediaCompactWidget::tick_cava() {
  if (m_cava && m_cava->available()) {
    m_cava->read_values(m_cavaValues);
    m_cavaActive = !m_cavaValues.empty();
  } else {
    m_cavaActive = false;
    return;
  }
  if (!m_cavaActive) return;

  const int nCava = std::min(static_cast<int>(m_cavaValues.size()), m_cfg.bars);

  // Interpolate cava values -> half-wave points, then mirror
  const int halfWave = m_cfg.bars;
  const int waveCount = halfWave * 2;

  std::vector<double> half(static_cast<size_t>(halfWave));
  for (int i = 0; i < halfWave; ++i) {
    const double pos = (static_cast<double>(i) + 0.5)
                     / static_cast<double>(halfWave)
                     * static_cast<double>(nCava);
    const int idx = std::min(static_cast<int>(pos), nCava - 1);
    const double frac = pos - static_cast<double>(idx);
    const double v0 = static_cast<double>(m_cavaValues[static_cast<size_t>(idx)]) / 100.0;
    const double v1 = (idx + 1 < nCava)
        ? static_cast<double>(m_cavaValues[static_cast<size_t>(idx + 1)]) / 100.0
        : 0.0;
    half[static_cast<size_t>(i)] = std::sqrt(std::clamp(v0 + (v1 - v0) * frac, 0.0, 1.0));
  }

  // Mirror: left side = reversed half, right side = forward half
  m_waveTarget.resize(static_cast<size_t>(waveCount));
  for (int i = 0; i < halfWave; ++i) {
    m_waveTarget[static_cast<size_t>(i)] = half[static_cast<size_t>(halfWave - 1 - i)];
    m_waveTarget[static_cast<size_t>(halfWave + i)] = half[static_cast<size_t>(i)];
  }
}

// Live cava visualizer needs full-rate animation; the idle breathe is a slow
// 0.4 Hz sine so a few Hz keeps it visually smooth. 0 lets the standalone
// loop disarm its animation timer entirely when nothing else animates.
uint32_t DesktopMediaCompactWidget::animIntervalMs() const {
  const bool playing = (m_playbackStatus == "Playing");
  if (m_cavaActive && playing) return 33;
  if (m_active && m_cfg.idle_breathe_amplitude > 0.0) return 250;
  return 0;
}

void DesktopMediaCompactWidget::tick(double deltaMs) {  const double dt = std::max(0.0, deltaMs);
  const bool playing = (m_playbackStatus == "Playing");
  const int waveCount = m_cfg.bars * 2;

  if (m_cavaActive && playing) {
    const double attackTau = m_cfg.attack_tau_ms;
    const double decayTau = m_cfg.decay_tau_ms;

    for (int i = 0; i < waveCount; ++i) {
      const double target = m_waveTarget[static_cast<size_t>(i)];
      double& display = m_waveSmoothed[static_cast<size_t>(i)];
      const double delta = target - display;

      if (std::fabs(delta) < 1.0 / 512.0) {
        if (display != target) display = target;
      } else if (delta > 0.0) {
        display += delta * (1.0 - std::exp(-dt / attackTau));
      } else {
        display += delta * (1.0 - std::exp(-dt / decayTau));
      }

      double& peak = m_peak[static_cast<size_t>(i)];
      if (display > peak) peak = display;
    }

    for (int i = 0; i < waveCount; ++i) {
      double& peak = m_peak[static_cast<size_t>(i)];
      peak -= 0.5 * dt / 1000.0;
      if (peak < m_waveSmoothed[static_cast<size_t>(i)])
        peak = m_waveSmoothed[static_cast<size_t>(i)];
    }
  } else if (m_active) {
    m_idleTimer += dt * 0.001;
    const double breathe = m_cfg.idle_breathe_amplitude
        * std::sin(m_idleTimer * 2.0 * M_PI * 0.4);
    for (int i = 0; i < waveCount; ++i) {
      const double pos = static_cast<double>(i)
                       / static_cast<double>(waveCount - 1);
      const double shape = std::sin(pos * M_PI);
      const double v = std::max(0.0, breathe * shape);
      m_waveSmoothed[static_cast<size_t>(i)] = v;
      m_peak[static_cast<size_t>(i)] = v;
    }
  } else {
    m_waveSmoothed.assign(static_cast<size_t>(waveCount), 0.0);
    m_peak.assign(static_cast<size_t>(waveCount), 0.0);
  }
}

void DesktopMediaCompactWidget::paint(cairo_t* cr,
                                       const eh::config::ShellConfig& sc) {
  if (!m_mpris) return;

  m_mpris->poll_position();
  const auto snap = m_mpris->snapshot();

  // Smooth position projection — same logic as dock media widget
  int64_t displayPosUs = snap.position_us;
  if (snap.active && snap.duration_us > 0 && !m_isStream) {
    const auto now = std::chrono::steady_clock::now();
    int64_t new_snap = snap.position_us;

    // Discard spurious 0-positions when we're clearly past the start
    if (new_snap == 0 && m_smoothPosUs > 2000000)
      new_snap = m_smoothLastSnap;

    bool seek = false;
    if (m_smoothLastSnap > 0 && snap.track_id == m_smoothTrackId) {
      int64_t delta = new_snap - m_smoothLastSnap;
      seek = (delta > 2000000 || delta < -2000000);
    }

    if (snap.track_id != m_smoothTrackId || seek) {
      m_smoothTrackId = snap.track_id;
      m_smoothPosUs = new_snap;
      m_smoothPosAt = now;
    } else {
      auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - m_smoothPosAt).count();
      m_smoothPosUs = m_smoothPosUs + elapsed;
      m_smoothPosAt = now;
    }

    m_smoothLastSnap = new_snap;
    displayPosUs = std::min(m_smoothPosUs, snap.duration_us);
  }

  const double W  = static_cast<double>(intrinsicWidth());
  const double H  = static_cast<double>(intrinsicHeight());
  const double cx = W * 0.5;

  const bool realActive = snap.active && (!snap.title.empty() || !snap.artist.empty());
  // Cava smoothing.
  if (realActive && snap.playback_status == "Playing")
    tick_cava();
  {
    const auto now = std::chrono::steady_clock::now();
    const double dt = m_lastTickTime.time_since_epoch().count() == 0
        ? 16.0
        : std::chrono::duration<double, std::milli>(now - m_lastTickTime).count();
    m_lastTickTime = now;
    tick(dt);
  }

  // Derived colors.
  const auto mc = eh::config::derived_chrome_colors(sc.appearance);

  // Glass background base — darken dockFill for the glass card look.
  // Tied to the "Widget cards" overlay slider in appearance settings;
  // only the backdrop becomes transparent, text/controls/art stay opaque.
  const double bgR = mc.dockFillR * 0.35;
  const double bgG = mc.dockFillG * 0.35;
  const double bgB = mc.dockFillB * 0.35;
  const double bgA = 0.78 * static_cast<double>(sc.appearance.overlayOpacityWidgetCard);

  // Background card.
  cairo_save(cr);

  const double rad = 22.0;
  m3::Box bgBox;
  bgBox.setColor(static_cast<float>(bgR), static_cast<float>(bgG), static_cast<float>(bgB),
                 static_cast<float>(bgA));
  bgBox.setRadius(static_cast<float>(rad));
  bgBox.setGeometry(0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H));
  bgBox.setGlassy(true);
  bgBox.paint(cr);
  stroke_rounded(cr, 0, 0, W, H, rad, 1.0, kWhiteR, kWhiteG, kWhiteB, 0.12);

  // Clip to card
  rounded_rect(cr, 0, 0, W, H, rad);
  cairo_clip(cr);

  // Header gradient.
  {
    cairo_pattern_t* grad = cairo_pattern_create_linear(0, 0, W, 0);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, mc.accentR, mc.accentG, mc.accentB, 0.12);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, mc.accentR, mc.accentG, mc.accentB, 0.0);
    cairo_rectangle(cr, 0, 0, W, kPadTop + kArtSize + 8);
    cairo_set_source(cr, grad);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);
  }

  // Cava waveform (organic Catmull-Rom spline).
  if (realActive) {
    const double waveBot = H - 6.0;
    const double maxWaveH = H * m_cfg.wave_height;
    constexpr double kTension = 0.5;
    const int waveCount = m_cfg.bars * 2;

    struct WPoint { double x, y; };
    std::vector<WPoint> pts(static_cast<size_t>(waveCount));
    bool hasWave = false;
    for (int i = 0; i < waveCount; ++i) {
      const double level = m_waveSmoothed[static_cast<size_t>(i)];
      if (level > 0.005) hasWave = true;
      pts[static_cast<size_t>(i)].x = W * static_cast<double>(i) / static_cast<double>(waveCount - 1);
      pts[static_cast<size_t>(i)].y = waveBot - maxWaveH * level;
    }

    if (hasWave) {
      // Filled region below the wave (vertical gradient).
      cairo_pattern_t* fg = cairo_pattern_create_linear(0, waveBot, 0, H);
      cairo_pattern_add_color_stop_rgba(fg, 0.0, mc.accentR, mc.accentG, mc.accentB, m_cfg.fill_opacity);
      cairo_pattern_add_color_stop_rgba(fg, 0.4, mc.accentR, mc.accentG, mc.accentB, 0.05);
      cairo_pattern_add_color_stop_rgba(fg, 1.0, mc.accentR, mc.accentG, mc.accentB, 0.0);
      cairo_set_source(cr, fg);

      cairo_new_path(cr);
      cairo_move_to(cr, pts[0].x, H);
      cairo_line_to(cr, pts[0].x, pts[0].y);
      for (int i = 0; i < waveCount - 1; ++i) {
        const WPoint& p0 = (i > 0) ? pts[static_cast<size_t>(i - 1)] : pts[0];
        const WPoint& p1 = pts[static_cast<size_t>(i)];
        const WPoint& p2 = pts[static_cast<size_t>(i + 1)];
        const WPoint& p3 = (i + 2 < waveCount) ? pts[static_cast<size_t>(i + 2)] : pts[static_cast<size_t>(waveCount - 1)];
        cairo_curve_to(cr,
          p1.x + (p2.x - p0.x) * kTension / 3.0,
          p1.y + (p2.y - p0.y) * kTension / 3.0,
          p2.x - (p3.x - p1.x) * kTension / 3.0,
          p2.y - (p3.y - p1.y) * kTension / 3.0,
          p2.x, p2.y);
      }
      cairo_line_to(cr, pts[static_cast<size_t>(waveCount - 1)].x, H);
      cairo_close_path(cr);
      cairo_fill(cr);
      cairo_pattern_destroy(fg);

      // Glow passes on the wave crest.
      for (int g = 3; g >= 0; --g) {
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB,
                              m_cfg.glow_intensity * static_cast<double>(g + 1));
        cairo_set_line_width(cr, static_cast<double>(g * 3 + 2));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

        cairo_new_path(cr);
        cairo_move_to(cr, pts[0].x, pts[0].y);
        for (int i = 0; i < waveCount - 1; ++i) {
          const WPoint& p0 = (i > 0) ? pts[static_cast<size_t>(i - 1)] : pts[0];
          const WPoint& p1 = pts[static_cast<size_t>(i)];
          const WPoint& p2 = pts[static_cast<size_t>(i + 1)];
          const WPoint& p3 = (i + 2 < waveCount) ? pts[static_cast<size_t>(i + 2)] : pts[static_cast<size_t>(waveCount - 1)];
          cairo_curve_to(cr,
            p1.x + (p2.x - p0.x) * kTension / 3.0,
            p1.y + (p2.y - p0.y) * kTension / 3.0,
            p2.x - (p3.x - p1.x) * kTension / 3.0,
            p2.y - (p3.y - p1.y) * kTension / 3.0,
            p2.x, p2.y);
        }
        cairo_stroke(cr);
      }

      // Peak marker dots.
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, m_cfg.peak_opacity);
      for (int i = 0; i < waveCount; ++i) {
        const double peak = m_peak[static_cast<size_t>(i)];
        if (peak > m_waveSmoothed[static_cast<size_t>(i)] + 0.008) {
          const double py = waveBot - maxWaveH * peak;
          cairo_arc(cr, pts[static_cast<size_t>(i)].x, py, 2.5, 0, 2.0 * M_PI);
          cairo_fill(cr);
        }
      }
    }
  }

  // Track info.
  const std::string title_m =
      realActive ? (snap.title.empty() ? std::string("Unknown Track") : snap.title)
                 : std::string("No Media");
  const std::string artist_m = realActive ? snap.artist : std::string();

  const double artY = kPadTop;
  const double artX = kPadH;
  const double infoX = artX + kArtSize + kArtGap;
  const double infoW = std::max(0.0, W - infoX - kPadH);

  // Album art.
  {
    // Glassy frame behind the album art
    constexpr double kArtFramePad = 6.0;
    m3::Box artFrame;
    artFrame.setColor(static_cast<float>(bgR), static_cast<float>(bgG), static_cast<float>(bgB),
                      static_cast<float>(bgA));
    artFrame.setRadius(static_cast<float>(kArtRadius));
    artFrame.setGeometry(static_cast<float>(artX - kArtFramePad),
                         static_cast<float>(artY - kArtFramePad),
                         static_cast<float>(kArtSize + 2.0 * kArtFramePad),
                         static_cast<float>(kArtSize + 2.0 * kArtFramePad));
    artFrame.setGlassy(true);
    artFrame.paint(cr);

    bool drewArt = false;
    if (realActive && snap.art && cairo_surface_status(snap.art.get()) == CAIRO_STATUS_SUCCESS) {
      const int iw = cairo_image_surface_get_width(snap.art.get());
      const int ih = cairo_image_surface_get_height(snap.art.get());
      if (iw > 0 && ih > 0) {
        const double sc = std::max(kArtSize / iw, kArtSize / ih);
        const double dw = iw * sc, dh = ih * sc;
        cairo_save(cr);
        rounded_rect(cr, artX, artY, kArtSize, kArtSize, kArtRadius);
        cairo_clip(cr);
        cairo_translate(cr, artX - (dw - kArtSize) * 0.5, artY - (dh - kArtSize) * 0.5);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, snap.art.get(), 0, 0);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
        cairo_paint(cr);
        cairo_restore(cr);
        drewArt = true;
      }
    }
    if (!drewArt) {
      // Placeholder gradient
      const double gr = 0.9, gg = 0.2, gb = 0.4;
      const double br = 0.5, bg = 0.2, bb = 0.9;
      cairo_pattern_t* grad = cairo_pattern_create_linear(artX, artY, artX + kArtSize, artY + kArtSize);
      cairo_pattern_add_color_stop_rgba(grad, 0.0, gr, gg, gb, 1.0);
      cairo_pattern_add_color_stop_rgba(grad, 1.0, br, bg, bb, 1.0);
      rounded_rect(cr, artX, artY, kArtSize, kArtSize, kArtRadius);
      cairo_set_source(cr, grad);
      cairo_fill(cr);
      cairo_pattern_destroy(grad);

      // Music note glyph
      if (!realActive) {
        cairo_set_source_rgba(cr, kWhiteR, kWhiteG, kWhiteB, 0.3);
      } else {
        cairo_set_source_rgba(cr, kWhiteR, kWhiteG, kWhiteB, 0.7);
      }
      // Draw a music note using the material glyph at a large size
      eh::shell::draw_material_glyph(cr, artX + kArtSize * 0.5, artY + kArtSize * 0.5,
                                      kArtSize * 0.45, "music_note",
                                      kWhiteR, kWhiteG, kWhiteB,
                                      realActive ? 0.7 : 0.3);
    }
    // Subtle border on album art
    stroke_rounded(cr, artX, artY, kArtSize, kArtSize, kArtRadius, 1.0,
                   kWhiteR, kWhiteG, kWhiteB, 0.10);
  }

  // Title / Artist.
  if (realActive) {
    // Title
    {
      PangoLayout* lt = make_layout(cr, "Inter SemiBold 15.5");
      pango_layout_set_text(lt, title_m.c_str(), -1);
      pango_layout_set_width(lt, static_cast<int>(infoW * PANGO_SCALE));
      int tw = 0, th = 0;
      pango_layout_get_pixel_size(lt, &tw, &th);
      // Vertically center title+artist block relative to album art
      const double titleY = artY + (kArtSize - static_cast<double>(th) - 3 - 16) * 0.5;
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.92);
      cairo_move_to(cr, infoX, titleY);
      pango_cairo_show_layout(cr, lt);
      g_object_unref(lt);
    }

    // Artist
    if (!artist_m.empty()) {
      PangoLayout* la = make_layout(cr, "Inter SemiBold 13.5");
      pango_layout_set_text(la, artist_m.c_str(), -1);
      pango_layout_set_width(la, static_cast<int>(infoW * PANGO_SCALE));
      int aw = 0, ah = 0;
      pango_layout_get_pixel_size(la, &aw, &ah);
      // Compute artist y based on title height measurement
      {
        PangoLayout* tmp = make_layout(cr, "Inter SemiBold 15.5");
        pango_layout_set_width(tmp, static_cast<int>(infoW * PANGO_SCALE));
        pango_layout_set_text(tmp, title_m.c_str(), -1);
        int th2 = 0;
        pango_layout_get_pixel_size(tmp, nullptr, &th2);
        const double titleY = artY + (kArtSize - static_cast<double>(th2) - 3 - 16) * 0.5;
        const double artistY = titleY + static_cast<double>(th2) + 3;
        cairo_move_to(cr, infoX, artistY);
        cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.65);
        pango_cairo_show_layout(cr, la);
        g_object_unref(tmp);
      }
      g_object_unref(la);
    }
  } else {
    // Inactive: show "No Media" centered in text area
    PangoLayout* lt = make_layout(cr, "Inter SemiBold 15.5");
    pango_layout_set_text(lt, title_m.c_str(), -1);
    pango_layout_set_width(lt, static_cast<int>(infoW * PANGO_SCALE));
    int th = 0;
    pango_layout_get_pixel_size(lt, nullptr, &th);
    const double titleY = artY + (kArtSize - static_cast<double>(th)) * 0.5;
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.50);
    cairo_move_to(cr, infoX, titleY);
    pango_cairo_show_layout(cr, lt);
    g_object_unref(lt);
  }

  // Progress bar (hidden for live streams with no duration).
  if (realActive && snap.duration_us > 0 && !m_isStream) {
    const double progY = artY + kArtSize + kProgTopPad;
    const double progW = W - kPadH * 2;
    const double progX = kPadH;

    // Track background
    fill_rounded(cr, progX, progY, progW, kProgH, kProgRad,
                 kWhiteR, kWhiteG, kWhiteB, 0.18);

    // Filled portion
    if (snap.duration_us > 0) {
      const double frac = std::clamp(static_cast<double>(displayPosUs) /
                                      static_cast<double>(snap.duration_us), 0.0, 1.0);
      if (frac > 0.005) {
        const double fillW = progW * frac;
        fill_rounded(cr, progX, progY, fillW, kProgH, kProgRad,
                     mc.accentR, mc.accentG, mc.accentB, 1.0);

        // Glow / thumb dot on hover
        if (m_hoverProg) {
          const double thumbD = 14.0;
          const double thumbX = progX + fillW;
          const double thumbY = progY + kProgH * 0.5;
          // Outer glow
          cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.3);
          cairo_arc(cr, thumbX, thumbY, thumbD * 0.5 + 4, 0, 2.0 * M_PI);
          cairo_fill(cr);
          // White core
          cairo_set_source_rgba(cr, kWhiteR, kWhiteG, kWhiteB, 1.0);
          cairo_arc(cr, thumbX, thumbY, thumbD * 0.5 - 1, 0, 2.0 * M_PI);
          cairo_fill(cr);
          // Accent ring
          cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 1.0);
          cairo_set_line_width(cr, 2.0);
          cairo_arc(cr, thumbX, thumbY, thumbD * 0.5 - 1, 0, 2.0 * M_PI);
          cairo_stroke(cr);
        }
      }
    }

    // Time labels
    {
      char curBuf[32], remBuf[32];
      format_time_mmss(curBuf, sizeof(curBuf), displayPosUs, false);
      const int64_t remaining = snap.duration_us - displayPosUs;
      format_time_mmss(remBuf, sizeof(remBuf), remaining > 0 ? remaining : 0, true);

      const double timeY = progY + kProgH + kTimeMargin;

      PangoLayout* lcur = make_layout(cr, "Inter SemiBold 12.2");
      pango_layout_set_text(lcur, curBuf, -1);
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.55);
      cairo_move_to(cr, progX, timeY);
      pango_cairo_show_layout(cr, lcur);
      g_object_unref(lcur);

      PangoLayout* lrem = make_layout(cr, "Inter SemiBold 12.2");
      pango_layout_set_text(lrem, remBuf, -1);
      pango_layout_set_alignment(lrem, PANGO_ALIGN_RIGHT);
      pango_layout_set_width(lrem, static_cast<int>(progW * PANGO_SCALE));
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.55);
      cairo_move_to(cr, progX, timeY);
      pango_cairo_show_layout(cr, lrem);
      g_object_unref(lrem);
    }
  }

  // Controls.
  if (realActive) {
    const double ctrlY = H - kCtrlBotPad - kPlayR;
    const double playCx = cx;
    const double prevCx = cx - kPlayR - kCtrlGap - kNavR;
    const double nextCx = cx + kPlayR + kCtrlGap + kNavR;

    const bool playing = (snap.playback_status == "Playing");

    // Skip-previous.
    {
      const double a = snap.can_go_previous ? 0.85 : 0.35;
      const double bgA3 = (m_hoverBtn == 0) ? 0.22 : 0.18;
      m3::Box prevBox;
      prevBox.setColor(static_cast<float>(kWhiteR), static_cast<float>(kWhiteG),
                       static_cast<float>(kWhiteB), static_cast<float>(bgA3));
      prevBox.setRadius(static_cast<float>(kNavR));
      prevBox.setGeometry(static_cast<float>(prevCx - kNavR), static_cast<float>(ctrlY - kNavR),
                          static_cast<float>(kNavD), static_cast<float>(kNavD));
      prevBox.setGlassy(true);
      prevBox.paint(cr);
      eh::shell::draw_material_glyph(cr, prevCx, ctrlY, 24.0, "skip_previous",
                                      mc.textR, mc.textG, mc.textB, a);
    }

    // Play / Pause.
    {
      // Background circle
      const double bgA2 = (m_hoverBtn == 1) ? 0.22 : 0.18;
      m3::Box playBox;
      playBox.setColor(static_cast<float>(kWhiteR), static_cast<float>(kWhiteG),
                       static_cast<float>(kWhiteB), static_cast<float>(bgA2));
      playBox.setRadius(static_cast<float>(kPlayR));
      playBox.setGeometry(static_cast<float>(playCx - kPlayR), static_cast<float>(ctrlY - kPlayR),
                          static_cast<float>(kPlayD), static_cast<float>(kPlayD));
      playBox.setGlassy(true);
      playBox.paint(cr);

      // Accent tint fill on hover
      if (m_hoverBtn == 1) {
        fill_rounded(cr, playCx - kPlayR, ctrlY - kPlayR,
                     kPlayD, kPlayD, kPlayR,
                     mc.accentR, mc.accentG, mc.accentB, 0.06);
      }

      // Glyph
      const double glyphSize = 26.0;
      if (playing) {
        eh::shell::draw_material_glyph(cr, playCx, ctrlY, glyphSize, "pause",
                                        mc.textR, mc.textG, mc.textB, 0.92);
      } else {
        eh::shell::draw_material_glyph(cr, playCx, ctrlY, glyphSize, "play_arrow",
                                        mc.textR, mc.textG, mc.textB, 0.92);
      }
    }

    // Skip-next.
    {
      const double a = snap.can_go_next ? 0.85 : 0.35;
      const double bgA3 = (m_hoverBtn == 2) ? 0.22 : 0.18;
      m3::Box nextBox;
      nextBox.setColor(static_cast<float>(kWhiteR), static_cast<float>(kWhiteG),
                       static_cast<float>(kWhiteB), static_cast<float>(bgA3));
      nextBox.setRadius(static_cast<float>(kNavR));
      nextBox.setGeometry(static_cast<float>(nextCx - kNavR), static_cast<float>(ctrlY - kNavR),
                          static_cast<float>(kNavD), static_cast<float>(kNavD));
      nextBox.setGlassy(true);
      nextBox.paint(cr);
      eh::shell::draw_material_glyph(cr, nextCx, ctrlY, 24.0, "skip_next",
                                      mc.textR, mc.textG, mc.textB, a);
    }

    // ════════════════════════════════════════════════════════════════════
    // Button glow on hover (subtle ring around the play button)
    if (m_hoverBtn == 1) {
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.30);
      cairo_set_line_width(cr, 2.0);
      cairo_arc(cr, playCx, ctrlY, kPlayR + 2, 0, 2.0 * M_PI);
      cairo_stroke(cr);
    }
  }

  cairo_restore(cr);
}

} // namespace eh::shell::desktop
