#include "desktop_shell/desktop/widgets/fetch/desktop_widget_fetch.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/os_logo/os_logo.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"
#include "desktop_shell/widgets/shared/measure_scratch.hpp"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace eh::shell::desktop {

using eh::shell::shared::rounded_rect;

namespace {

// Default fastfetch structure minus Break/Colors (terminal-only decorations
// that carry no meaning on a desktop card).
constexpr const char* kStructure =
    "Title:Separator:OS:Host:Kernel:Uptime:Packages:Shell:Display:DE:WM:"
    "WMTheme:Theme:Icons:Font:Cursor:Terminal:TerminalFont:CPU:GPU:Memory:"
    "Swap:Disk:LocalIp:Battery:PowerAdapter:Locale";

// Layout constants — kept in one place so measure() and paint() agree.
constexpr double kPadL = 16.0;
constexpr double kPadR = 16.0;
constexpr double kPadT = 16.0;
constexpr double kPadB = 16.0;
constexpr double kRowH = 20.0;
constexpr double kKeyValGap = 14.0;
constexpr double kLogoBoxMin = 108.0; // small cards keep the old footprint
constexpr double kLogoBoxMax = 256.0; // large cards get a fastfetch-scale logo
constexpr double kLogoGap = 20.0;  // gutter between logo and rows
constexpr double kColGap = 12.0;   // header -> rows
constexpr double kRadius = 15.0;

// Content-driven sizing. The card grows to fit instead of truncating; these
// are only belt-and-braces caps so a pathological value can't make a
// screen-wide card.
constexpr double kKeyColMax = 190.0;
constexpr double kValColMax = 880.0;
constexpr double kWidthMax = 1440.0;

void measure_text(cairo_t* cr, const char* fontDesc, const char* str, int* outW, int* outH) {
  PangoLayout* lay = pango_cairo_create_layout(cr);
  PangoFontDescription* pfd = pango_font_description_from_string(fontDesc);
  pango_layout_set_font_description(lay, pfd);
  pango_font_description_free(pfd);
  pango_layout_set_text(lay, str, -1);
  int tw = 0, th = 0;
  pango_layout_get_pixel_size(lay, &tw, &th);
  if (outW) *outW = tw;
  if (outH) *outH = th;
  g_object_unref(lay);
}

void draw_text_with_shadow(cairo_t* cr, const char* fontDesc, const char* str, double px, double py,
                           double r, double g, double b, double a) {
  PangoLayout* lay = pango_cairo_create_layout(cr);
  PangoFontDescription* pfd = pango_font_description_from_string(fontDesc);
  pango_layout_set_font_description(lay, pfd);
  pango_font_description_free(pfd);
  pango_layout_set_text(lay, str, -1);

  cairo_save(cr);
  cairo_move_to(cr, px + 1.0, py + 1.0);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.32);
  pango_cairo_show_layout(cr, lay);
  cairo_restore(cr);

  cairo_move_to(cr, px, py);
  cairo_set_source_rgba(cr, r, g, b, a);
  pango_cairo_show_layout(cr, lay);
  g_object_unref(lay);
}

// Truncate with "…" to fit maxW (UTF-8 safe). Only ever a fallback — the
// width is measured from content, so this shouldn't normally trigger.
std::string trunc_to_fit(cairo_t* cr, const char* fontDesc, const std::string& str, double maxW) {
  int tw = 0;
  measure_text(cr, fontDesc, str.c_str(), &tw, nullptr);
  if (static_cast<double>(tw) <= maxW) return str;
  std::string out = str;
  while (out.size() > 4) {
    do {
      out.pop_back();
    } while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80);
    std::string cand = out + "…";
    measure_text(cr, fontDesc, cand.c_str(), &tw, nullptr);
    if (static_cast<double>(tw) <= maxW) return cand;
  }
  return out;
}

std::string trim_eol(std::string line) {
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
  return line;
}

} // namespace

/* ---- Data ---- */

