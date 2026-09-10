#include "dialog/file_chooser_dialog.hpp"

static int monitors_section_card_height_rows(int n_rows) {
  return kMonSecTitlePadTop + kMonSecTitleLineH + kMonSecAfterTitle + n_rows * kMonFormRowPitch + kMonSecBottomPad;
}

static int monitors_section_content_y0(int card_y) {
  return card_y + kMonSecTitlePadTop + kMonSecTitleLineH + kMonSecAfterTitle;
}

static MonitorsFormGeom monitors_form_layout(const eh::settings_monitors_tab::MonitorsTabLayout& lay, int contentX,
                                             int contentW, const MonitorsFormRows& fr) {
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

static bool monitors_hdr_panel_visible(const App& app, const eh::settings_monitors::MonitorRow& row,
                                         const eh::settings_monitors::OutputCaps* caps) {
  if (!app.monitorsTab.supports_color_hdr_ui()) return false;
  if (row.disabled) return false;
  if (caps && caps->hdr_hint) return true;
  if (row.cm == "hdr" || row.cm == "hdredid") return true;
  if (row.supports_hdr == "1") return true;
  return false;
}

static bool monitors_wide_eotf_strip_visible(const App& app, const eh::settings_monitors::MonitorRow& row,
                                             const eh::settings_monitors::OutputCaps* caps) {
  if (!app.monitorsTab.supports_color_hdr_ui()) return false;
  if (row.disabled) return false;
  if (monitors_hdr_panel_visible(app, row, caps)) return false;
  std::string cm = row.cm;
  for (char& c : cm) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return cm == "srgb";
}

static MonitorsFormRows monitors_form_rows(const App& app, const eh::settings_monitors::MonitorRow& row,
                                           const eh::settings_monitors::OutputCaps* caps) {
  MonitorsFormRows fr{};
  fr.show_vrr = app.monitorsTab.kind == CompositorKind::Hyprland || app.monitorsTab.kind == CompositorKind::Mango ||
                (app.monitorsTab.kind == CompositorKind::Niri && caps && caps->vrr_capable);
  fr.d_res = 0;
  fr.d_hz = 1;
  fr.d_vrr = fr.show_vrr ? 2 : -1;

  fr.s_scale = 0;
  fr.s_tf = 1;

  int cr = 0;
  const bool show_bit = app.monitorsTab.supports_bitdepth_ui() && app.monitorsTab.kind != CompositorKind::Mango;
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

static void monitors_combo_geom_at_content_row(int content_y0, int cardX, int cardW, int rowIx, int* cx, int* cy,
                                               int* cw, int* ch) {
  const int y = content_y0 + rowIx * kMonFormRowPitch;
  *cx = cardX + cardW - kCardPad - kMonFormValRailPx - kSettingsComboW;
  *cy = y;
  *cw = kSettingsComboW;
  *ch = kSettingsComboH;
}

static void monitors_form_slider_track_geom_content(int content_y0, int cardX, int cardW, int rowIx, int* trX,
                                                    int* trY, int* trW) {
  constexpr int kValPx = 56;
  const int right = cardX + cardW - kCardPad;
  const int minX = cardX + kMonFormLabelColW + 12;
  *trW = std::max(96, right - kValPx - minX);
  *trX = right - kValPx - *trW;
  *trY = content_y0 + rowIx * kMonFormRowPitch + 6;
}

static bool monitors_dd_combo_geom(const MonitorsFormRows& fr, const MonitorsFormGeom& g, int ddKind, int* cx, int* cy,
                                   int* cw, int* ch) {
  int rowIx = -1;
  const MonitorsSectionGeom* sec = nullptr;
  if (ddKind == 0) {
    sec = &g.display;
    rowIx = fr.d_res;
  } else if (ddKind == 1) {
    sec = &g.display;
    rowIx = fr.d_hz;
  } else if (ddKind == 2) {
    sec = &g.scale;
    rowIx = fr.s_tf;
  } else if (ddKind == 3) {
    if (!g.has_color || fr.c_bit < 0) return false;
    sec = &g.color;
    rowIx = fr.c_bit;
  } else if (ddKind == 4) {
    if (!fr.show_vrr || fr.d_vrr < 0) return false;
    sec = &g.display;
    rowIx = fr.d_vrr;
  } else if (ddKind == 5) {
    if (!g.has_color || fr.c_cm < 0) return false;
    sec = &g.color;
    rowIx = fr.c_cm;
  } else if (ddKind == 6) {
    if (!g.has_hdr || fr.h_hdr_support < 0) return false;
    sec = &g.hdr;
    rowIx = fr.h_hdr_support;
  } else if (ddKind == 7) {
    if (!g.has_hdr) return false;
    sec = &g.hdr;
    rowIx = fr.h_wide;
  } else if (ddKind == 8) {
    if (!g.has_hdr) return false;
    sec = &g.hdr;
    rowIx = fr.h_eotf;
  }
  if (rowIx < 0 || !sec) return false;
  monitors_combo_geom_at_content_row(sec->content_y0, sec->x, sec->w, rowIx, cx, cy, cw, ch);
  return true;
}

static int monitors_dd_popup_list_doc_top_y(int combo_doc_y, int combo_h, int nrows, int scroll_px, int window_h) {
  if (nrows <= 0) return combo_doc_y + combo_h + 2;
  const int default_top = combo_doc_y + combo_h + 2;
  const int popup_h = nrows * kSettingsDdRowH;
  const int viewport_bottom_doc = window_h - kSpacingL + scroll_px;
  if (default_top + popup_h <= viewport_bottom_doc) return default_top;
  const int above_top = combo_doc_y - popup_h - 2;
  if (above_top >= kContentTop) return above_top;
  return default_top;
}

static void monitors_clamp_selected(App& app) {
  if (app.monitorsTab.outputs.empty()) {
    app.monitorsSelectedIdx = 0;
    return;
  }
  app.monitorsSelectedIdx =
      std::clamp(app.monitorsSelectedIdx, 0, static_cast<int>(app.monitorsTab.outputs.size()) - 1);
}

static int settings_monitors_scroll_max_px(App& app) {
  const int contentX = kSpacingL + kSidebarW + kSpacingL;
  const int contentW = app.width - contentX - kSpacingL;
  eh::settings_monitors_tab::MonitorsTabLayout lay{};
  eh::settings_monitors_tab::compute_monitors_tab_layout(contentX, contentW, kContentTop, settings_content_viewport_h(app),
                                                         &lay);
  const eh::settings_monitors::OutputCaps* caps = nullptr;
  const eh::settings_monitors::MonitorRow* rowPtr = nullptr;
  if (!app.monitorsTab.outputs.empty()) {
    rowPtr = &app.monitorsTab.outputs[static_cast<size_t>(app.monitorsSelectedIdx)];
    auto cit = app.monitorsTab.caps.find(rowPtr->name);
    if (cit != app.monitorsTab.caps.end()) caps = &cit->second;
  }
  static const eh::settings_monitors::MonitorRow kEmptyRow{};
  const MonitorsFormRows fr = monitors_form_rows(app, rowPtr ? *rowPtr : kEmptyRow, caps);
  const MonitorsFormGeom fg = monitors_form_layout(lay, contentX, contentW, fr);
  const int extent = fg.bottom_y + 32 - kContentTop;
  const int viewH = app.height - kContentTop - kSpacingL;
  return std::max(0, extent - viewH);
}

inline void settings_clamp_monitors_scroll_px(App& app) {
  app.settingsMonitorsScrollPx = std::clamp(app.settingsMonitorsScrollPx, 0, settings_monitors_scroll_max_px(app));
  settings_scroll_sync_after_clamp(app);
}

static void monitors_form_apply_scale_ticks(eh::settings_monitors::MonitorRow& row, int ticks) {
  ticks = std::clamp(ticks, 100, 200);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(ticks) / 100.0);
  row.scale = buf;
}

static void monitors_center_canvas_view(App& app, const eh::settings_monitors_tab::MonitorsTabLayout& lay) {
  std::vector<eh::settings_monitors_tab::ArrangeRect> rects;
  eh::settings_monitors_tab::layout_rects_from_outputs(app.monitorsTab.outputs, app.monitorsTab.caps, &rects);
  double min_x = 0, min_y = 0, span_w = 1, span_h = 1, fit_scale = 1;
  eh::settings_monitors_tab::compute_canvas_fit(rects, lay.canvas_w, lay.canvas_h, 0.1, &min_x, &min_y, &span_w,
                                                &span_h, &fit_scale);
  const double sf = fit_scale * app.monitorsCanvasZoom;
  app.monitorsCanvasPanX = lay.canvas_w * 0.5 - (min_x + span_w * 0.5) * sf;
  app.monitorsCanvasPanY = lay.canvas_h * 0.5 - (min_y + span_h * 0.5) * sf;
}

static void monitors_canvas_transform(const App& app, const eh::settings_monitors_tab::MonitorsTabLayout& lay,
                                      double* min_x, double* min_y, double* sf_out) {
  std::vector<eh::settings_monitors_tab::ArrangeRect> rects;
  eh::settings_monitors_tab::layout_rects_from_outputs(app.monitorsTab.outputs, app.monitorsTab.caps, &rects);
  double span_w = 1, span_h = 1, fit_scale = 1;
  eh::settings_monitors_tab::compute_canvas_fit(rects, lay.canvas_w, lay.canvas_h, 0.1, min_x, min_y, &span_w, &span_h,
                                                &fit_scale);
  *sf_out = fit_scale * app.monitorsCanvasZoom;
}

static bool monitors_canvas_screen_rect(const App& app, const eh::settings_monitors_tab::MonitorsTabLayout& lay,
                                        size_t idx, double* sx, double* sy, double* sw, double* sh) {
  std::vector<eh::settings_monitors_tab::ArrangeRect> rects;
  eh::settings_monitors_tab::layout_rects_from_outputs(app.monitorsTab.outputs, app.monitorsTab.caps, &rects);
  if (idx >= rects.size()) return false;
  const auto& r = rects[idx];
  if (app.monitorsTab.outputs[idx].disabled || r.w <= 0) return false;
  double min_x = 0, min_y = 0, sf = 1;
  monitors_canvas_transform(app, lay, &min_x, &min_y, &sf);
  *sx = static_cast<double>(lay.canvas_x) + app.monitorsCanvasPanX + (static_cast<double>(r.x) - min_x) * sf;
  *sy = static_cast<double>(lay.canvas_y) + app.monitorsCanvasPanY + (static_cast<double>(r.y) - min_y) * sf;
  *sw = static_cast<double>(r.w) * sf;
  *sh = static_cast<double>(r.h) * sf;
  return *sw > 1 && *sh > 1;
}

static int monitors_canvas_hit_monitor(App& app, const eh::settings_monitors_tab::MonitorsTabLayout& lay, double px,
                                       double pyLogical) {
  if (px < lay.canvas_x || pyLogical < lay.canvas_y || px >= lay.canvas_x + lay.canvas_w ||
      pyLogical >= lay.canvas_y + lay.canvas_h)
    return -1;
  for (int i = static_cast<int>(app.monitorsTab.outputs.size()) - 1; i >= 0; --i) {
    double sx = 0, sy = 0, sw = 0, sh = 0;
    if (!monitors_canvas_screen_rect(app, lay, static_cast<size_t>(i), &sx, &sy, &sw, &sh)) continue;
    if (px >= sx && px < sx + sw && pyLogical >= sy && pyLogical < sy + sh) return i;
  }
  return -1;
}

static void monitors_form_scale_track_geom(App& app, const eh::settings_monitors_tab::MonitorsTabLayout& lay,
                                           int contentX, int contentW, int* trX, int* trY, int* trW) {
  monitors_clamp_selected(app);
  const size_t ix = static_cast<size_t>(app.monitorsSelectedIdx);
  if (ix >= app.monitorsTab.outputs.size()) {
    *trX = *trY = *trW = 0;
    return;
  }
  const auto& row = app.monitorsTab.outputs[ix];
  auto cit = app.monitorsTab.caps.find(row.name);
  const eh::settings_monitors::OutputCaps* caps = cit != app.monitorsTab.caps.end() ? &cit->second : nullptr;
  const MonitorsFormRows fr = monitors_form_rows(app, row, caps);
  const MonitorsFormGeom g = monitors_form_layout(lay, contentX, contentW, fr);
  monitors_form_slider_track_geom_content(g.scale.content_y0, g.scale.x, g.scale.w, fr.s_scale, trX, trY, trW);
}

static void monitors_apply_form_scale_drag(App& app, double pointerX,
                                           const eh::settings_monitors_tab::MonitorsTabLayout& lay, int contentX,
                                           int contentW) {
  monitors_clamp_selected(app);
  const size_t ix = static_cast<size_t>(app.monitorsSelectedIdx);
  if (ix >= app.monitorsTab.outputs.size()) return;
  int trX = 0, trY = 0, trW = 0;
  monitors_form_scale_track_geom(app, lay, contentX, contentW, &trX, &trY, &trW);
  const double t = (pointerX - static_cast<double>(trX)) / static_cast<double>(std::max(1, trW));
  app.settingsSliderDragNormT = std::clamp(t, 0.0, 1.0);
  const int ticks = static_cast<int>(std::lround(app.settingsSliderDragNormT * 100.0)) + 100;
  monitors_form_apply_scale_ticks(app.monitorsTab.outputs[ix], ticks);
  app.monitorsTab.dirty = true;
}

static void monitors_pick_resolution_ix(App& app, size_t idx, int ix) {
  if (idx >= app.monitorsTab.outputs.size()) return;
  auto& row = app.monitorsTab.outputs[idx];
  auto cit = app.monitorsTab.caps.find(row.name);
  if (cit == app.monitorsTab.caps.end()) return;
  const auto& res = cit->second.resolutions;
  if (ix < 0 || ix >= static_cast<int>(res.size())) return;
  row.resolution = res[static_cast<size_t>(ix)];
  auto hzIt = cit->second.resolution_refresh_hz.find(row.resolution);
  if (hzIt != cit->second.resolution_refresh_hz.end() && !hzIt->second.empty()) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", hzIt->second.front());
    row.refresh_rate = buf;
  }
  app.monitorsTab.dirty = true;
}

