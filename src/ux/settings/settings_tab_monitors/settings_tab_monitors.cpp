#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_monitors/monitors_log.hpp"
#include "ux/settings/settings_tab_monitors/settings_tab_monitors.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "m3/controls/containers/button.hpp"
#include "dialog/file_chooser_dialog.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"

#include <chrono>

extern void draw(App& app);

// String arrays.
const char* kMonitorTfLabels[8] = {"0° normal", "90°", "180°", "270°",
                                    "Flip", "Flip 90°", "Flip 180°", "Flip 270°"};
const char* kMonitorCmLabels[kMonitorCmCount] = {"auto", "srgb", "dcip3", "dp3", "adobe",
                                                  "wide", "edid", "hdr", "hdredid"};
const char* kMonitorBitdepthDdLabels[2] = {"Default", "10-bit"};
const char* kMonSupportsHdrDdLabels[3] = {"Auto", "Force on", "Force off"};
const char* kMonSupportsWideDdLabels[3] = {"Auto", "Force on", "Force off"};
const char* kMonSdrEotfDdLabels[3] = {"Follow", "sRGB", "Gamma 2.2"};

// Forward declarations for static helpers.
static void monitors_clamp_selected(App& app);
static MonitorsFormRows monitors_form_rows(const App& app,
                                            const eh::settings_monitors::MonitorRow& row,
                                            const eh::settings_monitors::OutputCaps* caps);
static MonitorsFormGeom monitors_form_layout(const eh::settings_monitors_tab::MonitorsTabLayout& lay,
                                              int contentX, int contentW, const MonitorsFormRows& fr);
static void monitors_form_slider_track_geom_content(int content_y0, int cardX, int cardW,
                                                     int rowIx, int* trX, int* trY, int* trW);
static void monitors_canvas_transform(const App& app,
                                       const eh::settings_monitors_tab::MonitorsTabLayout& lay,
                                       double* min_x, double* min_y, double* sf_out);

// Internal helpers (static).

static int monitors_section_card_height_rows(int n_rows) {
   
  return kMonSecTitlePadTop + kMonSecTitleLineH + kMonSecAfterTitle +
         n_rows * kMonFormRowPitch + kMonSecBottomPad;
}

static int monitors_section_content_y0(int card_y) {
   
  return card_y + kMonSecTitlePadTop + kMonSecTitleLineH + kMonSecAfterTitle;
}

static void monitors_combo_geom_at_content_row(int content_y0, int cardX, int cardW,
                                               int rowIx, int* cx, int* cy, int* cw, int* ch) {
   
  const int y = content_y0 + rowIx * kMonFormRowPitch;
  *cx = cardX + cardW - kCardPad - kMonFormValRailPx - kSettingsComboW;
  *cy = y;
  *cw = kSettingsComboW;
  *ch = kSettingsComboH;
}

static double monitors_parse_scale_ds(const std::string& s) {
   
  char* e = nullptr;
  double v = std::strtod(s.c_str(), &e);
  if (e == s.c_str()) return 1.0;
  return std::clamp(v, 0.25, 8.0);
}

static bool monitors_hdr_panel_visible(const App& app,
                                        const eh::settings_monitors::MonitorRow& row,
                                        const eh::settings_monitors::OutputCaps* caps) {
   
  if (!app.monitorsTab.supports_color_hdr_ui()) return false;
  if (row.disabled) return false;
  if (caps && caps->hdr_hint) return true;
  if (row.cm == "hdr" || row.cm == "hdredid") return true;
  if (row.supports_hdr == "1") return true;
  return false;
}

static bool monitors_wide_eotf_strip_visible(const App& app,
                                              const eh::settings_monitors::MonitorRow& row,
                                              const eh::settings_monitors::OutputCaps* caps) {
   
  if (!app.monitorsTab.supports_color_hdr_ui()) return false;
  if (row.disabled) return false;
  if (monitors_hdr_panel_visible(app, row, caps)) return false;
  std::string cm = row.cm;
  for (char& c : cm) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return cm == "srgb";
}

static int monitors_form_scale_ticks(const eh::settings_monitors::MonitorRow& row) {
   
  double v = monitors_parse_scale_ds(row.scale);
  v = std::clamp(v, 1.0, 2.0);
  return static_cast<int>(std::lround(v * 100.0));
}

[[maybe_unused]] static bool monitors_canvas_screen_rect(const App& app,
                                        const eh::settings_monitors_tab::MonitorsTabLayout& lay,
                                        size_t idx, double* sx, double* sy,
                                        double* sw, double* sh) {
  // OPTIMIZED: single layout_rects_from_outputs + single compute_canvas_fit.
  // The old version called monitors_canvas_transform() internally, which
  // recomputed layout_rects a second time per monitor (2N parses per paint).
  std::vector<eh::settings_monitors_tab::ArrangeRect> rects;
  eh::settings_monitors_tab::layout_rects_from_outputs(app.monitorsTab.outputs,
                                                       app.monitorsTab.caps, &rects);
  if (idx >= rects.size()) return false;
  const auto& r = rects[idx];
  if (idx >= app.monitorsTab.outputs.size()) return false;
  if (app.monitorsTab.outputs[idx].disabled || r.w <= 0) return false;
  double min_x = 0, min_y = 0, span_w = 1, span_h = 1, fit_scale = 1;
  eh::settings_monitors_tab::compute_canvas_fit(rects, lay.canvas_w, lay.canvas_h, 0.1,
                                                &min_x, &min_y, &span_w, &span_h, &fit_scale);
  const double sf = fit_scale * app.monitorsCanvasZoom;
  *sx = static_cast<double>(lay.canvas_x) + app.monitorsCanvasPanX +
        (static_cast<double>(r.x) - min_x) * sf;
  *sy = static_cast<double>(lay.canvas_y) + app.monitorsCanvasPanY +
        (static_cast<double>(r.y) - min_y) * sf;
  *sw = static_cast<double>(r.w) * sf;
  *sh = static_cast<double>(r.h) * sf;
  return *sw > 1 && *sh > 1;
}

// Per-paint canvas cache: layout_rects + fit computed ONCE per paint, then
// reused for every monitor rect. Without this, N monitors cost N
// layout_rects_from_outputs (each parsing scale/transform strings + map
// lookups) plus N compute_canvas_fit. With it: exactly 1 of each.
struct MonPaintCanvasCache {
  std::vector<eh::settings_monitors_tab::ArrangeRect> rects;
  std::vector<const eh::settings_monitors::OutputCaps*> caps_for_idx;
  double min_x = 0;
  double min_y = 0;
  double sf = 1;
};

static MonPaintCanvasCache monitors_paint_canvas_cache_build(
    const App& app, const eh::settings_monitors_tab::MonitorsTabLayout& lay) {
  MonPaintCanvasCache c;
  eh::settings_monitors_tab::layout_rects_from_outputs(app.monitorsTab.outputs,
                                                       app.monitorsTab.caps, &c.rects);
  double span_w = 1, span_h = 1, fit_scale = 1;
  eh::settings_monitors_tab::compute_canvas_fit(c.rects, lay.canvas_w, lay.canvas_h, 0.1,
                                                &c.min_x, &c.min_y, &span_w, &span_h, &fit_scale);
  c.sf = fit_scale * app.monitorsCanvasZoom;
  c.caps_for_idx.resize(app.monitorsTab.outputs.size(), nullptr);
  for (size_t i = 0; i < app.monitorsTab.outputs.size(); ++i) {
    auto it = app.monitorsTab.caps.find(app.monitorsTab.outputs[i].name);
    c.caps_for_idx[i] = (it != app.monitorsTab.caps.end()) ? &it->second : nullptr;
  }
  return c;
}

static inline bool monitors_paint_screen_rect_cached(
    const App& app, const eh::settings_monitors_tab::MonitorsTabLayout& lay,
    const MonPaintCanvasCache& c, size_t idx, double* sx, double* sy, double* sw,
    double* sh) {
  if (idx >= c.rects.size() || idx >= app.monitorsTab.outputs.size()) return false;
  const auto& r = c.rects[idx];
  if (app.monitorsTab.outputs[idx].disabled || r.w <= 0) return false;
  *sx = static_cast<double>(lay.canvas_x) + app.monitorsCanvasPanX +
        (static_cast<double>(r.x) - c.min_x) * c.sf;
  *sy = static_cast<double>(lay.canvas_y) + app.monitorsCanvasPanY +
        (static_cast<double>(r.y) - c.min_y) * c.sf;
  *sw = static_cast<double>(r.w) * c.sf;
  *sh = static_cast<double>(r.h) * c.sf;
  return *sw > 1 && *sh > 1;
}

// Shared retained-surface bits (used by MonTextCtx below and the card cache).
struct MonSurfEntry {
  cairo_surface_t* surf = nullptr;
  int sw = 0, sh = 0;  // device px
  uint64_t key = 0;
};

static uint64_t mon_q8(float v) {
  const int q = static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  return static_cast<uint64_t>(static_cast<unsigned>(q));
}

static uint64_t mon_ret_mix(uint64_t h, uint64_t v) {
  h ^= v;
  h *= 1099511628211ULL;
  return h;
}

// Blit a device-resolution retained surface 1:1 onto cr at user (x, y).
static void mon_ret_blit(cairo_t* cr, const MonSurfEntry& e, double x, double y, double ds) {
  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_scale(cr, 1.0 / ds, 1.0 / ds);
  cairo_set_source_surface(cr, e.surf, 0, 0);
  cairo_rectangle(cr, 0, 0, e.sw, e.sh);
  cairo_fill(cr);
  cairo_restore(cr);
}

