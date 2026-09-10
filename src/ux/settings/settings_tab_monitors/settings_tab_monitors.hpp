#pragma once

#include <cairo/cairo.h>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "ux/settings/data/monitors/settings_monitors.hpp"
#include "ux/settings/utils/monitors/settings_monitors_tab.hpp"

struct App;

namespace eh::settings_monitors_tab {
struct MonitorsTabLayout;
}

void settings_clamp_monitors_scroll_px(App& app);
void monitors_wheel_zoom_canvas(App& app, const eh::settings_monitors_tab::MonitorsTabLayout& lay, double delta_px);

// Layout constants.
static constexpr int kMonToolbarBtnW = 92;
static constexpr int kMonToolbarBtnH = 30;
static constexpr int kMonFormRowPitch = 42;
static constexpr int kMonFormLabelColW = 192;
static constexpr int kMonPillH = 28;
static constexpr int kMonSecGap = 12;
static constexpr int kMonHeaderCardH = 52;
static constexpr int kMonSecTitlePadTop = 12;
static constexpr int kMonSecTitleLineH = 18;
static constexpr int kMonSecAfterTitle = 10;
static constexpr int kMonSecBottomPad = 14;
static constexpr int kMonFormValRailPx = 12;

static constexpr int kMonitorCmCount = 9;

extern const char* kMonitorTfLabels[8];
extern const char* kMonitorCmLabels[kMonitorCmCount];
extern const char* kMonitorBitdepthDdLabels[2];
extern const char* kMonSupportsHdrDdLabels[3];
extern const char* kMonSupportsWideDdLabels[3];
extern const char* kMonSdrEotfDdLabels[3];

// Geometry structs.
struct MonitorsFormRows {
  bool show_vrr = false;
  bool hdr_panel = false;
  bool hdr_luminance_panel = false;
  int d_res = 0;
  int d_hz = 1;
  int d_vrr = -1;
  int s_scale = 0;
  int s_tf = 1;
  int c_bit = -1;
  int c_cm = -1;
  int c_icc = -1;
  int h_hdr_support = 0;
  int h_wide = 1;
  int h_eotf = 2;
  int h_sdr_b = 3;
  int h_sdr_s = 4;
  int l_sdr_min = 0;
  int l_sdr_max = 1;
  int l_min = 2;
  int l_max = 3;
  int l_avg = 4;
};

struct MonitorsSectionGeom {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int content_y0 = 0;
};

struct MonitorsFormGeom {
  MonitorsSectionGeom header;
  MonitorsSectionGeom display;
  MonitorsSectionGeom scale;
  MonitorsSectionGeom color;
  MonitorsSectionGeom hdr;
  MonitorsSectionGeom luminance;
  bool has_color = false;
  bool has_hdr = false;
  bool has_luminance = false;
  int bottom_y = 0;
};

// Paint.
void paint_monitors_tab(App& app, cairo_t* cr, int contentX, int contentW,
                        double cardX, double cardW, double glassOv, double dockMatA,
                        double paintPointerYOffset);