static void monitors_pick_refresh_ix(App& app, size_t idx, int ix) {
  if (idx >= app.monitorsTab.outputs.size()) return;
  auto& row = app.monitorsTab.outputs[idx];
  auto cit = app.monitorsTab.caps.find(row.name);
  if (cit == app.monitorsTab.caps.end()) return;
  auto hzIt = cit->second.resolution_refresh_hz.find(row.resolution);
  if (hzIt == cit->second.resolution_refresh_hz.end()) return;
  const auto& hzlist = hzIt->second;
  if (ix < 0 || ix >= static_cast<int>(hzlist.size())) return;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%g", hzlist[static_cast<size_t>(ix)]);
  row.refresh_rate = buf;
  app.monitorsTab.dirty = true;
}

static void monitors_set_transform_ix(App& app, size_t idx, int t) {
  if (idx >= app.monitorsTab.outputs.size()) return;
  t = std::clamp(t, 0, 7);
  app.monitorsTab.outputs[idx].transform = std::to_string(t);
  app.monitorsTab.dirty = true;
}

static void monitors_set_vrr_ix(App& app, size_t idx, int v) {
  if (idx >= app.monitorsTab.outputs.size()) return;
  auto& row = app.monitorsTab.outputs[idx];
  if (app.monitorsTab.kind == CompositorKind::Mango) {
    v = std::clamp(v, 0, 1);
  } else {
    v = std::clamp(v, 0, 2);
  }
  row.vrr = std::to_string(v);
  app.monitorsTab.dirty = true;
}