// Per-paint shared Pango layout for monitors text. settings_show_text() and
// settings_draw_trimmed_text_line() each create+destroy a layout and font
// description per call (~30/frame here); reusing one layout and a few cached
// descriptions removes that churn with identical output.
struct MonTextCtx {
  explicit MonTextCtx(cairo_t* c, double ds) : cr(c), devScale(ds > 0.0 ? ds : 1.0) {
    layout = pango_cairo_create_layout(cr);
  }
  ~MonTextCtx() {
    if (layout) g_object_unref(layout);
    for (auto& e : descs) pango_font_description_free(e.desc);
    // surfs/textBytes are process-lifetime statics, not freed here.
  }
  MonTextCtx(const MonTextCtx&) = delete;
  MonTextCtx& operator=(const MonTextCtx&) = delete;

  PangoFontDescription* desc_for(float fontSize, int fontWeight) {
    for (const auto& e : descs) {
      if (e.size == fontSize && e.weight == fontWeight) return e.desc;
    }
    PangoFontDescription* d = pango_font_description_new();
    pango_font_description_set_family(d, "Inter");
    pango_font_description_set_size(d, static_cast<int>(fontSize * PANGO_SCALE));
    pango_font_description_set_weight(d, static_cast<PangoWeight>(fontWeight));
    descs.push_back({fontSize, fontWeight, d});
    return d;
  }

  void show(const char* text, double x, double y, float fontSize, int fontWeight, float r,
            float g, float b, float a) {
    if (!text || !text[0]) return;
    ++nText;
    int ph = 0;
    const uint64_t key = text_key(text, fontSize, fontWeight, r, g, b, a);
    auto it = surfs.find(key);
    if (it != surfs.end() && it->second.e.surf) {
      ++tHits;
      mon_ret_blit(cr, it->second.e, x, y - it->second.ph + 2.0, devScale);
      return;
    }
    ++tMiss;
    pango_layout_set_font_description(layout, desc_for(fontSize, fontWeight));
    pango_layout_set_text(layout, text, -1);
    int pw = 0;
    pango_layout_get_pixel_size(layout, &pw, &ph);
    (void)pw;
    render_text(r, g, b, a, key, ph);
    mon_ret_blit(cr, surfs[key].e, x, y - ph + 2.0, devScale);
  }

  void trimmed(const std::string& text, double x, double baselineY, size_t approxMaxChars,
               double fadeAlpha, float fontSize, int fontWeight) {
    std::string t = text;
    if (t.size() > approxMaxChars) {
      if (approxMaxChars <= 3)
        t.resize(approxMaxChars);
      else
        t = t.substr(0, approxMaxChars - 3) + "...";
    }
    show(t.c_str(), x, baselineY, fontSize, fontWeight, static_cast<float>(Theme::TextR),
         static_cast<float>(Theme::TextG), static_cast<float>(Theme::TextB),
         static_cast<float>(fadeAlpha));
  }

  // Exact-position show for the ICC path display (matches the old manual
  // measure-then-center block).
  void show_at(const char* text, double x, double y, float fontSize, int fontWeight, float r,
               float g, float b, float a, int* out_h = nullptr) {
    if (!text || !text[0]) return;
    ++nText;
    const uint64_t key = text_key(text, fontSize, fontWeight, r, g, b, a);
    auto it = surfs.find(key);
    if (it != surfs.end() && it->second.e.surf) {
      ++tHits;
      mon_ret_blit(cr, it->second.e, x, y, devScale);
      if (out_h) *out_h = it->second.ph;
      return;
    }
    ++tMiss;
    pango_layout_set_font_description(layout, desc_for(fontSize, fontWeight));
    pango_layout_set_text(layout, text, -1);
    int ph = 0;
    pango_layout_get_pixel_size(layout, nullptr, &ph);
    if (out_h) *out_h = ph;
    render_text(r, g, b, a, key, ph);
    mon_ret_blit(cr, surfs[key].e, x, y, devScale);
  }

  void measure(const char* text, float fontSize, int fontWeight, int* tw, int* th) {
    pango_layout_set_font_description(layout, desc_for(fontSize, fontWeight));
    pango_layout_set_text(layout, text ? text : "", -1);
    pango_layout_get_pixel_size(layout, tw, th);
  }

  // Retained text bitmaps: static labels paint once per distinct
  // (string, size, weight, color, scale), then blit. Dynamic values re-render
  // only when the string changes.
  uint64_t text_key(const char* text, float fontSize, int fontWeight, float r, float g, float b,
                    float a) {
    uint64_t k = 1469598103934665603ULL;
    k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(devScaleQ())));
    // Pack size+weight without float formatting: size*16 is exact for our sizes.
    k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(fontSize * 16.0f)));
    k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(fontWeight)));
    k = mon_ret_mix(k, mon_q8(r));
    k = mon_ret_mix(k, mon_q8(g));
    k = mon_ret_mix(k, mon_q8(b));
    k = mon_ret_mix(k, mon_q8(a));
    for (const char* p = text; *p; ++p) {
      k ^= static_cast<uint64_t>(static_cast<unsigned char>(*p));
      k *= 1099511628211ULL;
    }
    return k;
  }

  int devScaleQ() const {
    int q = static_cast<int>(std::lround(devScale * 128.0));
    return q > 0 ? q : 128;
  }

  void render_text(float r, float g, float b, float a, uint64_t key, int ph) {
    if (surfs.size() >= 160) {
      for (auto& kv : surfs)
        if (kv.second.e.surf) cairo_surface_destroy(kv.second.e.surf);
      surfs.clear();
      textBytes = 0;
    }
    int tw = 0, th = ph;
    pango_layout_get_pixel_size(layout, &tw, &th);
    const int sw = std::max(1, static_cast<int>(std::ceil(tw * devScale)) + 2);
    const int sh = std::max(1, static_cast<int>(std::ceil(th * devScale)) + 2);
    TextSurf ts;
    ts.e.surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    ts.e.sw = sw;
    ts.e.sh = sh;
    ts.e.key = key;
    ts.ph = ph;
    cairo_t* tmp = cairo_create(ts.e.surf);
    cairo_set_operator(tmp, CAIRO_OPERATOR_CLEAR);
    cairo_paint(tmp);
    cairo_set_operator(tmp, CAIRO_OPERATOR_OVER);
    cairo_scale(tmp, devScale, devScale);
    cairo_translate(tmp, 1.0 / devScale, 1.0 / devScale);
    cairo_set_source_rgba(tmp, r, g, b, a);
    pango_cairo_show_layout(tmp, layout);
    cairo_destroy(tmp);
    textBytes += static_cast<unsigned long long>(sw) * static_cast<unsigned long long>(sh) * 4ULL;
    surfs.emplace(key, ts);
  }

  cairo_t* cr;
  double devScale = 1.0;
  PangoLayout* layout = nullptr;
  unsigned nText = 0;  // texts painted this frame (element census)
  unsigned tHits = 0, tMiss = 0;
  struct TextSurf {
    MonSurfEntry e;
    int ph = 0;  // user-unit height (baseline adjust)
  };
  struct DescEnt {
    float size;
    int weight;
    PangoFontDescription* desc;
  };
  std::vector<DescEnt> descs;
  // Retained across frames (process lifetime): static labels blit forever,
  // dynamic values re-render only when the string changes.
  static std::unordered_map<uint64_t, TextSurf> surfs;
  static unsigned long long textBytes;
};

std::unordered_map<uint64_t, MonTextCtx::TextSurf> MonTextCtx::surfs;
unsigned long long MonTextCtx::textBytes = 0;

// ---- Retained background cache (pixel-identical, state-keyed) ----
// Section cards, the canvas backdrop and toolbar buttons are pure functions
// of (geometry, theme colors, alpha, hover/enabled state, device scale):
// repainting ~20 glassy gradients + m3 buttons every frame costs ~1ms.
// Render each once per distinct state into an image surface and blit.
// Key includes every paint input, so a hit is pixel-identical by construction.
enum MonRetCard : uint64_t {
  kRetCardHeader = 0,
  kRetCardDisplay,
  kRetCardScale,
  kRetCardColor,
  kRetCardHdr,
  kRetCardLum,
  kRetCanvasBg,
  kRetCardCount
};

struct MonRetainedCache {
  struct Entry {
    cairo_surface_t* surf = nullptr;
    int sw = 0, sh = 0;  // device px
    uint64_t key = 0;
  };
  std::array<Entry, static_cast<size_t>(kRetCardCount)> cards{};
  std::unordered_map<uint64_t, Entry> widgets;  // toolbar buttons, bounded
  unsigned ch = 0, cm = 0;                      // per-frame card hits/misses
  unsigned wh = 0, wm = 0;                      // per-frame widget hits/misses
  unsigned long long bytes = 0;
  ~MonRetainedCache() {
    for (auto& e : cards)
      if (e.surf) cairo_surface_destroy(e.surf);
    for (auto& kv : widgets)
      if (kv.second.surf) cairo_surface_destroy(kv.second.surf);
  }
  void frame_reset() { ch = cm = wh = wm = 0; }
  void drop_bytes(unsigned long long n) { bytes -= std::min(bytes, n); }
};

static double mon_device_scale(cairo_t* cr) {
  cairo_matrix_t m;
  cairo_get_matrix(cr, &m);
  double ds = std::hypot(m.xx, m.xy);
  if (!(ds > 0.0) || !std::isfinite(ds)) ds = 1.0;
  return ds;
}

