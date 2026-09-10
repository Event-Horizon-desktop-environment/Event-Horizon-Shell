#include "ux/settings/utils/monitors/settings_monitors_tab.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>

namespace eh::settings_monitors_tab {

bool parse_position_xy(const std::string& pos, int* ox, int* oy) {
   
  if (!ox || !oy) return false;
  const size_t sep = pos.find('x');
  if (sep == std::string::npos) return false;
  *ox = static_cast<int>(std::strtol(pos.c_str(), nullptr, 10));
  *oy = static_cast<int>(std::strtol(pos.c_str() + sep + 1, nullptr, 10));
  return true;
}

void format_position_xy(int x, int y, std::string* out) {
  if (!out) return;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%dx%d", x, y);
  *out = buf;
}

static void physical_mode_px(const eh::settings_monitors::MonitorRow& row,
                             const eh::settings_monitors::OutputCaps* cap, int* pw, int* ph) {
  int w = 0, h = 0;
  const size_t sep = row.resolution.find('x');
  if (sep != std::string::npos) {
    w = static_cast<int>(std::strtol(row.resolution.c_str(), nullptr, 10));
    h = static_cast<int>(std::strtol(row.resolution.c_str() + sep + 1, nullptr, 10));
  }
  if ((w <= 0 || h <= 0) && cap && cap->width > 0 && cap->height > 0) {
    w = cap->width;
    h = cap->height;
  }
  if (w <= 0) w = 1920;
  if (h <= 0) h = 1080;
  *pw = w;
  *ph = h;
}

void monitor_layout_extents(const eh::settings_monitors::MonitorRow& row,
                            const eh::settings_monitors::OutputCaps* cap, int* lw, int* lh) {
  int pw = 0, ph = 0;
  physical_mode_px(row, cap, &pw, &ph);
  char* e = nullptr;
  double sc = std::strtod(row.scale.c_str(), &e);
  if (e == row.scale.c_str() || sc <= 0.0) sc = 1.0;
  int fw = static_cast<int>(std::lround(static_cast<double>(pw) / sc));
  int fh = static_cast<int>(std::lround(static_cast<double>(ph) / sc));
  const int tr = std::clamp(std::atoi(row.transform.c_str()), 0, 7);
  if (tr == 1 || tr == 3 || tr == 5 || tr == 7) std::swap(fw, fh);
  *lw = std::max(1, fw);
  *lh = std::max(1, fh);
}

void layout_rects_from_outputs(const std::vector<eh::settings_monitors::MonitorRow>& outputs,
                               const std::unordered_map<std::string, eh::settings_monitors::OutputCaps>& caps,
                               std::vector<ArrangeRect>* out_rects) {
  if (!out_rects) return;
  out_rects->clear();
  out_rects->reserve(outputs.size());
  for (const auto& row : outputs) {
    ArrangeRect r{};
    if (row.disabled) {
      out_rects->push_back(r);
      continue;
    }
    const eh::settings_monitors::OutputCaps* cap = nullptr;
    auto it = caps.find(row.name);
    if (it != caps.end()) cap = &it->second;
    monitor_layout_extents(row, cap, &r.w, &r.h);
    if (!parse_position_xy(row.position.empty() ? "0x0" : row.position, &r.x, &r.y)) {
      r.x = 0;
      r.y = 0;
    }
    out_rects->push_back(r);
  }
}

void compute_canvas_fit(const std::vector<ArrangeRect>& rects, int cw, int ch, double pad_frac, double* min_x,
                        double* min_y, double* span_w, double* span_h, double* fit_scale) {
  double mnX = 0, mnY = 0, mxX = 0, mxY = 0;
  bool any = false;
  for (const auto& r : rects) {
    if (r.w <= 0 || r.h <= 0) continue;
    if (!any) {
      mnX = r.x;
      mnY = r.y;
      mxX = r.x + r.w;
      mxY = r.y + r.h;
      any = true;
    } else {
      mnX = std::min(mnX, static_cast<double>(r.x));
      mnY = std::min(mnY, static_cast<double>(r.y));
      mxX = std::max(mxX, static_cast<double>(r.x + r.w));
      mxY = std::max(mxY, static_cast<double>(r.y + r.h));
    }
  }
  if (!any) {
    if (min_x) *min_x = 0;
    if (min_y) *min_y = 0;
    if (span_w) *span_w = 1;
    if (span_h) *span_h = 1;
    if (fit_scale) *fit_scale = 1;
    return;
  }
  const double sw = std::max(1.0, mxX - mnX);
  const double sh = std::max(1.0, mxY - mnY);
  const double inner_w = std::max(1.0, static_cast<double>(cw) * (1.0 - 2.0 * pad_frac));
  const double inner_h = std::max(1.0, static_cast<double>(ch) * (1.0 - 2.0 * pad_frac));
  const double fs = std::min(inner_w / sw, inner_h / sh);
  if (min_x) *min_x = mnX;
  if (min_y) *min_y = mnY;
  if (span_w) *span_w = sw;
  if (span_h) *span_h = sh;
  if (fit_scale) *fit_scale = fs;
}

void canvas_delta_to_monitor_delta(double d_canvas_x, double d_canvas_y, double scale_factor, double* d_mon_x,
                                   double* d_mon_y) {
  const double inv = scale_factor > 1e-9 ? (1.0 / scale_factor) : 1.0;
  if (d_mon_x) *d_mon_x = d_canvas_x * inv;
  if (d_mon_y) *d_mon_y = d_canvas_y * inv;
}

namespace {

constexpr int kSnapPx = 40;

void rects_from_outputs(std::vector<eh::settings_monitors::MonitorRow>& outputs,
                        const std::unordered_map<std::string, eh::settings_monitors::OutputCaps>& caps,
                        std::vector<ArrangeRect>* rects) {
  layout_rects_from_outputs(outputs, caps, rects);
}

void write_rect_to_row(ArrangeRect r, eh::settings_monitors::MonitorRow* row) {
  if (!row) return;
  format_position_xy(r.x, r.y, &row->position);
}

bool aabb_overlap(const ArrangeRect& a, const ArrangeRect& b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

void separate_pair(ArrangeRect& a, ArrangeRect& b) {
  const int axc = a.x + a.w / 2;
  const int bxc = b.x + b.w / 2;
  const int ayc = a.y + a.h / 2;
  const int byc = b.y + b.h / 2;
  const int overlap_x = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
  const int overlap_y = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
  if (overlap_x <= 0 || overlap_y <= 0) return;
  if (overlap_x < overlap_y) {
    if (axc < bxc) {
      a.x -= (overlap_x + 1) / 2;
      b.x += (overlap_x + 1) / 2;
    } else {
      a.x += (overlap_x + 1) / 2;
      b.x -= (overlap_x + 1) / 2;
    }
  } else {
    if (ayc < byc) {
      a.y -= (overlap_y + 1) / 2;
      b.y += (overlap_y + 1) / 2;
    } else {
      a.y += (overlap_y + 1) / 2;
      b.y -= (overlap_y + 1) / 2;
    }
  }
}

void resolve_collisions(std::vector<ArrangeRect>& rects, const std::vector<eh::settings_monitors::MonitorRow>& outputs) {
  for (int iter = 0; iter < 24; ++iter) {
    bool moved = false;
    for (size_t i = 0; i < rects.size(); ++i) {
      if (i < outputs.size() && outputs[i].disabled) continue;
      if (rects[i].w <= 0) continue;
      for (size_t j = i + 1; j < rects.size(); ++j) {
        if (j < outputs.size() && outputs[j].disabled) continue;
        if (rects[j].w <= 0) continue;
        if (!aabb_overlap(rects[i], rects[j])) continue;
        separate_pair(rects[i], rects[j]);
        moved = true;
      }
    }
    if (!moved) break;
  }
}

void normalize_origin(std::vector<ArrangeRect>& rects, const std::vector<eh::settings_monitors::MonitorRow>& outputs) {
  int mnX = std::numeric_limits<int>::max();
  int mnY = std::numeric_limits<int>::max();
  for (size_t i = 0; i < rects.size(); ++i) {
    if (i < outputs.size() && outputs[i].disabled) continue;
    if (rects[i].w <= 0) continue;
    mnX = std::min(mnX, rects[i].x);
    mnY = std::min(mnY, rects[i].y);
  }
  if (mnX == std::numeric_limits<int>::max()) return;
  for (auto& r : rects) {
    if (r.w <= 0) continue;
    r.x -= mnX;
    r.y -= mnY;
  }
}

void snap_edges(size_t moved_idx, std::vector<ArrangeRect>& rects,
                const std::vector<eh::settings_monitors::MonitorRow>& outputs) {
  if (moved_idx >= rects.size()) return;
  if (moved_idx < outputs.size() && outputs[moved_idx].disabled) return;
  ArrangeRect& me = rects[moved_idx];
  if (me.w <= 0) return;

  auto try_snap_int = [](int val, int target, int threshold) -> std::optional<int> {
    if (std::abs(val - target) <= threshold) return target;
    return std::nullopt;
  };

  for (size_t j = 0; j < rects.size(); ++j) {
    if (j == moved_idx) continue;
    if (j < outputs.size() && outputs[j].disabled) continue;
    const ArrangeRect& o = rects[j];
    if (o.w <= 0) continue;

    if (auto sx = try_snap_int(me.x, o.x - me.w, kSnapPx)) me.x = *sx;
    if (auto sx = try_snap_int(me.x, o.x + o.w, kSnapPx)) me.x = *sx;
    if (auto sy = try_snap_int(me.y, o.y - me.h, kSnapPx)) me.y = *sy;
    if (auto sy = try_snap_int(me.y, o.y + o.h, kSnapPx)) me.y = *sy;

    if (auto sy = try_snap_int(me.y, o.y, kSnapPx)) me.y = *sy;
    if (auto sy = try_snap_int(me.y + me.h, o.y + o.h, kSnapPx)) me.y = *sy - me.h;
    const int oc = o.y + o.h / 2;
    const int mc = me.y + me.h / 2;
    if (auto sy = try_snap_int(mc, oc, kSnapPx)) me.y = *sy - me.h / 2;

    if (auto sx = try_snap_int(me.x, o.x, kSnapPx)) me.x = *sx;
    if (auto sx = try_snap_int(me.x + me.w, o.x + o.w, kSnapPx)) me.x = *sx - me.w;
    const int ocx = o.x + o.w / 2;
    const int mcx = me.x + me.w / 2;
    if (auto sx = try_snap_int(mcx, ocx, kSnapPx)) me.x = *sx - me.w / 2;
  }
}

}

void finalize_arrangement_drag(std::vector<eh::settings_monitors::MonitorRow>& outputs,
                               const std::unordered_map<std::string, eh::settings_monitors::OutputCaps>& caps,
                               size_t idx_moved) {
  std::vector<ArrangeRect> rects;
  rects_from_outputs(outputs, caps, &rects);
  resolve_collisions(rects, outputs);
  snap_edges(idx_moved, rects, outputs);
  normalize_origin(rects, outputs);
  for (size_t i = 0; i < outputs.size() && i < rects.size(); ++i) {
    if (outputs[i].disabled) continue;
    write_rect_to_row(rects[i], &outputs[i]);
  }
}

void align_all_tops(std::vector<eh::settings_monitors::MonitorRow>& outputs,
                    const std::unordered_map<std::string, eh::settings_monitors::OutputCaps>& caps) {
  std::vector<ArrangeRect> rects;
  rects_from_outputs(outputs, caps, &rects);
  int best_y = std::numeric_limits<int>::max();
  for (size_t i = 0; i < rects.size(); ++i) {
    if (i < outputs.size() && outputs[i].disabled) continue;
    if (rects[i].w <= 0) continue;
    best_y = std::min(best_y, rects[i].y);
  }
  if (best_y == std::numeric_limits<int>::max()) return;
  for (size_t i = 0; i < rects.size(); ++i) {
    if (i < outputs.size() && outputs[i].disabled) continue;
    if (rects[i].w <= 0) continue;
    rects[i].y = best_y;
  }
  normalize_origin(rects, outputs);
  for (size_t i = 0; i < outputs.size() && i < rects.size(); ++i) {
    if (outputs[i].disabled) continue;
    write_rect_to_row(rects[i], &outputs[i]);
  }
}

void compute_monitors_tab_layout(int content_x, int content_w, int content_top, int viewport_h, MonitorsTabLayout* lay) {
  if (!lay) return;
  constexpr int btn_w = 92;
  constexpr int btn_h = 30;
  constexpr int gap = 10;
  constexpr int center_gap = 40;

  constexpr int kAboveCanvasPx = 126;

  constexpr int kBelowCanvasPx = 88;

  constexpr int kMinFormPeekPx = 72;
  constexpr int kCanvasMinH = 200;
  constexpr int kCanvasMaxH = 720;

  constexpr double kWidgetCanvasHeightScale = 0.56;
  lay->toolbar_btn_w = btn_w;
  lay->toolbar_btn_h = btn_h;
  lay->toolbar_y = content_top + 56;
  lay->toolbar_refresh_x = content_x + 16;
  lay->toolbar_revert_x = content_x + content_w - 16 - btn_w;
  lay->toolbar_apply_x = lay->toolbar_revert_x - gap - btn_w;
  {
    const int space = lay->toolbar_apply_x - (lay->toolbar_refresh_x + btn_w);
    const int cg_w = 2 * btn_w + center_gap;
    const int cg_x = lay->toolbar_refresh_x + btn_w + std::max(0, (space - cg_w) / 2);
    lay->center_btn_x = cg_x;
    lay->align_top_btn_x = cg_x + btn_w + center_gap;
  }
  lay->canvas_y = lay->toolbar_y + btn_h + 40;
  {
    const int col_w = monitors_main_column_width_px(content_w);
    lay->canvas_w = col_w;
    lay->canvas_x = monitors_main_column_left_px(content_x, content_w, col_w);
  }
  {
    const int vh = std::max(200, viewport_h);
    const int avail = vh - kAboveCanvasPx - kBelowCanvasPx - kMinFormPeekPx;
    const int base_h = std::clamp(avail, kCanvasMinH, kCanvasMaxH);
    lay->canvas_h = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(base_h) * kWidgetCanvasHeightScale)),
        static_cast<int>(std::lround(static_cast<double>(kCanvasMinH) * kWidgetCanvasHeightScale)),
        static_cast<int>(std::lround(static_cast<double>(kCanvasMaxH) * kWidgetCanvasHeightScale)));
  }
  lay->aux_btn_y = lay->toolbar_y;
  lay->aux_btn_w = btn_w;
  lay->aux_btn_h = btn_h;
  lay->pills_y = lay->canvas_y + lay->canvas_h + 10;
  lay->form_y = lay->pills_y + 40;
}

}