FetchSnapshot DesktopFetchWidget::read_fetch() {
  FetchSnapshot s;

  const std::string cmd =
      std::string("fastfetch -l none --pipe -s \"") + kStructure + "\"";

  FILE* p = popen(cmd.c_str(), "r");
  if (!p) {
    s.title = "fastfetch";
    s.rows.emplace_back("ERROR", "fastfetch not found");
    return s;
  }

  char buf[2048];
  bool sawTitle = false;
  while (std::fgets(buf, sizeof(buf), p)) {
    std::string line = trim_eol(buf);
    if (line.empty()) continue;
    // The Separator module emits a run of dashes; skip it.
    if (line.find_first_not_of('-') == std::string::npos) continue;

    // Split on the first ": " — values may legitimately contain colons.
    const auto pos = line.find(": ");
    if (pos == std::string::npos) {
      if (!sawTitle) {
        s.title = line;
        sawTitle = true;
      }
      continue;
    }
    sawTitle = true;
    s.rows.emplace_back(line.substr(0, pos), line.substr(pos + 2));
  }
  pclose(p);

  if (s.title.empty()) s.title = "fastfetch";
  s.ok = !s.rows.empty();
  if (!s.ok) s.rows.emplace_back("ERROR", "fastfetch returned no output");
  return s;
}

/* ---- Sizing ---- */

void DesktopFetchWidget::relayout(cairo_t* cr) {
  if (!cr) cr = eh::widgets::get_measure_cr();

  FetchSnapshot snap;
  {
    std::lock_guard lk(m_dataMutex);
    snap = m_snapshot;
  }

  // Key column is driven by the longest key; value column by the longest
  // value. Both capped only as a safety net. A few px of slack absorbs
  // sub-pixel rounding between measure and paint so the "…" fallback stays
  // a last resort for pathological values rather than a common case.
  constexpr double kMeasureSlack = 6.0;
  double keyW = 0.0, valW = 0.0;
  for (const auto& [key, value] : snap.rows) {
    int w = 0;
    measure_text(cr, "Inter 10", key.c_str(), &w, nullptr);
    keyW = std::max(keyW, static_cast<double>(w));
    measure_text(cr, "Inter 11", value.c_str(), &w, nullptr);
    valW = std::max(valW, static_cast<double>(w));
  }
  m_keyCol = std::clamp(keyW + kMeasureSlack, 74.0, kKeyColMax);
  m_valMax = std::clamp(valW + kMeasureSlack, 200.0, kValColMax);

  int titleH = 0;
  measure_text(cr, "Inter Bold 18", snap.title.c_str(), nullptr, &titleH);

  // Right-hand column: title over rows.
  const double colW = m_keyCol + kKeyValGap + m_valMax;
  const double rowsTop = kPadT + static_cast<double>(titleH) + kColGap;
  const double rowsH = kRowH * static_cast<double>(snap.rows.size());

  const double contentH = rowsTop + rowsH + kPadB;
  // Logo scales with the card like fastfetch's full-height ASCII logo, so a
  // 25-row fetch card gets a large logo instead of a 108px stamp.
  m_logoBox = std::clamp(contentH - kPadT - kPadB, kLogoBoxMin, kLogoBoxMax);
  // Logo column must at least fit its square.
  m_height = static_cast<int>(std::ceil(std::max(contentH, m_logoBox + kPadT + kPadB)));

  const double contentW = kPadL + m_logoBox + kLogoGap + colW + kPadR;
  m_width = static_cast<int>(std::ceil(std::min(contentW, kWidthMax)));
  m_layoutDirty = false;
}

/* ---- Widget ---- */

DesktopFetchWidget::DesktopFetchWidget(std::string widgetId)
    : m_widgetId(std::move(widgetId)) {
  m_logo = eh_os_logo::load_distro_logo_cairo_surface();
  {
    std::lock_guard lk(m_dataMutex);
    m_snapshot = read_fetch();
  }
  m_lastRead = std::chrono::steady_clock::now();
  m_haveRead = true;
  relayout();
}

DesktopFetchWidget::~DesktopFetchWidget() {
  if (m_logo) cairo_surface_destroy(m_logo);
  // Don't leave a detached worker running past destruction.
  if (m_readFuture.valid()) m_readFuture.wait();
}