template <typename PaintFn>
static void mon_ret_render(MonRetainedCache::Entry& e, int sw, int sh, double ds, uint64_t key,
                           PaintFn&& paint) {
  if (e.surf) cairo_surface_destroy(e.surf);
  e.surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
  e.sw = sw;
  e.sh = sh;
  e.key = key;
  cairo_t* tmp = cairo_create(e.surf);
  cairo_set_operator(tmp, CAIRO_OPERATOR_CLEAR);
  cairo_paint(tmp);
  cairo_set_operator(tmp, CAIRO_OPERATOR_OVER);
  cairo_scale(tmp, ds, ds);
  paint(tmp);
  cairo_destroy(tmp);
}

static void mon_ret_blit(cairo_t* cr, const MonRetainedCache::Entry& e, double x, double y,
                         double ds) {
  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_scale(cr, 1.0 / ds, 1.0 / ds);
  cairo_set_source_surface(cr, e.surf, 0, 0);
  cairo_rectangle(cr, 0, 0, e.sw, e.sh);
  cairo_fill(cr);
  cairo_restore(cr);
}

// Fixed-slot card/canvas entry. Colors must match settings_card()/canvas-box.
template <typename PaintFn>
static bool mon_ret_card(cairo_t* cr, MonRetainedCache& c, MonRetCard id, double x, double y, int w,
                         int h, uint64_t key, double ds, PaintFn&& paint) {
  if (w <= 0 || h <= 0) return false;
  const int sw = std::max(1, static_cast<int>(std::ceil(w * ds)));
  const int sh = std::max(1, static_cast<int>(std::ceil(h * ds)));
  auto& e = c.cards[static_cast<size_t>(id)];
  if (e.surf && e.key == key && e.sw == sw && e.sh == sh) {
    mon_ret_blit(cr, e, x, y, ds);
    ++c.ch;
    return true;
  }
  c.drop_bytes(static_cast<unsigned long long>(e.sw) * static_cast<unsigned long long>(e.sh) * 4ULL);
  mon_ret_render(e, sw, sh, ds, key, std::forward<PaintFn>(paint));
  c.bytes += static_cast<unsigned long long>(sw) * static_cast<unsigned long long>(sh) * 4ULL;
  mon_ret_blit(cr, e, x, y, ds);
  ++c.cm;
  return false;
}

// Bounded map entry for small widgets (toolbar buttons).
template <typename PaintFn>
static bool mon_ret_widget(cairo_t* cr, MonRetainedCache& c, uint64_t key, double x, double y, int w,
                           int h, double ds, PaintFn&& paint, double pad = 0.0) {
  if (w <= 0 || h <= 0) return false;
  const int sw = std::max(1, static_cast<int>(std::ceil((w + 2.0 * pad) * ds)));
  const int sh = std::max(1, static_cast<int>(std::ceil((h + 2.0 * pad) * ds)));
  auto it = c.widgets.find(key);
  if (it != c.widgets.end() && it->second.surf && it->second.sw == sw && it->second.sh == sh) {
    mon_ret_blit(cr, it->second, x - pad, y - pad, ds);
    ++c.wh;
    return true;
  }
  if (c.widgets.size() >= 64) {
    for (auto& kv : c.widgets)
      if (kv.second.surf) cairo_surface_destroy(kv.second.surf);
    c.widgets.clear();
    c.bytes = 0;
  }
  MonRetainedCache::Entry e;
  mon_ret_render(e, sw, sh, ds, key, std::forward<PaintFn>(paint));
  c.bytes += static_cast<unsigned long long>(sw) * static_cast<unsigned long long>(sh) * 4ULL;
  mon_ret_blit(cr, e, x - pad, y - pad, ds);
  ++c.wm;
  auto res = c.widgets.emplace(key, e);
  (void)res;
  return false;
}

// settings_card() colors, factored out so paint and cache key agree.
static void mon_card_colors(const App& app, double glassOv, float* r, float* g, float* b,
                            float* a) {
  if (app.drawChromeMatugen) {
    *r = static_cast<float>(app.drawChrome.panelFillR * 0.35);
    *g = static_cast<float>(app.drawChrome.panelFillG * 0.35);
    *b = static_cast<float>(app.drawChrome.panelFillB * 0.35);
  } else {
    *r = static_cast<float>(Theme::BgR * 0.35);
    *g = static_cast<float>(Theme::BgG * 0.35);
    *b = static_cast<float>(Theme::BgB * 0.35);
  }
  *a = static_cast<float>(0.78 * glassOv);
}

static uint64_t mon_ret_cardkey(MonRetCard id, int w, int h, int dsQ, float r, float g, float b,
                                float a) {
  uint64_t k = 1469598103934665603ULL;
  k = mon_ret_mix(k, static_cast<uint64_t>(id));
  k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(w)));
  k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(h)));
  k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(dsQ)));
  k = mon_ret_mix(k, mon_q8(r));
  k = mon_ret_mix(k, mon_q8(g));
  k = mon_ret_mix(k, mon_q8(b));
  k = mon_ret_mix(k, mon_q8(a));
  return k;
}

// Toolbar/small-button key: label idx + final size + hover/enabled + theme + scale.
// (m3::Button surface colors are never overridden here, so they are constant.)
static uint64_t mon_ret_btnkey(int idx, int w, int h, int dsQ, int hov, int en, float ar, float ag,
                               float ab, float or_, float og, float ob) {
  uint64_t k = 1469598103934665603ULL;
  k = mon_ret_mix(k, static_cast<uint64_t>(16));
  k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(idx)));
  k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(w)));
  k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(h)));
  k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(dsQ)));
  k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(hov)));
  k = mon_ret_mix(k, static_cast<uint64_t>(static_cast<uint32_t>(en)));
  k = mon_ret_mix(k, mon_q8(ar));
  k = mon_ret_mix(k, mon_q8(ag));
  k = mon_ret_mix(k, mon_q8(ab));
  k = mon_ret_mix(k, mon_q8(or_));
  k = mon_ret_mix(k, mon_q8(og));
  k = mon_ret_mix(k, mon_q8(ob));
  return k;
}

static int monitors_hypr_slider_paint_v(const eh::settings_monitors::MonitorRow& row,
                                        int kind, int* vmin, int* vmax,
                                        char* disp, size_t dispn) {
   
  switch (kind) {
    case 0: {
      double d = row.sdrbrightness.empty() ? 1.0 : std::strtod(row.sdrbrightness.c_str(), nullptr);
      d = std::clamp(d, 0.1, 2.0);
      *vmin = 10;
      *vmax = 200;
      std::snprintf(disp, dispn, "%.2f", d);
      return static_cast<int>(std::lround(d * 100.0));
    }
    case 1: {
      double d = row.sdrsaturation.empty() ? 1.0 : std::strtod(row.sdrsaturation.c_str(), nullptr);
      d = std::clamp(d, 0.0, 2.0);
      *vmin = 0;
      *vmax = 200;
      std::snprintf(disp, dispn, "%.2f", d);
      return static_cast<int>(std::lround(d * 100.0));
    }
    case 2: {
      double d = row.sdr_min_luminance.empty() ? 0.0 : std::strtod(row.sdr_min_luminance.c_str(), nullptr);
      d = std::clamp(d, 0.0, 0.01);
      *vmin = 0;
      *vmax = 100;
      std::snprintf(disp, dispn, "%.4f", d);
      return static_cast<int>(std::lround(d * 10000.0));
    }
    case 3: {
      int n = row.sdr_max_luminance.empty() ? 200 : std::atoi(row.sdr_max_luminance.c_str());
      n = std::clamp(n, 80, 400);
      *vmin = 80;
      *vmax = 400;
      std::snprintf(disp, dispn, "%d nits", n);
      return n;
    }
    case 4: {
      double d = row.min_luminance.empty() ? 0.0 : std::strtod(row.min_luminance.c_str(), nullptr);
      d = std::clamp(d, 0.0, 0.01);
      *vmin = 0;
      *vmax = 100;
      std::snprintf(disp, dispn, "%.4f", d);
      return static_cast<int>(std::lround(d * 10000.0));
    }
    case 5: {
      int n = row.max_luminance.empty() ? 600 : std::atoi(row.max_luminance.c_str());
      n = std::clamp(n, 0, 2000);
      *vmin = 0;
      *vmax = 2000;
      std::snprintf(disp, dispn, "%d nits", n);
      return n;
    }
    case 6: {
      int n = row.max_avg_luminance.empty() ? 400 : std::atoi(row.max_avg_luminance.c_str());
      n = std::clamp(n, 0, 2000);
      *vmin = 0;
      *vmax = 2000;
      std::snprintf(disp, dispn, "%d nits", n);
      return n;
    }
    default:
      *vmin = 0;
      *vmax = 1;
      disp[0] = '\0';
      return 0;
  }
}



// Exported helpers called from settings_app.cpp.

static void monitors_clamp_selected(App& app) {
   
  if (app.monitorsTab.outputs.empty()) {
    app.monitorsSelectedIdx = 0;
    return;
  }
  app.monitorsSelectedIdx =
      std::clamp(app.monitorsSelectedIdx, 0,
                 static_cast<int>(app.monitorsTab.outputs.size()) - 1);
}

static int monitors_tri_auto_field_sel(const std::string& s) {
   
  if (s.empty() || s == "0") return 0;
  if (s == "1") return 1;
  return 2;
}

static int monitors_sdr_eotf_sel(const std::string& s) {
   
  if (s.empty()) return 0;
  return std::clamp(std::atoi(s.c_str()), 0, 2);
}

[[maybe_unused]] static void monitors_canvas_transform(const App& app,
                                const eh::settings_monitors_tab::MonitorsTabLayout& lay,
                                double* min_x, double* min_y, double* sf_out) {
   
  std::vector<eh::settings_monitors_tab::ArrangeRect> rects;
  eh::settings_monitors_tab::layout_rects_from_outputs(app.monitorsTab.outputs,
                                                       app.monitorsTab.caps, &rects);
  double span_w = 1, span_h = 1, fit_scale = 1;
  eh::settings_monitors_tab::compute_canvas_fit(rects, lay.canvas_w, lay.canvas_h, 0.1,
                                                min_x, min_y, &span_w, &span_h, &fit_scale);
  *sf_out = fit_scale * app.monitorsCanvasZoom;
}