static void monitors_set_bitdepth_ix(App& app, size_t idx, int opt) {
  if (idx >= app.monitorsTab.outputs.size()) return;
  app.monitorsTab.outputs[idx].bitdepth = (opt == 1) ? "10" : "";
  app.monitorsTab.dirty = true;
}

static void monitors_set_cm_ix(App& app, size_t idx, int opt) {
  if (idx >= app.monitorsTab.outputs.size()) return;
  opt = std::clamp(opt, 0, kMonitorCmCount - 1);
  app.monitorsTab.outputs[idx].cm = kMonitorCmLabels[opt];
  app.monitorsTab.dirty = true;
}

inline int monitors_cm_index(const std::string& cm) {
  std::string lc = cm;
  for (char& c : lc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (int i = 0; i < kMonitorCmCount; ++i) {
    if (lc == kMonitorCmLabels[i]) return i;
  }
  return 0;
}

inline int monitors_tri_auto_field_sel(const std::string& s) {
  if (s.empty() || s == "0") return 0;
  if (s == "1") return 1;
  return 2;
}

static void monitors_set_tri_auto_field(std::string* out, int ix) {
  ix = std::clamp(ix, 0, 2);
  if (ix == 0)
    *out = "";
  else if (ix == 1)
    *out = "1";
  else
    *out = "-1";
}

inline int monitors_sdr_eotf_sel(const std::string& s) {
  if (s.empty()) return 0;
  return std::clamp(std::atoi(s.c_str()), 0, 2);
}

static void monitors_set_sdr_eotf_ix(App& app, size_t idx, int ix) {
  if (idx >= app.monitorsTab.outputs.size()) return;
  ix = std::clamp(ix, 0, 2);
  app.monitorsTab.outputs[idx].sdr_eotf = (ix == 0) ? "" : std::to_string(ix);
  app.monitorsTab.dirty = true;
}

static void monitors_apply_hypr_extra_drag(App& app, eh::settings_monitors::MonitorRow* row, int kind, double pointerX,
                                             int trX, int trW) {
  if (!row) return;
  const double t =
      (pointerX - static_cast<double>(trX)) / static_cast<double>(std::max(1, trW));
  const double tf = std::clamp(t, 0.0, 1.0);
  app.settingsSliderDragNormT = tf;
  char buf[64];
  switch (kind) {
    case 0: {
      const int v = static_cast<int>(std::lround(tf * 190.0)) + 10;
      std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v) / 100.0);
      row->sdrbrightness = buf;
      break;
    }
    case 1: {
      const int v = static_cast<int>(std::lround(tf * 200.0));
      std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v) / 100.0);
      row->sdrsaturation = buf;
      break;
    }
    case 2: {
      const int v = static_cast<int>(std::lround(tf * 100.0));
      std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v) / 10000.0);
      row->sdr_min_luminance = buf;
      break;
    }
    case 3: {
      const int v = static_cast<int>(std::lround(tf * 320.0)) + 80;
      std::snprintf(buf, sizeof(buf), "%d", v);
      row->sdr_max_luminance = buf;
      break;
    }
    case 4: {
      const int v = static_cast<int>(std::lround(tf * 100.0));
      std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v) / 10000.0);
      row->min_luminance = buf;
      break;
    }
    case 5: {
      const int v = static_cast<int>(std::lround(tf * 2000.0));
      std::snprintf(buf, sizeof(buf), "%d", v);
      row->max_luminance = buf;
      break;
    }
    case 6: {
      const int v = static_cast<int>(std::lround(tf * 2000.0));
      std::snprintf(buf, sizeof(buf), "%d", v);
      row->max_avg_luminance = buf;
      break;
    }
    default:
      break;
  }
}