void DesktopFetchWidget::create() {
  // Reap the previous read if it finished; otherwise skip this tick so we
  // never stack concurrent fastfetch processes.
  if (m_readFuture.valid()) {
    if (m_readFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      m_readFuture.get();
      // Snapshot changed — paint() will relayout with the painting cr.
      m_layoutDirty = true;
    } else {
      return;
    }
  }

  const auto now = std::chrono::steady_clock::now();
  if (m_haveRead &&
      std::chrono::duration_cast<std::chrono::seconds>(now - m_lastRead).count() < kRefreshSeconds) {
    return;
  }
  m_lastRead = now;
  m_haveRead = true;

  m_readFuture = std::async(std::launch::async, [this]() {
    FetchSnapshot next = read_fetch();
    std::lock_guard lk(m_dataMutex);
    m_snapshot = std::move(next);
    // Relayout is deferred to paint() so it measures with the painting cr.
    // (Measuring here on the scratch context can disagree with scaled
    // outputs — see relayout().)
    m_layoutDirty = true;
  });
}

/* ---- Paint ---- */

void DesktopFetchWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
  // Re-measure with the painting cr when data changed. The host clips to the
  // previous intrinsic size for this frame and picks up the corrected size on
  // the next tick; from then on measure and paint agree exactly.
  if (m_layoutDirty) relayout(cr);

  FetchSnapshot snap;
  {
    std::lock_guard lk(m_dataMutex);
    snap = m_snapshot;
  }

  const auto mc = eh::config::derived_chrome_colors(sc.appearance);
  const double backdropAlpha = static_cast<double>(sc.appearance.overlayOpacityWidgetCard);
  const double txR = mc.textR, txG = mc.textG, txB = mc.textB;

  const double W = static_cast<double>(m_width);
  const double H = static_cast<double>(m_height);

  // --- Card ---
  cairo_save(cr);
  eh::shell::shared::paint_glass_card(cr, 0.0, 0.0, W, H, kRadius, mc, backdropAlpha);

  // --- Distro logo, left column, vertically centered ---
  // The box grows with the card (see relayout) so a full fetch readout gets
  // a fastfetch-scale logo instead of a small stamp.
  const double logoBox = m_logoBox;
  const double logoTop = (H - logoBox) * 0.5;
  if (m_logo) {
    // Backplate keeps light logos readable on a dark card.
    cairo_save(cr);
    rounded_rect(cr, kPadL, logoTop, logoBox, logoBox, logoBox * 0.28);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
    cairo_fill(cr);
    cairo_restore(cr);

    eh_os_logo::paint_logo_surface_in_square(cr, m_logo, kPadL, logoTop, logoBox,
                                             false, 0.0, 0.0, 0.0);
  }

  // --- Right column geometry ---
  const double colX = kPadL + logoBox + kLogoGap;
  const double valX = colX + m_keyCol + kKeyValGap;
  const double valMaxW = std::min(m_valMax, W - kPadR - valX);

  int titleH = 0;
  measure_text(cr, "Inter Bold 18", snap.title.c_str(), nullptr, &titleH);
  const double rowsTop = kPadT + static_cast<double>(titleH) + kColGap;

  // Header hairline sits below the title, above the rows.
  const double ruleY = kPadT + static_cast<double>(titleH) + 5.0;
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, colX, ruleY + 0.5);
  cairo_line_to(cr, W - kPadR, ruleY + 0.5);
  cairo_stroke(cr);

  draw_text_with_shadow(cr, "Inter Bold 18", snap.title.c_str(), colX, kPadT,
                        txR, txG, txB, 0.95);

  // --- Rows ---
  for (size_t i = 0; i < snap.rows.size(); ++i) {
    const auto& [key, value] = snap.rows[i];
    const double rowTop = rowsTop + static_cast<double>(i) * kRowH;

    const std::string k = trunc_to_fit(cr, "Inter 10", key, m_keyCol);
    const std::string v = trunc_to_fit(cr, "Inter 11", value, valMaxW);

    draw_text_with_shadow(cr, "Inter 10", k.c_str(), colX, rowTop + 4.0,
                          txR, txG, txB, 0.50);
    draw_text_with_shadow(cr, "Inter 11", v.c_str(), valX, rowTop + 3.0,
                          txR, txG, txB, 0.92);

    // Faint separator keeps a long list from reading as one wall of text.
    if (i + 1 < snap.rows.size()) {
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, colX, rowTop + kRowH - 0.5);
      cairo_line_to(cr, W - kPadR, rowTop + kRowH - 0.5);
      cairo_stroke(cr);
    }
  }

  cairo_restore(cr);
}

} // namespace eh::shell::desktop