static void monitors_center_canvas_view(App& app,
                                 const eh::settings_monitors_tab::MonitorsTabLayout& lay) {
   
  std::vector<eh::settings_monitors_tab::ArrangeRect> rects;
  eh::settings_monitors_tab::layout_rects_from_outputs(app.monitorsTab.outputs,
                                                       app.monitorsTab.caps, &rects);
  double min_x = 0, min_y = 0, span_w = 1, span_h = 1, fit_scale = 1;
  eh::settings_monitors_tab::compute_canvas_fit(rects, lay.canvas_w, lay.canvas_h, 0.1,
                                                &min_x, &min_y, &span_w, &span_h, &fit_scale);
  const double sf = fit_scale * app.monitorsCanvasZoom;
  app.monitorsCanvasPanX = lay.canvas_w * 0.5 - (min_x + span_w * 0.5) * sf;
  app.monitorsCanvasPanY = lay.canvas_h * 0.5 - (min_y + span_h * 0.5) * sf;
}

static MonitorsFormRows monitors_form_rows(const App& app,
                                    const eh::settings_monitors::MonitorRow& row,
                                    const eh::settings_monitors::OutputCaps* caps) {
   
  MonitorsFormRows fr{};
  fr.show_vrr = app.monitorsTab.kind == CompositorKind::Hyprland ||
                app.monitorsTab.kind == CompositorKind::Mango ||
                (app.monitorsTab.kind == CompositorKind::Niri && caps && caps->vrr_capable);
  fr.d_res = 0;
  fr.d_hz = 1;
  fr.d_vrr = fr.show_vrr ? 2 : -1;
  fr.s_scale = 0;
  fr.s_tf = 1;
  int cr = 0;
  const bool show_bit =
      app.monitorsTab.supports_bitdepth_ui() && app.monitorsTab.kind != CompositorKind::Mango;
  if (show_bit) fr.c_bit = cr++;
  if (app.monitorsTab.supports_color_hdr_ui()) fr.c_cm = cr++;
  fr.c_icc = cr++;
  if (monitors_hdr_panel_visible(app, row, caps)) {
    fr.hdr_panel = true;
    fr.hdr_luminance_panel = true;
    fr.h_hdr_support = 0;
    fr.h_wide = 1;
    fr.h_eotf = 2;
    fr.h_sdr_b = 3;
    fr.h_sdr_s = 4;
    fr.l_sdr_min = 0;
    fr.l_sdr_max = 1;
    fr.l_min = 2;
    fr.l_max = 3;
    fr.l_avg = 4;
  } else if (monitors_wide_eotf_strip_visible(app, row, caps)) {
    fr.hdr_panel = true;
    fr.hdr_luminance_panel = false;
    fr.h_hdr_support = -1;
    fr.h_wide = 0;
    fr.h_eotf = 1;
    fr.h_sdr_b = -1;
    fr.h_sdr_s = -1;
    fr.l_sdr_min = -1;
    fr.l_sdr_max = -1;
    fr.l_min = -1;
    fr.l_max = -1;
    fr.l_avg = -1;
  }
  return fr;
}

static MonitorsFormGeom monitors_form_layout(const eh::settings_monitors_tab::MonitorsTabLayout& lay,
                                      int contentX, int contentW, const MonitorsFormRows& fr) {
   
  MonitorsFormGeom g{};
  const int w = eh::settings_monitors_tab::monitors_main_column_width_px(contentW);
  const int x = eh::settings_monitors_tab::monitors_main_column_left_px(contentX, contentW, w);
  int y = lay.form_y;

  g.header.x = x;
  g.header.y = y;
  g.header.w = w;
  g.header.h = kMonHeaderCardH;
  g.header.content_y0 = y + 18;
  y += kMonHeaderCardH + kMonSecGap;

  const int disp_rows = fr.show_vrr ? 3 : 2;
  const int dh = monitors_section_card_height_rows(disp_rows);
  g.display = {x, y, w, dh, monitors_section_content_y0(y)};
  y += dh + kMonSecGap;

  const int sh = monitors_section_card_height_rows(2);
  g.scale = {x, y, w, sh, monitors_section_content_y0(y)};
  y += sh + kMonSecGap;

  const int color_rows = (fr.c_bit >= 0 ? 1 : 0) + (fr.c_cm >= 0 ? 1 : 0) + (fr.c_icc >= 0 ? 1 : 0);
  g.has_color = color_rows > 0;
  if (g.has_color) {
    const int ch = monitors_section_card_height_rows(color_rows);
    g.color = {x, y, w, ch, monitors_section_content_y0(y)};
    y += ch + kMonSecGap;
  }

  g.has_hdr = fr.hdr_panel;
  g.has_luminance = fr.hdr_luminance_panel;
  if (fr.hdr_panel) {
    const int hdr_rows = (fr.h_hdr_support >= 0) ? 5 : 2;
    const int hh = monitors_section_card_height_rows(hdr_rows);
    g.hdr = {x, y, w, hh, monitors_section_content_y0(y)};
    y += hh + kMonSecGap;
    if (fr.hdr_luminance_panel) {
      const int lh = monitors_section_card_height_rows(5);
      g.luminance = {x, y, w, lh, monitors_section_content_y0(y)};
      y += lh + kMonSecGap;
    }
  }

  g.bottom_y = y + 80;
  return g;
}

static void monitors_form_slider_track_geom_content(int content_y0, int cardX, int cardW,
                                             int rowIx, int* trX, int* trY, int* trW) {
   
  constexpr int kValPx = 56;
  const int right = cardX + cardW - kCardPad;
  const int minX = cardX + kMonFormLabelColW + 12;
  *trW = std::max(96, right - kValPx - minX);
  *trX = right - kValPx - *trW;
  *trY = content_y0 + rowIx * kMonFormRowPitch + 6;
}

// Paint function.