inline void monitors_wheel_zoom_canvas(App& app, const eh::settings_monitors_tab::MonitorsTabLayout& lay,
                                       double delta_px) {
  double min_x = 0, min_y = 0, sf_old = 1;
  monitors_canvas_transform(app, lay, &min_x, &min_y, &sf_old);
  const double fit = sf_old / std::max(app.monitorsCanvasZoom, 1e-9);
  const int step = static_cast<int>(std::lround(delta_px));
  if (step == 0) return;
  const double zmul = (step > 0) ? 0.92 : (1.0 / 0.92);
  const double znew = std::clamp(app.monitorsCanvasZoom * zmul, 0.35, 4.0);
  const double px = app.pointerX - static_cast<double>(lay.canvas_x);
  const double py =
      app.pointerY + settings_scroll_px(app) - static_cast<double>(lay.canvas_y);
  const double wx = (px - app.monitorsCanvasPanX) / std::max(sf_old, 1e-9);
  const double wy = (py - app.monitorsCanvasPanY) / std::max(sf_old, 1e-9);
  app.monitorsCanvasZoom = znew;
  const double sf_new = fit * znew;
  app.monitorsCanvasPanX = px - wx * sf_new;
  app.monitorsCanvasPanY = py - wy * sf_new;
}

inline bool settings_monitors_consume_pointer_down(App& app, int contentX, int contentW) {
  const double pyLogical = app.pointerY + settings_scroll_px(app);
  eh::settings_monitors_tab::MonitorsTabLayout lay{};
  eh::settings_monitors_tab::compute_monitors_tab_layout(contentX, contentW, kContentTop, settings_content_viewport_h(app),
                                                         &lay);

  if (point_in_rect(app.pointerX, pyLogical, lay.toolbar_refresh_x, lay.toolbar_y, lay.toolbar_btn_w,
                    lay.toolbar_btn_h)) {
    app.monitorsCanvasLayoutReady = false;
    app.monitorsCanvasZoom = 1.0;
    app.monitorsTab.refresh_from_system();
    monitors_clamp_selected(app);
    settings_clamp_monitors_scroll_px(app);
    draw(app);
    return true;
  }
  const bool dirty = app.monitorsTab.dirty;
  if (point_in_rect(app.pointerX, pyLogical, lay.toolbar_apply_x, lay.toolbar_y, lay.toolbar_btn_w,
                    lay.toolbar_btn_h)) {
    if (dirty) {
      std::string err;
      if (!app.monitorsTab.save_and_reload(err) && !err.empty()) app.monitorsTab.status = err;
    }
    settings_clamp_monitors_scroll_px(app);
    draw(app);
    return true;
  }
  if (point_in_rect(app.pointerX, pyLogical, lay.toolbar_revert_x, lay.toolbar_y, lay.toolbar_btn_w,
                    lay.toolbar_btn_h)) {
    if (dirty) app.monitorsTab.revert_edits();
    monitors_clamp_selected(app);
    settings_clamp_monitors_scroll_px(app);
    draw(app);
    return true;
  }

  if (point_in_rect(app.pointerX, pyLogical, lay.center_btn_x, lay.aux_btn_y, lay.aux_btn_w, lay.aux_btn_h)) {
    monitors_center_canvas_view(app, lay);
    draw(app);
    return true;
  }
  if (point_in_rect(app.pointerX, pyLogical, lay.align_top_btn_x, lay.aux_btn_y, lay.aux_btn_w, lay.aux_btn_h)) {
    eh::settings_monitors_tab::align_all_tops(app.monitorsTab.outputs, app.monitorsTab.caps);
    app.monitorsTab.dirty = true;
    draw(app);
    return true;
  }

  if (app.monitorsActiveDd >= 0) {
    int cx = 0, cy = 0, cw = 0, ch = 0;
    const int si = app.monitorsSelectedIdx;
    const size_t su = static_cast<size_t>(si);
    if (su >= app.monitorsTab.outputs.size()) {
      app.monitorsActiveDd = -1;
      draw(app);
      return true;
    }
    auto& row = app.monitorsTab.outputs[su];
    auto cit = app.monitorsTab.caps.find(row.name);
    const eh::settings_monitors::OutputCaps* caps = cit != app.monitorsTab.caps.end() ? &cit->second : nullptr;
    const MonitorsFormRows fr = monitors_form_rows(app, row, caps);
    const MonitorsFormGeom formG = monitors_form_layout(lay, contentX, contentW, fr);

    if (!monitors_dd_combo_geom(fr, formG, app.monitorsActiveDd, &cx, &cy, &cw, &ch)) {
      app.monitorsActiveDd = -1;
      draw(app);
      return true;
    }

    int nrows = 0;
    if (app.monitorsActiveDd == 0 && caps)
      nrows = static_cast<int>(caps->resolutions.size());
    else if (app.monitorsActiveDd == 1 && caps) {
      auto it = caps->resolution_refresh_hz.find(row.resolution);
      if (it != caps->resolution_refresh_hz.end()) nrows = static_cast<int>(it->second.size());
    } else if (app.monitorsActiveDd == 2)
      nrows = 8;
    else if (app.monitorsActiveDd == 3)
      nrows = 2;
    else if (app.monitorsActiveDd == 4)
      nrows = app.monitorsTab.kind == CompositorKind::Mango ? 2 : 3;
    else if (app.monitorsActiveDd == 5)
      nrows = kMonitorCmCount;
    else if (app.monitorsActiveDd == 6 || app.monitorsActiveDd == 7 || app.monitorsActiveDd == 8)
      nrows = 3;

    const int ly =
        monitors_dd_popup_list_doc_top_y(cy, ch, nrows, settings_scroll_px_int(app), app.height);

    if (nrows > 0 && point_in_rect(app.pointerX, pyLogical, cx, ly, cw, nrows * kSettingsDdRowH)) {
      const int rr = settings_mode_dd_pointer_row(app.pointerX, pyLogical, cx, ly, cw, kSettingsDdRowH, nrows);
      if (rr >= 0) {
        if (app.monitorsActiveDd == 0)
          monitors_pick_resolution_ix(app, su, rr);
        else if (app.monitorsActiveDd == 1)
          monitors_pick_refresh_ix(app, su, rr);
        else if (app.monitorsActiveDd == 2)
          monitors_set_transform_ix(app, su, rr);
        else if (app.monitorsActiveDd == 3)
          monitors_set_bitdepth_ix(app, su, rr);
        else if (app.monitorsActiveDd == 4)
          monitors_set_vrr_ix(app, su, rr);
        else if (app.monitorsActiveDd == 5)
          monitors_set_cm_ix(app, su, rr);
        else if (app.monitorsActiveDd == 6) {
          monitors_set_tri_auto_field(&row.supports_hdr, rr);
          app.monitorsTab.dirty = true;
        } else if (app.monitorsActiveDd == 7) {
          monitors_set_tri_auto_field(&row.supports_wide_color, rr);
          app.monitorsTab.dirty = true;
        } else if (app.monitorsActiveDd == 8)
          monitors_set_sdr_eotf_ix(app, su, rr);
      }
      app.monitorsActiveDd = -1;
      draw(app);
      return true;
    }
    app.monitorsActiveDd = -1;
    draw(app);
    return true;
  }

  const int hitMon = monitors_canvas_hit_monitor(app, lay, app.pointerX, pyLogical);
  if (hitMon >= 0) {
    app.monitorsSelectedIdx = hitMon;
    app.monitorsCanvasDragIdx = hitMon;
    int ax = 0, ay = 0;
    (void)eh::settings_monitors_tab::parse_position_xy(
        app.monitorsTab.outputs[static_cast<size_t>(hitMon)].position.empty()
            ? "0x0"
            : app.monitorsTab.outputs[static_cast<size_t>(hitMon)].position,
        &ax, &ay);
    app.monitorsCanvasDragAnchorX = ax;
    app.monitorsCanvasDragAnchorY = ay;
    app.monitorsCanvasPressLogicalX = app.pointerX;
    app.monitorsCanvasPressLogicalY = pyLogical;
    app.monitorsCanvasPanArmed = false;
    draw(app);
    return true;
  }

  if (point_in_rect(app.pointerX, pyLogical, lay.canvas_x, lay.canvas_y, lay.canvas_w, lay.canvas_h)) {
    app.monitorsCanvasPanArmed = true;
    app.monitorsCanvasPanGrabX = app.pointerX - app.monitorsCanvasPanX;
    app.monitorsCanvasPanGrabY = pyLogical - app.monitorsCanvasPanY;
    app.monitorsCanvasDragIdx = -1;
    return true;
  }

  const int pillY = lay.pills_y;
  int px = lay.canvas_x;
  for (size_t i = 0; i < app.monitorsTab.outputs.size(); ++i) {
    const std::string& nm = app.monitorsTab.outputs[i].name;
    const int pillW = static_cast<int>(nm.size()) * 8 + 24;
    if (point_in_rect(app.pointerX, pyLogical, px, pillY, pillW, kMonPillH)) {
      app.monitorsSelectedIdx = static_cast<int>(i);
      draw(app);
      return true;
    }
    px += pillW + 8;
  }

  if (!app.monitorsTab.outputs.empty()) {
    monitors_clamp_selected(app);
    const size_t si = static_cast<size_t>(app.monitorsSelectedIdx);
    auto& row = app.monitorsTab.outputs[si];
    auto cit = app.monitorsTab.caps.find(row.name);
    const eh::settings_monitors::OutputCaps* caps = cit != app.monitorsTab.caps.end() ? &cit->second : nullptr;
    const MonitorsFormRows fr = monitors_form_rows(app, row, caps);
    const MonitorsFormGeom g = monitors_form_layout(lay, contentX, contentW, fr);

    if (point_in_rect(app.pointerX, pyLogical, g.header.x, g.header.y, g.header.w, g.bottom_y - g.header.y)) {
      constexpr int swW = 52;
      constexpr int swH = 26;
      const int swX = g.header.x + g.header.w - swW - kSpacingXL;
      const int swY = g.header.y + 13;
      if (point_in_rect(app.pointerX, pyLogical, swX, swY, swW, swH)) {
        row.disabled = !row.disabled;
        app.monitorsTab.dirty = true;
        draw(app);
        return true;
      }

      int cx = 0, cy = 0, cw = 0, ch = 0;
      monitors_combo_geom_at_content_row(g.display.content_y0, g.display.x, g.display.w, fr.d_res, &cx, &cy, &cw, &ch);
      if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
        app.monitorsActiveDd = 0;
        app.monitorsDdHoverRow = -1;
        draw(app);
        return true;
      }
      monitors_combo_geom_at_content_row(g.display.content_y0, g.display.x, g.display.w, fr.d_hz, &cx, &cy, &cw, &ch);
      if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
        app.monitorsActiveDd = 1;
        app.monitorsDdHoverRow = -1;
        draw(app);
        return true;
      }
      if (fr.show_vrr && fr.d_vrr >= 0) {
        monitors_combo_geom_at_content_row(g.display.content_y0, g.display.x, g.display.w, fr.d_vrr, &cx, &cy, &cw, &ch);
        if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
          app.monitorsActiveDd = 4;
          app.monitorsDdHoverRow = -1;
          draw(app);
          return true;
        }
      }
      int trX = 0, trY = 0, trW = 0;
      monitors_form_scale_track_geom(app, lay, contentX, contentW, &trX, &trY, &trW);
      if (point_in_rect(app.pointerX, pyLogical, trX - 6, trY, trW + 12, 28)) {
        app.monitorsScaleSliderDragIdx = app.monitorsSelectedIdx;
        monitors_apply_form_scale_drag(app, app.pointerX, lay, contentX, contentW);
        draw(app);
        return true;
      }
      monitors_combo_geom_at_content_row(g.scale.content_y0, g.scale.x, g.scale.w, fr.s_tf, &cx, &cy, &cw, &ch);
      if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
        app.monitorsActiveDd = 2;
        app.monitorsDdHoverRow = -1;
        draw(app);
        return true;
      }
      if (g.has_color && fr.c_bit >= 0) {
        monitors_combo_geom_at_content_row(g.color.content_y0, g.color.x, g.color.w, fr.c_bit, &cx, &cy, &cw, &ch);
        if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
          app.monitorsActiveDd = 3;
          app.monitorsDdHoverRow = -1;
          draw(app);
          return true;
        }
      }
      if (g.has_color && fr.c_cm >= 0) {
        monitors_combo_geom_at_content_row(g.color.content_y0, g.color.x, g.color.w, fr.c_cm, &cx, &cy, &cw, &ch);
        if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
          app.monitorsActiveDd = 5;
          app.monitorsDdHoverRow = -1;
          draw(app);
          return true;
        }
      }
      if (g.has_color && fr.c_icc >= 0) {
        const int rowY = g.color.content_y0 + fr.c_icc * kMonFormRowPitch;
        const int elY = rowY + (kMonFormRowPitch - kSettingsComboH) / 2;
        const int valX = g.color.x + kMonFormLabelColW + kMonFormValRailPx;
        const int valW = g.color.w - kMonFormLabelColW - kMonFormValRailPx - kCardPad;
        constexpr int kBwBrowse = 80;
        constexpr int kBwClear = 64;
        constexpr int kGap = kSpacingM;
        const bool hasIcc = !row.icc.empty();
        const int browseW = kBwBrowse;
        const int clearW = hasIcc ? kBwClear : 0;
        const int clearGap = hasIcc ? kGap : 0;
        const int pathW = valW - browseW - clearW - kGap - clearGap;
        const int browseX = valX + pathW + kGap;
        if (point_in_rect(app.pointerX, pyLogical, browseX, elY, browseW, kSettingsComboH)) {
          std::string filePath;
          {
            using namespace eh::dialog;
            FileChooserDialog dlg(FileChooserDialog::Mode::Open,
                                  "Select ICC/ICM color profile",
                                  "", false);
            auto res = dlg.run();
            if (res.response == 0 && !res.uris.empty()) {
              filePath = file_uri_to_local_path(res.uris[0]);
            }
          }
          if (!filePath.empty()) {
            row.icc = filePath;
            app.monitorsTab.dirty = true;
          }
          draw(app);
          return true;
        }
        if (hasIcc) {
          const int clearX = browseX + browseW + kGap;
          if (point_in_rect(app.pointerX, pyLogical, clearX, elY, clearW, kSettingsComboH)) {
            row.icc.clear();
            app.monitorsTab.dirty = true;
            draw(app);
            return true;
          }
        }
      }

      if (g.has_hdr) {
        if (fr.h_hdr_support >= 0) {
          monitors_combo_geom_at_content_row(g.hdr.content_y0, g.hdr.x, g.hdr.w, fr.h_hdr_support, &cx, &cy, &cw, &ch);
          if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
            app.monitorsActiveDd = 6;
            app.monitorsDdHoverRow = -1;
            draw(app);
            return true;
          }
        }
        monitors_combo_geom_at_content_row(g.hdr.content_y0, g.hdr.x, g.hdr.w, fr.h_wide, &cx, &cy, &cw, &ch);
        if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
          app.monitorsActiveDd = 7;
          app.monitorsDdHoverRow = -1;
          draw(app);
          return true;
        }
        monitors_combo_geom_at_content_row(g.hdr.content_y0, g.hdr.x, g.hdr.w, fr.h_eotf, &cx, &cy, &cw, &ch);
        if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
          app.monitorsActiveDd = 8;
          app.monitorsDdHoverRow = -1;
          draw(app);
          return true;
        }
        const struct {
          const MonitorsSectionGeom* sec;
          int row;
          int kind;
        } hyprSliders[] = {{&g.hdr, fr.h_sdr_b, 0},
                           {&g.hdr, fr.h_sdr_s, 1},
                           {&g.luminance, fr.l_sdr_min, 2},
                           {&g.luminance, fr.l_sdr_max, 3},
                           {&g.luminance, fr.l_min, 4},
                           {&g.luminance, fr.l_max, 5},
                           {&g.luminance, fr.l_avg, 6}};
        for (const auto& hs : hyprSliders) {
          if (hs.row < 0) continue;
          monitors_form_slider_track_geom_content(hs.sec->content_y0, hs.sec->x, hs.sec->w, hs.row, &trX, &trY, &trW);
          if (point_in_rect(app.pointerX, pyLogical, trX - 6, trY, trW + 12, 28)) {
            app.monitorsHyprExtraSlider = hs.kind;
            monitors_apply_hypr_extra_drag(app, &row, hs.kind, app.pointerX, trX, trW);
            app.monitorsTab.dirty = true;
            draw(app);
            return true;
          }
        }
      }
    }
  }

  return false;
}
