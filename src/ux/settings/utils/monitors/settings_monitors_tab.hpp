#pragma once

#include "ux/settings/data/monitors/settings_monitors.hpp"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace eh::settings_monitors_tab {

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
