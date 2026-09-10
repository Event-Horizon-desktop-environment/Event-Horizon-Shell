#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_monitors/settings_tab_monitors.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "m3/controls/containers/button.hpp"
#include "dialog/file_chooser_dialog.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"

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

static bool monitors_canvas_screen_rect(const App& app,
                                        const eh::settings_monitors_tab::MonitorsTabLayout& lay,
                                        size_t idx, double* sx, double* sy,
                                        double* sw, double* sh) {
   
  std::vector<eh::settings_monitors_tab::ArrangeRect> rects;
  eh::settings_monitors_tab::layout_rects_from_outputs(app.monitorsTab.outputs,
                                                       app.monitorsTab.caps, &rects);
  if (idx >= rects.size()) return false;
  const auto& r = rects[idx];
  if (app.monitorsTab.outputs[idx].disabled || r.w <= 0) return false;
  double min_x = 0, min_y = 0, sf = 1;
  monitors_canvas_transform(app, lay, &min_x, &min_y, &sf);
  *sx = static_cast<double>(lay.canvas_x) + app.monitorsCanvasPanX +
        (static_cast<double>(r.x) - min_x) * sf;
  *sy = static_cast<double>(lay.canvas_y) + app.monitorsCanvasPanY +
        (static_cast<double>(r.y) - min_y) * sf;
  *sw = static_cast<double>(r.w) * sf;
  *sh = static_cast<double>(r.h) * sf;
  return *sw > 1 && *sh > 1;
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

static void monitors_canvas_transform(const App& app,
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
   
  (void)cardW;
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

  settings_label(cr, cardX + kCardPad,
                 static_cast<double>(kContentTop + 22), "Monitors",
                 "Drag outputs on the canvas. Apply writes config and reloads the compositor.");

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  auto paint_mon_small_btn = [&](int bx, int by, int bw, int bh, const char* label,
                                 bool hot, float alphaScale) {
    m3::Button btn;
    btn.setMinSize(0, 0);
    btn.setLabel(label);
    btn.setGeometry(static_cast<float>(bx), static_cast<float>(by),
                    static_cast<float>(bw), static_cast<float>(bh));
    btn.setStyle(m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    btn.setEnabled(alphaScale > 0.5f);
    btn.setAccentColor(a_r, a_g, a_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    btn.setHovered(hot);
    btn.setPressed(false);
    btn.paint(cr);
  };

  const bool dirtyMon = app.monitorsTab.dirty;
  const bool hRef = point_in_rect(app.pointerX, pyH, monLay.toolbar_refresh_x, monLay.toolbar_y,
                                  monLay.toolbar_btn_w, monLay.toolbar_btn_h);
  const bool hApply = point_in_rect(app.pointerX, pyH, monLay.toolbar_apply_x, monLay.toolbar_y,
                                    monLay.toolbar_btn_w, monLay.toolbar_btn_h);
  const bool hRev = point_in_rect(app.pointerX, pyH, monLay.toolbar_revert_x, monLay.toolbar_y,
                                  monLay.toolbar_btn_w, monLay.toolbar_btn_h);
  paint_mon_small_btn(monLay.toolbar_refresh_x, monLay.toolbar_y, monLay.toolbar_btn_w,
                      monLay.toolbar_btn_h, "Refresh", hRef, 1.f);
  paint_mon_small_btn(monLay.toolbar_apply_x, monLay.toolbar_y, monLay.toolbar_btn_w,
                      monLay.toolbar_btn_h, "Apply", hApply, dirtyMon ? 1.f : 0.45f);
  paint_mon_small_btn(monLay.toolbar_revert_x, monLay.toolbar_y, monLay.toolbar_btn_w,
                      monLay.toolbar_btn_h, "Revert", hRev, dirtyMon ? 1.f : 0.45f);

  const bool hCent = point_in_rect(app.pointerX, pyH, monLay.center_btn_x, monLay.aux_btn_y,
                                    monLay.aux_btn_w, monLay.aux_btn_h);
  const bool hAlign = point_in_rect(app.pointerX, pyH, monLay.align_top_btn_x, monLay.aux_btn_y,
                                    monLay.aux_btn_w, monLay.aux_btn_h);
  paint_mon_small_btn(monLay.center_btn_x, monLay.aux_btn_y, monLay.aux_btn_w, monLay.aux_btn_h,
                      "Center view", hCent, 1.f);
  paint_mon_small_btn(monLay.align_top_btn_x, monLay.aux_btn_y, monLay.aux_btn_w,
                      monLay.aux_btn_h, "Align top", hAlign, 1.f);

  if (!app.monitorsTab.status.empty()) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    const double metaY0 = static_cast<double>(monLay.toolbar_y + monLay.toolbar_btn_h + 10);
    settings_draw_trimmed_text_line(cr, app.monitorsTab.status,
                                    static_cast<double>(contentX) + kCardPad, metaY0, 96,
                                    0.82 * glassOv, 11.f, 400);
  }

  {
    m3::Box box;
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
    box.setColor(r, g, b, static_cast<float>(0.55 * glassOv));
    box.setRadius(10.f);
    box.setGeometry(static_cast<float>(monLay.canvas_x), static_cast<float>(monLay.canvas_y),
                    static_cast<float>(monLay.canvas_w), static_cast<float>(monLay.canvas_h));
    box.paint(cr);
  }
  cairo_round_rect(cr, static_cast<double>(monLay.canvas_x),
                   static_cast<double>(monLay.canvas_y),
                   static_cast<double>(monLay.canvas_w),
                   static_cast<double>(monLay.canvas_h), 10.0);
  paint_src_glass_hi(app, cr, 0.10 * glassOv);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const int nMon = static_cast<int>(app.monitorsTab.outputs.size());
  cairo_save(cr);
  cairo_round_rect(cr, static_cast<double>(monLay.canvas_x),
                   static_cast<double>(monLay.canvas_y),
                   static_cast<double>(monLay.canvas_w),
                   static_cast<double>(monLay.canvas_h), 10.0);
  cairo_clip(cr);

  for (int ii = 0; ii < nMon; ++ii) {
    double sx = 0, sy = 0, sw = 0, sh = 0;
    if (!monitors_canvas_screen_rect(app, monLay, static_cast<size_t>(ii), &sx, &sy, &sw, &sh))
      continue;
    const auto& row = app.monitorsTab.outputs[static_cast<size_t>(ii)];
    const bool sel = ii == app.monitorsSelectedIdx;
    {
      m3::Box box;
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
      box.setColor(hr, hg, hb, static_cast<float>(ha));
      box.setRadius(6.f);
      box.setGeometry(sx, sy, sw, sh);
      box.paint(cr);
    }
    cairo_round_rect(cr, sx, sy, sw, sh, 6.0);
    cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB,
                          sel ? 0.55 * glassOv : 0.22 * glassOv);
    cairo_set_line_width(cr, sel ? 2.0 : 1.0);
    cairo_stroke(cr);

    settings_show_text(cr, sx + 8, sy + 18, row.name.c_str(), 11, 700, Theme::TextR, Theme::TextG, Theme::TextB,
                          row.disabled ? 0.35f * static_cast<float>(glassOv) : 0.9f * static_cast<float>(glassOv));

    auto cit = app.monitorsTab.caps.find(row.name);
    const eh::settings_monitors::OutputCaps* ocaps =
        cit != app.monitorsTab.caps.end() ? &cit->second : nullptr;
    std::string badges;
    if (ocaps && ocaps->hdr_hint) badges += "HDR ";
    if (!row.bitdepth.empty()) badges += "10b ";
    const int vv = row.vrr.empty() ? 0 : std::atoi(row.vrr.c_str());
    if (vv > 0) badges += "VRR ";
    if (!badges.empty()) {
      settings_show_text(cr, sx + 8, sy + sh - 8, badges.c_str(), 9, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
    }
  }
  cairo_restore(cr);

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
        m3::Box box;
        float hr, hg, hb;
        if (app.drawChromeMatugen) {
          hr = 0.48f + 0.52f * app.drawChrome.outlineR;
          hg = 0.48f + 0.52f * app.drawChrome.outlineG;
          hb = 0.48f + 0.52f * app.drawChrome.outlineB;
        } else {
          hr = 1.f; hg = 1.f; hb = 1.f;
        }
        box.setColor(hr, hg, hb, static_cast<float>((psel ? 0.22 : 0.12) + (ph ? 0.06 : 0.0)));
        box.setRadius(static_cast<float>(kMonPillH) * 0.45f);
        box.setGeometry(static_cast<float>(pillX), static_cast<float>(pillY),
                        static_cast<float>(pillW), static_cast<float>(kMonPillH));
        box.paint(cr);
      }
      settings_show_text(cr, pillX + 12, pillY + 19, nm.c_str(), 11, psel ? 700 : 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
      pillX += pillW + 8;
    }
  }

  // Form cards
  if (!app.monitorsTab.outputs.empty()) {
    monitors_clamp_selected(app);
    const auto& srow = app.monitorsTab.outputs[static_cast<size_t>(app.monitorsSelectedIdx)];
    auto scit = app.monitorsTab.caps.find(srow.name);
    const eh::settings_monitors::OutputCaps* scaps =
        scit != app.monitorsTab.caps.end() ? &scit->second : nullptr;
    const MonitorsFormRows fr = monitors_form_rows(app, srow, scaps);
    const MonitorsFormGeom fg = monitors_form_layout(monLay, static_cast<int>(contentX), contentW,
                                                     fr);

    auto paint_sec_heading = [&](const MonitorsSectionGeom& sec, const char* title) {
      settings_show_text(cr, static_cast<double>(sec.x + kCardPad),
                    static_cast<double>(sec.y + kMonSecTitlePadTop + 14), title, 12, 700, Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
    };

    auto paint_mon_combo_at_row = [&](const MonitorsSectionGeom& sec, int rowIx,
                                      const char* label, const char* valueText, bool expanded) {
      const int labY = sec.content_y0 + rowIx * kMonFormRowPitch;
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      settings_draw_trimmed_text_line(cr, std::string(label), sec.x + kCardPad,
                                     static_cast<double>(labY + 19), 22, 0.82 * glassOv, 12.f, 400);
      int cx = 0, cy = 0, cw = 0, ch = 0;
      monitors_combo_geom_at_content_row(sec.content_y0, sec.x, sec.w, rowIx, &cx, &cy, &cw, &ch);
      const bool hovered =
          app.pointerX >= cx && pyH >= cy && app.pointerX < cx + cw && pyH < cy + ch;
      {
        m3::Box box;
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
        box.setColor(r, g, b, static_cast<float>((hovered ? 0.94 : 0.88) * glassOv));
        box.setRadius(9.f);
        box.setGeometry(cx, cy, cw, ch);
        box.paint(cr);
      }
      cairo_round_rect(cr, cx, cy, cw, ch, 9.0);
      paint_src_glass_hi(app, cr, 0.11 * glassOv);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      const size_t valChars = static_cast<size_t>(std::clamp((cw - 40) / 7, 16, 52));
      settings_draw_trimmed_text_line(cr, std::string(valueText), cx + 12.0, cy + 19.0, valChars,
                                     0.88 * glassOv, 12.f, 400);
      material_symbols_draw_glyph(cr, cx + cw - 12.0, cy + 14.0, 12.0,
          expanded ? "arrow_drop_up" : "arrow_drop_down",
          Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
    };

    // Header card
    settings_card(app, cr, static_cast<double>(fg.header.x), static_cast<double>(fg.header.y),
                  static_cast<double>(fg.header.w), static_cast<double>(fg.header.h), glassOv);
    settings_show_text(cr, fg.header.x + kCardPad, fg.header.y + 32, srow.name.c_str(), 13, 700, Theme::TextR, Theme::TextG, Theme::TextB, 1.0 * glassOv);
    settings_toggle(app, cr, fg.header.x, fg.header.y + 13, fg.header.w,
                    static_cast<double>(fg.header.y), 52.0, srow.disabled, dockMatA);

    const bool dimForm = srow.disabled;

    // Display section
    settings_card(app, cr, static_cast<double>(fg.display.x),
                  static_cast<double>(fg.display.y), static_cast<double>(fg.display.w),
                  static_cast<double>(fg.display.h), glassOv);
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

    // Scale & transform section
    settings_card(app, cr, static_cast<double>(fg.scale.x), static_cast<double>(fg.scale.y),
                  static_cast<double>(fg.scale.w), static_cast<double>(fg.scale.h), glassOv);
    paint_sec_heading(fg.scale, "Scale & transform");
    if (!dimForm) {
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      const int scaleLabY = fg.scale.content_y0 + fr.s_scale * kMonFormRowPitch;
      settings_draw_trimmed_text_line(cr, std::string("Scale"), fg.scale.x + kCardPad,
                                     static_cast<double>(scaleLabY + 19), 22, 0.82 * glassOv, 12.f, 400);
      int trXf = 0, trYf = 0, trWf = 0;
      monitors_form_slider_track_geom_content(fg.scale.content_y0, fg.scale.x, fg.scale.w,
                                              fr.s_scale, &trXf, &trYf, &trWf);
      char scaleBuf[48];
      std::snprintf(scaleBuf, sizeof(scaleBuf), "%s\u00d7", srow.scale.c_str());
      const double scDn =
          (app.monitorsScaleSliderDragIdx >= 0) ? app.settingsSliderDragNormT : -1.0;
      settings_slider(app, cr, trXf, trYf, trWf, monitors_form_scale_ticks(srow), 100, 200,
                      paintPointerYOffset, scaleBuf, false, scDn);

      const int ti = std::clamp(std::atoi(srow.transform.c_str()), 0, 7);
      paint_mon_combo_at_row(fg.scale, fr.s_tf, "Transform",
                             kMonitorTfLabels[static_cast<size_t>(ti)],
                             app.monitorsActiveDd == 2);
    }

    // Color section
    if (fg.has_color) {
      settings_card(app, cr, static_cast<double>(fg.color.x), static_cast<double>(fg.color.y),
                    static_cast<double>(fg.color.w), static_cast<double>(fg.color.h), glassOv);
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
          settings_draw_trimmed_text_line(cr, std::string("Color profile"), fg.color.x + kCardPad,
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
            auto* layout = pango_cairo_create_layout(cr);
            auto* desc = pango_font_description_new();
            pango_font_description_set_family(desc, "Inter");
            pango_font_description_set_size(desc, static_cast<int>(11.0f * PANGO_SCALE));
            pango_layout_set_font_description(layout, desc);
            pango_layout_set_text(layout, pathDisp.c_str(), -1);
            int tw, th;
            pango_layout_get_pixel_size(layout, &tw, &th);
            cairo_save(cr);
            cairo_translate(cr, static_cast<double>(valX + 8),
                            static_cast<double>(elY + (kSettingsComboH - th) / 2));
            cairo_set_source_rgba(cr, t_r, t_g, t_b, hasIcc ? 1.0f : 0.45f);
            pango_cairo_show_layout(cr, layout);
            cairo_restore(cr);
            pango_font_description_free(desc);
            g_object_unref(layout);
          }
          const int browseX = valX + pathW + kGap;
          const bool hBrowse = point_in_rect(app.pointerX, pyH, browseX, elY, browseW, kSettingsComboH);
          paint_mon_small_btn(browseX, elY, browseW, kSettingsComboH, "Browse", hBrowse, 1.f);
          if (hasIcc) {
            const int clearX = browseX + browseW + kGap;
            const bool hClear = point_in_rect(app.pointerX, pyH, clearX, elY, clearW, kSettingsComboH);
            paint_mon_small_btn(clearX, elY, clearW, kSettingsComboH, "Clear", hClear, 1.f);
          }
        }
      }
    }

    // HDR section
    if (fg.has_hdr) {
      settings_card(app, cr, static_cast<double>(fg.hdr.x), static_cast<double>(fg.hdr.y),
                    static_cast<double>(fg.hdr.w), static_cast<double>(fg.hdr.h), glassOv);
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
            settings_draw_trimmed_text_line(cr, std::string(label), sec.x + kCardPad,
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
            settings_slider(app, cr, trx, trey, trw, sv, vmin, vmax, paintPointerYOffset, disp,
                            false, hyDn);
          };
          paint_mon_hypr_slider_at_row(fg.hdr, fr.h_sdr_b, "SDR brightness", 0);
          paint_mon_hypr_slider_at_row(fg.hdr, fr.h_sdr_s, "SDR saturation", 1);
        }
      }

      // Luminance section
      if (fg.has_luminance) {
        settings_card(app, cr, static_cast<double>(fg.luminance.x),
                      static_cast<double>(fg.luminance.y), static_cast<double>(fg.luminance.w),
                      static_cast<double>(fg.luminance.h), glassOv);
        paint_sec_heading(fg.luminance, "Luminance");
        if (!dimForm) {
          auto paint_mon_hypr_slider_at_row = [&](const MonitorsSectionGeom& sec, int rowIx,
                                                  const char* label, int kind) {
            const int labY = sec.content_y0 + rowIx * kMonFormRowPitch;
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 12);
            settings_draw_trimmed_text_line(cr, std::string(label), sec.x + kCardPad,
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
            settings_slider(app, cr, trx, trey, trw, sv, vmin, vmax, paintPointerYOffset, disp,
                            false, hyDn2);
          };
          paint_mon_hypr_slider_at_row(fg.luminance, fr.l_sdr_min, "SDR min luminance", 2);
          paint_mon_hypr_slider_at_row(fg.luminance, fr.l_sdr_max, "SDR max luminance", 3);
          paint_mon_hypr_slider_at_row(fg.luminance, fr.l_min, "Min luminance (HDR)", 4);
          paint_mon_hypr_slider_at_row(fg.luminance, fr.l_max, "Max luminance", 5);
          paint_mon_hypr_slider_at_row(fg.luminance, fr.l_avg, "Max average luminance", 6);
        }
      }
    }
  }

}