void paint_monitors_tab(App& app, cairo_t* cr, int contentX, int contentW,
                        double cardX, double cardW, double glassOv, double dockMatA,
                        double paintPointerYOffset) {
  using ML = eh::settings::monitors_log::MonitorsLog;
  using MC = std::chrono::steady_clock;
  const auto t_paint0 = MC::now();
  static unsigned s_mon_paint_n = 0;
  static long long s_mon_paint_max_us = 0;
  static long long s_mon_paint_sum_us = 0;

  (void)cardW;
  (void)dockMatA;
  const double pyH = app.pointerY + paintPointerYOffset;

  eh::settings_monitors_tab::MonitorsTabLayout monLay{};
  eh::settings_monitors_tab::compute_monitors_tab_layout(
      static_cast<int>(contentX), contentW, kContentTop, settings_content_viewport_h(app),
      &monLay);

  if (!app.monitorsCanvasLayoutReady) {
    app.monitorsCanvasZoom = 1.0;
    monitors_center_canvas_view(app, monLay);
    app.monitorsCanvasLayoutReady = true;
    app.monitorsCanvasGeomW = monLay.canvas_w;
    app.monitorsCanvasGeomH = monLay.canvas_h;
  } else if (monLay.canvas_w != app.monitorsCanvasGeomW ||
             monLay.canvas_h != app.monitorsCanvasGeomH) {
    app.monitorsCanvasGeomW = monLay.canvas_w;
    app.monitorsCanvasGeomH = monLay.canvas_h;
    monitors_center_canvas_view(app, monLay);
  }
  const auto t_after_layout = MC::now();

  // Per-paint canvas cache: 1x layout_rects + 1x fit for the whole frame.
  const MonPaintCanvasCache monCache = monitors_paint_canvas_cache_build(app, monLay);
  const auto t_after_cache = MC::now();
  // Retained background cache (section cards, canvas backdrop, buttons).
  static MonRetainedCache s_monCache;
  s_monCache.frame_reset();
  const double monDs = mon_device_scale(cr);
  const int monDsQ = monDs > 0.0 ? static_cast<int>(std::lround(monDs * 128.0)) : 128;
  // Shared text layout for all monitors-tab text (1 layout vs ~30 create/destroy).
  MonTextCtx monTx(cr, monDs);
  // Cached section card: identical pixels to settings_card().
  auto mon_card = [&](MonRetCard id, double x, double y, int w, int h) {
    float r, g, b, a;
    mon_card_colors(app, glassOv, &r, &g, &b, &a);
    const uint64_t key = mon_ret_cardkey(id, w, h, monDsQ, r, g, b, a);
    mon_ret_card(cr, s_monCache, id, x, y, w, h, key, monDs, [=](cairo_t* t) {
      m3::Box box;
      box.setColor(r, g, b, a);
      box.setRadius(static_cast<float>(kCardRad));
      box.setGeometry(0, 0, static_cast<float>(w), static_cast<float>(h));
      box.setGlassy(true);
      box.paint(t);
    });
  };

  settings_label(cr, cardX + kCardPad,
                 static_cast<double>(kContentTop + 22), "Monitors",
                 "Drag outputs on the canvas. Apply writes config and reloads the compositor.");

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  // Button ids for the retained cache (labels are static per id).
  // 0=Refresh 1=Restart portals 2=Apply 3=Revert 4=Center view 5=Align top 6=Browse 7=Clear
  auto paint_mon_small_btn = [&](int bx, int by, int bw, int bh, int idx, const char* label,
                                 bool hot, float alphaScale) {
    const int en = alphaScale > 0.5f ? 1 : 0;
    const int hov = hot ? 1 : 0;
    // Resolve final geometry first (m3 may widen+recenter for long labels);
    // the cached bitmap holds the button at origin, blitted at the resolved spot.
    m3::Button gb;
    gb.setMinSize(0, 0);
    gb.setLabel(label);
    gb.setGeometry(0, 0, static_cast<float>(bw), static_cast<float>(bh));
    gb.setStyle(m3::Button::Style::Outlined);
    gb.setSize(m3::Button::Size::XS);
    gb.setEnabled(en != 0);
    gb.setAccentColor(a_r, a_g, a_b);
    gb.setOutlineColor(o_r, o_g, o_b);
    gb.setHovered(hov != 0);
    gb.setPressed(false);
    const float gdx = gb.x(), gdy = gb.y();
    const int fw = std::max(1, static_cast<int>(std::lround(gb.width())));
    const int fh = std::max(1, static_cast<int>(std::lround(gb.height())));
    const uint64_t key =
        mon_ret_btnkey(idx, fw, fh, monDsQ, hov, en, a_r, a_g, a_b, o_r, o_g, o_b);
    gb.setGeometry(-gdx, -gdy, static_cast<float>(bw), static_cast<float>(bh));
    mon_ret_widget(cr, s_monCache, key, bx + gdx, by + gdy, fw, fh, monDs,
                   [&](cairo_t* t) { gb.paint(t); });
  };

  const bool dirtyMon = app.monitorsTab.dirty;
  const bool hRef = point_in_rect(app.pointerX, pyH, monLay.toolbar_refresh_x, monLay.toolbar_y,
                                  monLay.toolbar_btn_w, monLay.toolbar_btn_h);
  const bool hPortals = point_in_rect(app.pointerX, pyH, monLay.toolbar_portals_x, monLay.toolbar_y,
                                      monLay.toolbar_portals_w, monLay.toolbar_btn_h);
  const bool hApply = point_in_rect(app.pointerX, pyH, monLay.toolbar_apply_x, monLay.toolbar_y,
                                    monLay.toolbar_btn_w, monLay.toolbar_btn_h);
  const bool hRev = point_in_rect(app.pointerX, pyH, monLay.toolbar_revert_x, monLay.toolbar_y,
                                  monLay.toolbar_btn_w, monLay.toolbar_btn_h);
  paint_mon_small_btn(monLay.toolbar_refresh_x, monLay.toolbar_y, monLay.toolbar_btn_w,
                      monLay.toolbar_btn_h, 0, "Refresh", hRef, 1.f);
  paint_mon_small_btn(monLay.toolbar_portals_x, monLay.toolbar_y, monLay.toolbar_portals_w,
                      monLay.toolbar_btn_h, 1, "Restart portals", hPortals, 1.f);
  paint_mon_small_btn(monLay.toolbar_apply_x, monLay.toolbar_y, monLay.toolbar_btn_w,
                      monLay.toolbar_btn_h, 2, "Apply", hApply, dirtyMon ? 1.f : 0.45f);
  paint_mon_small_btn(monLay.toolbar_revert_x, monLay.toolbar_y, monLay.toolbar_btn_w,
                      monLay.toolbar_btn_h, 3, "Revert", hRev, dirtyMon ? 1.f : 0.45f);

  const bool hCent = point_in_rect(app.pointerX, pyH, monLay.center_btn_x, monLay.aux_btn_y,
                                    monLay.aux_btn_w, monLay.aux_btn_h);
  const bool hAlign = point_in_rect(app.pointerX, pyH, monLay.align_top_btn_x, monLay.aux_btn_y,
                                    monLay.aux_btn_w, monLay.aux_btn_h);
  paint_mon_small_btn(monLay.center_btn_x, monLay.aux_btn_y, monLay.aux_btn_w, monLay.aux_btn_h,
                      4, "Center view", hCent, 1.f);
  paint_mon_small_btn(monLay.align_top_btn_x, monLay.aux_btn_y, monLay.aux_btn_w,
                      monLay.aux_btn_h, 5, "Align top", hAlign, 1.f);
  const auto t_after_toolbar = MC::now();

  if (!app.monitorsTab.status.empty()) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    const double metaY0 = static_cast<double>(monLay.toolbar_y + monLay.toolbar_btn_h + 10);
    monTx.trimmed(app.monitorsTab.status,
                  static_cast<double>(contentX) + kCardPad, metaY0, 96,
                  0.82 * glassOv, 11.f, 400);
  }

  {
    float r, g, b;
    if (app.drawChromeMatugen) {
      r = app.drawChrome.panelFillR;
      g = app.drawChrome.panelFillG;
      b = app.drawChrome.panelFillB;
    } else {
      r = static_cast<float>(Theme::BgR);
      g = static_cast<float>(Theme::BgG);
      b = static_cast<float>(Theme::BgB);
    }
    const float a = static_cast<float>(0.55 * glassOv);
    const uint64_t key = mon_ret_cardkey(kRetCanvasBg, monLay.canvas_w, monLay.canvas_h, monDsQ,
                                         r, g, b, a);
    mon_ret_card(cr, s_monCache, kRetCanvasBg, static_cast<double>(monLay.canvas_x),
                 static_cast<double>(monLay.canvas_y), monLay.canvas_w, monLay.canvas_h, key,
                 monDs, [=](cairo_t* t) {
                   m3::Box box;
                   box.setColor(r, g, b, a);
                   box.setRadius(10.f);
                   box.setGeometry(0, 0, static_cast<float>(monLay.canvas_w),
                                   static_cast<float>(monLay.canvas_h));
                   box.setGlassy(true);
                   box.paint(t);
                 });
  }
  cairo_round_rect(cr, static_cast<double>(monLay.canvas_x),
                   static_cast<double>(monLay.canvas_y),
                   static_cast<double>(monLay.canvas_w),
                   static_cast<double>(monLay.canvas_h), 10.0);
  paint_src_glass_hi(app, cr, 0.10 * glassOv);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  const auto t_after_canvas_bg = MC::now();

  const int nMon = static_cast<int>(app.monitorsTab.outputs.size());
  cairo_save(cr);
  cairo_round_rect(cr, static_cast<double>(monLay.canvas_x),
                   static_cast<double>(monLay.canvas_y),
                   static_cast<double>(monLay.canvas_w),
                   static_cast<double>(monLay.canvas_h), 10.0);
  cairo_clip(cr);

  for (int ii = 0; ii < nMon; ++ii) {
    double sx = 0, sy = 0, sw = 0, sh = 0;
    if (!monitors_paint_screen_rect_cached(app, monLay, monCache, static_cast<size_t>(ii), &sx, &sy,
                                           &sw, &sh))
      continue;
    const auto& row = app.monitorsTab.outputs[static_cast<size_t>(ii)];
    const bool sel = ii == app.monitorsSelectedIdx;
    {
      float hr, hg, hb;
      if (app.drawChromeMatugen) {
        hr = 0.48f + 0.52f * app.drawChrome.outlineR;
        hg = 0.48f + 0.52f * app.drawChrome.outlineG;
        hb = 0.48f + 0.52f * app.drawChrome.outlineB;
      } else {
        hr = 1.f; hg = 1.f; hb = 1.f;
      }
      double ha;
      if (row.disabled) {
        ha = 0.06 * glassOv;
      } else if (sel) {
        ha = 0.22 * glassOv;
      } else {
        ha = 0.14 * glassOv;
      }
      const float fa = static_cast<float>(ha);
      // Position-free key on exact size bits: static frames hit; zoom/pan
      // transit misses and rerenders (transient, bounded by map cap).
      uint64_t swb = 0, shb = 0;
      static_assert(sizeof(swb) == sizeof(sw));
      std::memcpy(&swb, &sw, sizeof(sw));
      std::memcpy(&shb, &sh, sizeof(sh));
      uint64_t rkey = 1469598103934665603ULL;
      rkey = mon_ret_mix(rkey, static_cast<uint64_t>(65));
      rkey = mon_ret_mix(rkey, swb);
      rkey = mon_ret_mix(rkey, shb);
      rkey = mon_ret_mix(rkey, static_cast<uint64_t>(static_cast<uint32_t>(monDsQ)));
      rkey = mon_ret_mix(rkey, static_cast<uint64_t>(row.disabled ? 2 : (sel ? 1 : 0)));
      rkey = mon_ret_mix(rkey, mon_q8(hr));
      rkey = mon_ret_mix(rkey, mon_q8(hg));
      rkey = mon_ret_mix(rkey, mon_q8(hb));
      rkey = mon_ret_mix(rkey, mon_q8(fa));
      const int iw = std::max(1, static_cast<int>(std::ceil(sw)));
      const int ih = std::max(1, static_cast<int>(std::ceil(sh)));
      m3::Box rprobe;
      rprobe.setColor(hr, hg, hb, fa);
      rprobe.setRadius(6.f);
      rprobe.setGeometry(0, 0, static_cast<float>(sw), static_cast<float>(sh));
      rprobe.setGlassy(true);
      mon_ret_widget(cr, s_monCache, rkey, sx, sy, iw, ih, monDs,
                     [&](cairo_t* t) { rprobe.paint(t); });
    }
    cairo_round_rect(cr, sx, sy, sw, sh, 6.0);
    cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB,
                          sel ? 0.55 * glassOv : 0.22 * glassOv);
    cairo_set_line_width(cr, sel ? 2.0 : 1.0);
    cairo_stroke(cr);

    monTx.show(row.name.c_str(), sx + 8, sy + 18, 11, 700, Theme::TextR, Theme::TextG, Theme::TextB,
                 row.disabled ? 0.35f * static_cast<float>(glassOv) : 0.9f * static_cast<float>(glassOv));

    // OPTIMIZED: reuse cached caps pointer (no unordered_map find per monitor)
    // and stack buffer for badges (no std::string alloc per monitor per frame).
    const eh::settings_monitors::OutputCaps* ocaps =
        (static_cast<size_t>(ii) < monCache.caps_for_idx.size()) ? monCache.caps_for_idx[static_cast<size_t>(ii)]
                                                                 : nullptr;
    char badges[32];
    badges[0] = '\0';
    {
      size_t bp = 0;
      if (ocaps && ocaps->hdr_hint && bp + 4 < sizeof(badges)) {
        badges[bp++] = 'H';
        badges[bp++] = 'D';
        badges[bp++] = 'R';
        badges[bp++] = ' ';
        badges[bp] = '\0';
      }
      if (!row.bitdepth.empty() && bp + 4 < sizeof(badges)) {
        badges[bp++] = '1';
        badges[bp++] = '0';
        badges[bp++] = 'b';
        badges[bp++] = ' ';
        badges[bp] = '\0';
      }
      const int vv = row.vrr.empty() ? 0 : std::atoi(row.vrr.c_str());
      if (vv > 0 && bp + 4 < sizeof(badges)) {
        badges[bp++] = 'V';
        badges[bp++] = 'R';
        badges[bp++] = 'R';
        badges[bp++] = ' ';
        badges[bp] = '\0';
      }
    }
    if (badges[0] != '\0') {
      monTx.show(badges, sx + 8, sy + sh - 8, 9, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
    }
  }
  cairo_restore(cr);
  const auto t_after_canvas_mon = MC::now();

  // Pills
  {
    int pillX = monLay.canvas_x;
    const int pillY = monLay.pills_y;
    for (int ii = 0; ii < nMon; ++ii) {
      const std::string& nm = app.monitorsTab.outputs[static_cast<size_t>(ii)].name;
      const int pillW = static_cast<int>(nm.size()) * 8 + 24;
      const bool ph = point_in_rect(app.pointerX, pyH, pillX, pillY, pillW, kMonPillH);
      const bool psel = ii == app.monitorsSelectedIdx;
      {
        float hr, hg, hb;
        if (app.drawChromeMatugen) {
          hr = 0.48f + 0.52f * app.drawChrome.outlineR;
          hg = 0.48f + 0.52f * app.drawChrome.outlineG;
          hb = 0.48f + 0.52f * app.drawChrome.outlineB;
        } else {
          hr = 1.f; hg = 1.f; hb = 1.f;
        }
        const float pa = static_cast<float>((psel ? 0.22 : 0.12) + (ph ? 0.06 : 0.0));
        uint64_t pkey = 1469598103934665603ULL;
        pkey = mon_ret_mix(pkey, static_cast<uint64_t>(66));
        pkey = mon_ret_mix(pkey, static_cast<uint64_t>(static_cast<uint32_t>(pillW)));
        pkey = mon_ret_mix(pkey, static_cast<uint64_t>(static_cast<uint32_t>(monDsQ)));
        pkey = mon_ret_mix(pkey, static_cast<uint64_t>(psel ? 1 : 0));
        pkey = mon_ret_mix(pkey, static_cast<uint64_t>(ph ? 1 : 0));
        pkey = mon_ret_mix(pkey, mon_q8(hr));
        pkey = mon_ret_mix(pkey, mon_q8(hg));
        pkey = mon_ret_mix(pkey, mon_q8(hb));
        pkey = mon_ret_mix(pkey, mon_q8(pa));
        m3::Box pprobe;
        pprobe.setColor(hr, hg, hb, pa);
        pprobe.setRadius(static_cast<float>(kMonPillH) * 0.45f);
        pprobe.setGeometry(0, 0, static_cast<float>(pillW), static_cast<float>(kMonPillH));
        pprobe.setGlassy(true);
        mon_ret_widget(cr, s_monCache, pkey, pillX, pillY, pillW, kMonPillH, monDs,
                       [&](cairo_t* t) { pprobe.paint(t); });
      }
      monTx.show(nm.c_str(), pillX + 12, pillY + 19, 11, psel ? 700 : 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
      pillX += pillW + 8;
    }
  }
  const auto t_after_pills = MC::now();
  // Form-section split (assigned inside the form block; default to pills when empty).
  auto t_form_header = t_after_pills;
  auto t_form_display = t_after_pills;
  auto t_form_scale = t_after_pills;
  auto t_form_color = t_after_pills;
  auto t_form_hdr = t_after_pills;
  auto t_form_lum = t_after_pills;
  // Accumulated settings_slider() paint cost this frame (reported as dragsl=
  // while a slider drag is active).
  long long us_sliders = 0;
  // Element census for the paint line (combos/sliders/glyphs/cards/texts).
  unsigned nCombos = 0, nSliders = 0, nGlyphs = 0;
  int nCards = 0;

  // Form cards
  if (!app.monitorsTab.outputs.empty()) {
    monitors_clamp_selected(app);
    const auto& srow = app.monitorsTab.outputs[static_cast<size_t>(app.monitorsSelectedIdx)];
    // OPTIMIZED: reuse cached caps pointer (no second unordered_map find).
    const eh::settings_monitors::OutputCaps* scaps =
        (static_cast<size_t>(app.monitorsSelectedIdx) < monCache.caps_for_idx.size())
            ? monCache.caps_for_idx[static_cast<size_t>(app.monitorsSelectedIdx)]
            : nullptr;
    const MonitorsFormRows fr = monitors_form_rows(app, srow, scaps);
    const MonitorsFormGeom fg = monitors_form_layout(monLay, static_cast<int>(contentX), contentW,
                                                     fr);
    nCards = 3 + (fg.has_color ? 1 : 0) + (fg.has_hdr ? 1 : 0) + (fg.has_luminance ? 1 : 0);

    auto paint_sec_heading = [&](const MonitorsSectionGeom& sec, const char* title) {
      monTx.show(title, static_cast<double>(sec.x + kCardPad),
                 static_cast<double>(sec.y + kMonSecTitlePadTop + 14), 12, 700, Theme::TextR,
                 Theme::TextG, Theme::TextB, 1.0 * glassOv);
    };

    auto paint_mon_combo_at_row = [&](const MonitorsSectionGeom& sec, int rowIx,
                                      const char* label, const char* valueText, bool expanded) {
      ++nCombos;
      ++nGlyphs;
      const int labY = sec.content_y0 + rowIx * kMonFormRowPitch;
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      monTx.trimmed(label, sec.x + kCardPad,
                      static_cast<double>(labY + 19), 22, 0.82 * glassOv, 12.f, 400);
      int cx = 0, cy = 0, cw = 0, ch = 0;
      monitors_combo_geom_at_content_row(sec.content_y0, sec.x, sec.w, rowIx, &cx, &cy, &cw, &ch);
      const bool hovered =
          app.pointerX >= cx && pyH >= cy && app.pointerX < cx + cw && pyH < cy + ch;
      {
        m3::Box probe;
        float r, g, b;
        if (app.drawChromeMatugen) {
          r = app.drawChrome.panelFillR;
          g = app.drawChrome.panelFillG;
          b = app.drawChrome.panelFillB;
        } else {
          r = static_cast<float>(Theme::BgR);
          g = static_cast<float>(Theme::BgG);
          b = static_cast<float>(Theme::BgB);
        }
        const float ca = static_cast<float>((hovered ? 0.94 : 0.88) * glassOv);
        // Position-free key: box+stroke inputs are (size, hover, theme, scale) only.
        // The glass-hi edge stroke is baked into the bitmap (1px pad) — its
        // outline color is part of the key.
        uint64_t ckey = 1469598103934665603ULL;
        ckey = mon_ret_mix(ckey, static_cast<uint64_t>(64));
        ckey = mon_ret_mix(ckey, static_cast<uint64_t>(static_cast<uint32_t>(cw)));
        ckey = mon_ret_mix(ckey, static_cast<uint64_t>(static_cast<uint32_t>(ch)));
        ckey = mon_ret_mix(ckey, static_cast<uint64_t>(static_cast<uint32_t>(monDsQ)));
        ckey = mon_ret_mix(ckey, static_cast<uint64_t>(hovered ? 1 : 0));
        ckey = mon_ret_mix(ckey, mon_q8(r));
        ckey = mon_ret_mix(ckey, mon_q8(g));
        ckey = mon_ret_mix(ckey, mon_q8(b));
        ckey = mon_ret_mix(ckey, mon_q8(ca));
        ckey = mon_ret_mix(ckey, mon_q8(o_r));
        ckey = mon_ret_mix(ckey, mon_q8(o_g));
        ckey = mon_ret_mix(ckey, mon_q8(o_b));
        probe.setColor(r, g, b, ca);
        probe.setRadius(9.f);
        probe.setGeometry(0, 0, static_cast<float>(cw), static_cast<float>(ch));
        probe.setGlassy(true);
        mon_ret_widget(cr, s_monCache, ckey, cx, cy, cw, ch, monDs,
                       [&](cairo_t* t) {
                         cairo_translate(t, 1.0, 1.0);
                         probe.paint(t);
                         cairo_round_rect(t, 0, 0, cw, ch, 9.0);
                         paint_src_glass_hi(app, t, 0.11 * glassOv);
                         cairo_set_line_width(t, 1.0);
                         cairo_stroke(t);
                       },
                       1.0);
      }
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      const size_t valChars = static_cast<size_t>(std::clamp((cw - 40) / 7, 16, 52));
      monTx.trimmed(valueText, cx + 12.0, cy + 19.0, valChars,
                    0.88 * glassOv, 12.f, 400);
      material_symbols_draw_glyph(cr, cx + cw - 12.0, cy + 14.0, 12.0,
          expanded ? "arrow_drop_up" : "arrow_drop_down",
          Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
    };

    // Header card (cached background; name+toggle painted below)
    mon_card(kRetCardHeader, static_cast<double>(fg.header.x), static_cast<double>(fg.header.y),
             fg.header.w, fg.header.h);
    monTx.show(srow.name.c_str(), fg.header.x + kCardPad, fg.header.y + 32, 13, 700, Theme::TextR,
                 Theme::TextG, Theme::TextB, 1.0 * glassOv);
    {
      // Enable toggle: visuals depend only on (on/off, theme, scale), so retain it.
      constexpr int kSwW = 52, kSwH = 26;
      const int swX = fg.header.x + fg.header.w - kSwW - kSpacingXL;
      const int swY = fg.header.y + static_cast<int>((52.0 - kSwH) / 2.0);
      const int on = srow.disabled ? 1 : 0;
      uint64_t tkey = 1469598103934665603ULL;
      tkey = mon_ret_mix(tkey, static_cast<uint64_t>(80));
      tkey = mon_ret_mix(tkey, static_cast<uint64_t>(static_cast<uint32_t>(monDsQ)));
      tkey = mon_ret_mix(tkey, static_cast<uint64_t>(on));
      tkey = mon_ret_mix(tkey, mon_q8(a_r));
      tkey = mon_ret_mix(tkey, mon_q8(a_g));
      tkey = mon_ret_mix(tkey, mon_q8(a_b));
      tkey = mon_ret_mix(tkey, mon_q8(s_r));
      tkey = mon_ret_mix(tkey, mon_q8(s_g));
      tkey = mon_ret_mix(tkey, mon_q8(s_b));
      tkey = mon_ret_mix(tkey, mon_q8(t_r));
      tkey = mon_ret_mix(tkey, mon_q8(t_g));
      tkey = mon_ret_mix(tkey, mon_q8(t_b));
      tkey = mon_ret_mix(tkey, mon_q8(o_r));
      tkey = mon_ret_mix(tkey, mon_q8(o_g));
      tkey = mon_ret_mix(tkey, mon_q8(o_b));
      m3::Toggle tprobe;
      tprobe.setGeometry(0, 0, static_cast<float>(kSwW), static_cast<float>(kSwH));
      tprobe.setOn(srow.disabled);
      tprobe.setAccentColor(a_r, a_g, a_b);
      tprobe.setSurfaceColor(s_r, s_g, s_b);
      tprobe.setTextColor(t_r, t_g, t_b);
      tprobe.setOutlineColor(o_r, o_g, o_b);
      tprobe.setHovered(false);
      mon_ret_widget(cr, s_monCache, tkey, swX, swY, kSwW, kSwH, monDs,
                     [&](cairo_t* t) { tprobe.paint(t); });
    }
    t_form_header = MC::now();

    const bool dimForm = srow.disabled;

    // Display section
    mon_card(kRetCardDisplay, static_cast<double>(fg.display.x),
             static_cast<double>(fg.display.y), fg.display.w, fg.display.h);
    paint_sec_heading(fg.display, "Display");
    if (!dimForm) {
      paint_mon_combo_at_row(fg.display, fr.d_res, "Resolution",
                             srow.resolution.empty() ? "\u2014" : srow.resolution.c_str(),
                             app.monitorsActiveDd == 0);
      std::string hzDisp = srow.refresh_rate.empty() ? "\u2014" : (srow.refresh_rate + " Hz");
      paint_mon_combo_at_row(fg.display, fr.d_hz, "Refresh rate", hzDisp.c_str(),
                             app.monitorsActiveDd == 1);
      if (fr.show_vrr && fr.d_vrr >= 0) {
        const int vv = srow.vrr.empty() ? 0 : std::atoi(srow.vrr.c_str());
        const char* vrTxt = "Off";
        if (app.monitorsTab.kind == CompositorKind::Mango)
          vrTxt = vv ? "On" : "Off";
        else if (vv == 1)
          vrTxt = "On";
        else if (vv == 2)
          vrTxt = "On-demand";
        paint_mon_combo_at_row(fg.display, fr.d_vrr, "VRR", vrTxt, app.monitorsActiveDd == 4);
      }
    }
    t_form_display = MC::now();

    // Scale & transform section
    mon_card(kRetCardScale, static_cast<double>(fg.scale.x), static_cast<double>(fg.scale.y),
             fg.scale.w, fg.scale.h);
    paint_sec_heading(fg.scale, "Scale & transform");
    if (!dimForm) {
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      const int scaleLabY = fg.scale.content_y0 + fr.s_scale * kMonFormRowPitch;
      monTx.trimmed("Scale", fg.scale.x + kCardPad,
                      static_cast<double>(scaleLabY + 19), 22, 0.82 * glassOv, 12.f, 400);
      int trXf = 0, trYf = 0, trWf = 0;
      monitors_form_slider_track_geom_content(fg.scale.content_y0, fg.scale.x, fg.scale.w,
                                              fr.s_scale, &trXf, &trYf, &trWf);
      char scaleBuf[48];
      std::snprintf(scaleBuf, sizeof(scaleBuf), "%s\u00d7", srow.scale.c_str());
      const double scDn =
          (app.monitorsScaleSliderDragIdx >= 0) ? app.settingsSliderDragNormT : -1.0;
      {
        const auto t_sl = MC::now();
        settings_slider(app, cr, trXf, trYf, trWf, monitors_form_scale_ticks(srow), 100, 200,
                        paintPointerYOffset, scaleBuf, false, scDn);
        us_sliders += eh::settings::monitors_log::mon_us(t_sl, MC::now());
        ++nSliders;
      }

      const int ti = std::clamp(std::atoi(srow.transform.c_str()), 0, 7);
      paint_mon_combo_at_row(fg.scale, fr.s_tf, "Transform",
                             kMonitorTfLabels[static_cast<size_t>(ti)],
                             app.monitorsActiveDd == 2);
    }
    t_form_scale = MC::now();

    // Color section
    if (fg.has_color) {
      mon_card(kRetCardColor, static_cast<double>(fg.color.x), static_cast<double>(fg.color.y),
               fg.color.w, fg.color.h);
      paint_sec_heading(fg.color, "Color");
      if (!dimForm) {
        if (fr.c_bit >= 0) {
          const char* bdTxt = (srow.bitdepth == "10") ? "10-bit" : "Default";
          paint_mon_combo_at_row(fg.color, fr.c_bit, "Bit depth", bdTxt,
                                 app.monitorsActiveDd == 3);
        }
        if (fr.c_cm >= 0) {
          std::string cmDisp = srow.cm.empty() ? "auto" : srow.cm;
          paint_mon_combo_at_row(fg.color, fr.c_cm, "Color management", cmDisp.c_str(),
                                 app.monitorsActiveDd == 5);
        }
        if (fr.c_icc >= 0) {
          const int rowY = fg.color.content_y0 + fr.c_icc * kMonFormRowPitch;
          const int elY = rowY + (kMonFormRowPitch - kSettingsComboH) / 2;
          cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
          cairo_set_font_size(cr, 12);
          monTx.trimmed("Color profile", fg.color.x + kCardPad,
                          static_cast<double>(rowY + 19), 22, 0.82 * glassOv, 12.f, 400);
          const bool hasIcc = !srow.icc.empty();
          const int valX = fg.color.x + kMonFormLabelColW + kMonFormValRailPx;
          const int valW = fg.color.w - kMonFormLabelColW - kMonFormValRailPx - kCardPad;
          constexpr int kBwBrowse = 80;
          constexpr int kBwClear = 64;
          constexpr int kGap = kSpacingM;
          const int browseW = kBwBrowse;
          const int clearW = hasIcc ? kBwClear : 0;
          const int clearGap = hasIcc ? kGap : 0;
          const int pathW = valW - browseW - clearW - kGap - clearGap;
          {
            m3::Box box;
            float br, bg, bb;
            if (app.drawChromeMatugen) {
              br = app.drawChrome.panelFillR;
              bg = app.drawChrome.panelFillG;
              bb = app.drawChrome.panelFillB;
            } else {
              br = static_cast<float>(Theme::BgR);
              bg = static_cast<float>(Theme::BgG);
              bb = static_cast<float>(Theme::BgB);
            }
            box.setColor(br, bg, bb, 0.88f * static_cast<float>(glassOv));
            box.setRadius(9.f);
            box.setGeometry(static_cast<float>(valX), static_cast<float>(elY),
                            static_cast<float>(pathW), static_cast<float>(kSettingsComboH));
            box.paint(cr);
          }
          std::string pathDisp;
          if (hasIcc) {
            auto lastSlash = srow.icc.find_last_of('/');
            std::string fname = (lastSlash != std::string::npos) ? srow.icc.substr(lastSlash + 1) : srow.icc;
            pathDisp = fname.size() > 28 ? fname.substr(0, 25) + "..." : fname;
          } else {
            pathDisp = "None";
          }
          {
            int th = 12;
            monTx.measure(pathDisp.c_str(), 11.0f, 400, nullptr, &th);
            monTx.show_at(pathDisp.c_str(), static_cast<double>(valX + 8),
                          static_cast<double>(elY + (kSettingsComboH - th) / 2), 11.0f, 400, t_r,
                          t_g, t_b, hasIcc ? 1.0f : 0.45f);
          }
          const int browseX = valX + pathW + kGap;
          const bool hBrowse = point_in_rect(app.pointerX, pyH, browseX, elY, browseW, kSettingsComboH);
          paint_mon_small_btn(browseX, elY, browseW, kSettingsComboH, 6, "Browse", hBrowse, 1.f);
          if (hasIcc) {
            const int clearX = browseX + browseW + kGap;
            const bool hClear = point_in_rect(app.pointerX, pyH, clearX, elY, clearW, kSettingsComboH);
            paint_mon_small_btn(clearX, elY, clearW, kSettingsComboH, 7, "Clear", hClear, 1.f);
          }
        }
      }
    }
    t_form_color = MC::now();

    // HDR section
    if (fg.has_hdr) {
      mon_card(kRetCardHdr, static_cast<double>(fg.hdr.x), static_cast<double>(fg.hdr.y),
               fg.hdr.w, fg.hdr.h);
      paint_sec_heading(fg.hdr, "HDR");
      if (!dimForm) {
        const int wideSel = monitors_tri_auto_field_sel(srow.supports_wide_color);
        if (fr.h_hdr_support >= 0) {
          const int hdrSel = monitors_tri_auto_field_sel(srow.supports_hdr);
          paint_mon_combo_at_row(fg.hdr, fr.h_hdr_support, "HDR support",
                                 kMonSupportsHdrDdLabels[static_cast<size_t>(hdrSel)],
                                 app.monitorsActiveDd == 6);
        }
        paint_mon_combo_at_row(fg.hdr, fr.h_wide, "Wide color gamut",
                               kMonSupportsWideDdLabels[static_cast<size_t>(wideSel)],
                               app.monitorsActiveDd == 7);
        paint_mon_combo_at_row(fg.hdr, fr.h_eotf, "SDR EOTF",
                               kMonSdrEotfDdLabels[static_cast<size_t>(
                                   monitors_sdr_eotf_sel(srow.sdr_eotf))],
                               app.monitorsActiveDd == 8);

        if (fr.h_sdr_b >= 0 && fr.h_sdr_s >= 0) {
          auto paint_mon_hypr_slider_at_row = [&](const MonitorsSectionGeom& sec, int rowIx,
                                                  const char* label, int kind) {
            const int labY = sec.content_y0 + rowIx * kMonFormRowPitch;
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 12);
            monTx.trimmed(label, sec.x + kCardPad,
                            static_cast<double>(labY + 19), 22, 0.82 * glassOv, 12.f, 400);
            int trx = 0, trey = 0, trw = 0;
            monitors_form_slider_track_geom_content(sec.content_y0, sec.x, sec.w, rowIx, &trx,
                                                    &trey, &trw);
            int vmin = 0, vmax = 1;
            char disp[48];
            const int sv =
                monitors_hypr_slider_paint_v(srow, kind, &vmin, &vmax, disp, sizeof disp);
            const double hyDn =
                (app.monitorsHyprExtraSlider == kind) ? app.settingsSliderDragNormT : -1.0;
            {
              const auto t_sl = MC::now();
              settings_slider(app, cr, trx, trey, trw, sv, vmin, vmax, paintPointerYOffset, disp,
                              false, hyDn);
              us_sliders += eh::settings::monitors_log::mon_us(t_sl, MC::now());
              ++nSliders;
            }
          };
          paint_mon_hypr_slider_at_row(fg.hdr, fr.h_sdr_b, "SDR brightness", 0);
          paint_mon_hypr_slider_at_row(fg.hdr, fr.h_sdr_s, "SDR saturation", 1);
        }
      }
      t_form_hdr = MC::now();

      // Luminance section
      if (fg.has_luminance) {
        mon_card(kRetCardLum, static_cast<double>(fg.luminance.x),
                 static_cast<double>(fg.luminance.y), fg.luminance.w, fg.luminance.h);
        paint_sec_heading(fg.luminance, "Luminance");
        if (!dimForm) {
          auto paint_mon_hypr_slider_at_row = [&](const MonitorsSectionGeom& sec, int rowIx,
                                                  const char* label, int kind) {
            const int labY = sec.content_y0 + rowIx * kMonFormRowPitch;
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 12);
            monTx.trimmed(label, sec.x + kCardPad,
                            static_cast<double>(labY + 19), 22, 0.82 * glassOv, 12.f, 400);
            int trx = 0, trey = 0, trw = 0;
            monitors_form_slider_track_geom_content(sec.content_y0, sec.x, sec.w, rowIx, &trx,
                                                    &trey, &trw);
            int vmin = 0, vmax = 1;
            char disp[48];
            const int sv =
                monitors_hypr_slider_paint_v(srow, kind, &vmin, &vmax, disp, sizeof disp);
            const double hyDn2 =
                (app.monitorsHyprExtraSlider == kind) ? app.settingsSliderDragNormT : -1.0;
            {
              const auto t_sl = MC::now();
              settings_slider(app, cr, trx, trey, trw, sv, vmin, vmax, paintPointerYOffset, disp,
                              false, hyDn2);
              us_sliders += eh::settings::monitors_log::mon_us(t_sl, MC::now());
              ++nSliders;
            }
          };
          paint_mon_hypr_slider_at_row(fg.luminance, fr.l_sdr_min, "SDR min luminance", 2);
          paint_mon_hypr_slider_at_row(fg.luminance, fr.l_sdr_max, "SDR max luminance", 3);
          paint_mon_hypr_slider_at_row(fg.luminance, fr.l_min, "Min luminance (HDR)", 4);
          paint_mon_hypr_slider_at_row(fg.luminance, fr.l_max, "Max luminance", 5);
          paint_mon_hypr_slider_at_row(fg.luminance, fr.l_avg, "Max average luminance", 6);
        }
      }
      t_form_lum = MC::now();
    }
  }
  const auto t_paint1 = MC::now();
  {
    using eh::settings::monitors_log::mon_us;
    const long long us_layout = mon_us(t_paint0, t_after_layout);
    const long long us_cache = mon_us(t_after_layout, t_after_cache);
    const long long us_toolbar = mon_us(t_after_cache, t_after_toolbar);
    const long long us_canvas_bg = mon_us(t_after_toolbar, t_after_canvas_bg);
    const long long us_canvas_mon = mon_us(t_after_canvas_bg, t_after_canvas_mon);
    const long long us_pills = mon_us(t_after_canvas_mon, t_after_pills);
    const long long us_form = mon_us(t_after_pills, t_paint1);
    const long long us_f_head = mon_us(t_after_pills, t_form_header);
    const long long us_f_disp = mon_us(t_form_header, t_form_display);
    const long long us_f_scale = mon_us(t_form_display, t_form_scale);
    const long long us_f_color = mon_us(t_form_scale, t_form_color);
    const long long us_f_hdr = mon_us(t_form_color, t_form_hdr);
    const long long us_f_lum = mon_us(t_form_hdr, t_form_lum);
    const long long us_total = mon_us(t_paint0, t_paint1);
    const bool dragActive =
        (app.monitorsScaleSliderDragIdx >= 0 || app.monitorsHyprExtraSlider >= 0);
    if (dragActive) {
      eh::settings::monitors_log::slider_drag_paint(us_total, us_sliders);
    } else {
      eh::settings::monitors_log::slider_drag_end("paint-observed");
    }
    ++s_mon_paint_n;
    s_mon_paint_sum_us += us_total;
    if (us_total > s_mon_paint_max_us) s_mon_paint_max_us = us_total;
    const long long avg = s_mon_paint_n ? (s_mon_paint_sum_us / s_mon_paint_n) : 0;
    const int sel = app.monitorsSelectedIdx;
    const char* selName =
        (!app.monitorsTab.outputs.empty() && sel >= 0 &&
         static_cast<size_t>(sel) < app.monitorsTab.outputs.size())
            ? app.monitorsTab.outputs[static_cast<size_t>(sel)].name.c_str()
            : "-";
    ML::instance().writef(
        "paint#%u total=%lldus layout=%lldus cache=%lldus toolbar=%lldus canvas_bg=%lldus "
        "canvas_mon=%lldus pills=%lldus form=%lldus fhead=%lldus fdisp=%lldus fscale=%lldus "
        "fcolor=%lldus fhdr=%lldus flum=%lldus dragsl=%lldus cause=%s els=C%u/S%u/T%u/G%u/D%d dd=%d retC=%u/%u retW=%u/%u retKB=%llu tx=%u/%u txKB=%llu nMon=%d sel=%d(%s) dirty=%d "
        "zoom=%.2f pan=%.0f,%.0f canvas=%dx%d glass=%.2f avg=%lldus max=%lldus",
        s_mon_paint_n, us_total, us_layout, us_cache, us_toolbar, us_canvas_bg,
        us_canvas_mon, us_pills, us_form, us_f_head, us_f_disp, us_f_scale,
        us_f_color, us_f_hdr, us_f_lum, dragActive ? us_sliders : 0,
        eh::settings::monitors_log::mon_cause_take(), nCombos, nSliders, monTx.nText, nGlyphs,
        nCards, app.monitorsActiveDd, s_monCache.ch, s_monCache.cm, s_monCache.wh, s_monCache.wm,
        s_monCache.bytes / 1024ULL, monTx.tHits, monTx.tMiss,
        MonTextCtx::textBytes / 1024ULL, nMon, sel, selName,
        app.monitorsTab.dirty ? 1 : 0, app.monitorsCanvasZoom, app.monitorsCanvasPanX,
        app.monitorsCanvasPanY, monLay.canvas_w, monLay.canvas_h, glassOv, avg,
        s_mon_paint_max_us);
    if (us_total >= 8000) {
      ML::instance().writef(
          "slow paint#%u total=%lldus (>8ms frame budget) nMon=%d canvas=%dx%d form=%lldus",
          s_mon_paint_n, us_total, nMon, monLay.canvas_w, monLay.canvas_h, us_form);
    }
  }
}
