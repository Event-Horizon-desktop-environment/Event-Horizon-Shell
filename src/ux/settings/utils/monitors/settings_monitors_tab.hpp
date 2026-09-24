#pragma once

#include "ux/settings/data/monitors/settings_monitors.hpp"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

namespace eh::settings_monitors_tab {

// Width a plain toolbar button needs for `label` (Inter 12px / 400, plus the
// 2x10px horizontal padding m3::Button adds). m3::Button auto-expands its
// geometry — and recentres by shifting left — when the requested width is
// smaller than this, so callers must size the slot from this value to keep
// painted and hit-tested rects identical.
[[nodiscard]] inline int monitors_toolbar_btn_label_width_px(const char* label) {
  if (!label || !label[0]) return 0;
  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* cr = cairo_create(surface);
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(12.0f * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(400));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, label, -1);
  int pw = 0, ph = 0;
  pango_layout_get_pixel_size(layout, &pw, &ph);
  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  return pw + 20;
}

[[nodiscard]] inline int monitors_main_column_width_px(int content_w) {
  return std::max(100, (content_w * 68) / 100);
}

[[nodiscard]] inline int monitors_main_column_left_px(int content_x, int content_w, int column_w) {
  return content_x + (content_w - column_w) / 2;
}

[[nodiscard]] bool parse_position_xy(const std::string& pos, int* ox, int* oy);
void format_position_xy(int x, int y, std::string* out);

void monitor_layout_extents(const eh::settings_monitors::MonitorRow& row,
                            const eh::settings_monitors::OutputCaps* cap, int* lw, int* lh);

struct ArrangeRect {
  int x, y, w, h;
};

void layout_rects_from_outputs(const std::vector<eh::settings_monitors::MonitorRow>& outputs,
                               const std::unordered_map<std::string, eh::settings_monitors::OutputCaps>& caps,
                               std::vector<ArrangeRect>* out_rects);

void compute_canvas_fit(const std::vector<ArrangeRect>& rects, int cw, int ch, double pad_frac, double* min_x,
                        double* min_y, double* span_w, double* span_h, double* fit_scale);

void canvas_delta_to_monitor_delta(double d_canvas_x, double d_canvas_y, double scale_factor, double* d_mon_x,
                                   double* d_mon_y);


void finalize_arrangement_drag(std::vector<eh::settings_monitors::MonitorRow>& outputs,
                               const std::unordered_map<std::string, eh::settings_monitors::OutputCaps>& caps,
                               size_t idx_moved);

void align_all_tops(std::vector<eh::settings_monitors::MonitorRow>& outputs,
                    const std::unordered_map<std::string, eh::settings_monitors::OutputCaps>& caps);

struct MonitorsTabLayout {
  int toolbar_refresh_x = 0;
  int toolbar_portals_x = 0;
  int toolbar_portals_w = 0;
  int toolbar_apply_x = 0;
  int toolbar_revert_x = 0;
  int toolbar_y = 0;
  int toolbar_btn_w = 0;
  int toolbar_btn_h = 0;
  int canvas_x = 0;
  int canvas_y = 0;
  int canvas_w = 0;
  int canvas_h = 300;
  int center_btn_x = 0;
  int align_top_btn_x = 0;
  int aux_btn_y = 0;
  int aux_btn_w = 0;
  int aux_btn_h = 0;
  int pills_y = 0;
  int form_y = 0;
};

void compute_monitors_tab_layout(int content_x, int content_w, int content_top, int viewport_h, MonitorsTabLayout* lay);

}
