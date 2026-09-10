#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "services/audio/pipewire_service.hpp"
#include "../../../controlcenter/mixer/mixer_stream_icon_resolve.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "m3/core/primitives/box.hpp"
#include <cairo/cairo.h>

namespace eh::shell::dock::popup::volume_mixer {
namespace {

// Constants.

constexpr double kPad          = 16.0;
constexpr double kCornerRadius = 14.0;
constexpr double kHeaderH      = 48.0;
constexpr double kPillH        = 28.0;
constexpr double kPillR        = 8.0;
constexpr double kRowH         = 48.0;
constexpr double kRadioR       = 9.0;
constexpr double kRadioInnerR  = 5.0;
constexpr double kDevIconSz    = 20.0;
constexpr double kMuteBtnSz    = 26.0;
constexpr double kMuteIconSz   = 18.0;
constexpr double kAppIconSz    = 24.0;
constexpr double kSliderTrackH = 8.0;
constexpr double kSliderThumbR = 9.0;
constexpr double kSliderMinW   = 60.0;
constexpr double kNameMaxW     = 160.0;
constexpr double kAppNameMaxW  = 110.0;
constexpr double kPickerBtnW   = 100.0;
constexpr double kPickerBtnH   = 26.0;
constexpr double kEqBtnSz      = 30.0;
constexpr double kCloseBtnSz   = 26.0;

constexpr double kShadOffX  = 2.0;
constexpr double kShadOffY  = 4.0;
constexpr double kShadAlpha = 0.28;

constexpr int    kEqBandCount   = 10;
constexpr double kEqBandH       = 100.0;
constexpr int    kEqBandMinDb   = -12;
constexpr int    kEqBandMaxDb   = 12;
constexpr double kEqPanelPad    = 8.0;
constexpr double kEqPresetBtnH  = 30.0;
constexpr double kEqPresetBtnW  = 100.0;
constexpr double kEqBandW       = 30.0;

// Data types.

struct DeviceInfo {
  std::uint32_t node_id = 0;
  std::string name{};
  bool is_default = false;
  bool muted = false;
  int volume_pct = 0;
};

struct StreamInfo {
  std::uint32_t node_id = 0;
  int volume_pct = 0;
  bool muted = false;
  std::string app_name{};
  std::string icon_name{};
  std::string app_id{};
  std::string process_binary{};
  std::string node_name{};
  std::string description{};
  std::string routed_sink_name{};
};

struct MixerSnapshot {
  std::vector<DeviceInfo> sinks{};
  std::vector<DeviceInfo> sources{};
  std::vector<StreamInfo> streams{};
};

MixerSnapshot take_snapshot() {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const auto s = pw.snapshot();

  MixerSnapshot out;
  for (const auto& d : s.sinks) {
    DeviceInfo di;
    di.node_id = d.node_id;
    di.name = d.display_name.empty() ? d.name : d.display_name;
    di.is_default = d.is_default;
    di.muted = d.muted;
    di.volume_pct = d.volume_pct;
    out.sinks.push_back(std::move(di));
  }
  for (const auto& d : s.sources) {
    DeviceInfo di;
    di.node_id = d.node_id;
    di.name = d.display_name.empty() ? d.name : d.display_name;
    di.is_default = d.is_default;
    di.muted = d.muted;
    di.volume_pct = d.volume_pct;
    out.sources.push_back(std::move(di));
  }
  for (const auto& st : s.output_streams) {
    StreamInfo si;
    si.node_id = st.node_id;
    si.volume_pct = st.volume_pct;
    si.muted = st.muted;
    si.app_name = st.app_name;
    si.icon_name = st.icon_name;
    si.app_id = st.app_id;
    si.process_binary = st.process_binary;
    si.node_name = st.node_name;
    si.description = st.description;
    si.routed_sink_name = pw.playback_route_sink_name(st.node_id);

    {
      auto lower = [](const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
      };

      auto is_generic_icon_name = [](const std::string& n) -> bool {
        std::string low = n;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (low.empty()) return true;
        if (low == "chromium" || low == "electron") return true;
        if (low == "chromium-browser" || low == "google-chrome" || low == "google-chrome-stable") return true;
        if (low.substr(0, 8) == "electron" && low.size() > 8) return true;
        return false;
      };
      if (is_generic_icon_name(si.icon_name))
        si.icon_name.clear();

      StreamDesktopIds ids;
      ids.process_binary = si.process_binary;
      ids.app_id = si.app_id;
      ids.app_name = si.app_name;
      ids.node_name = si.node_name;
      ids.node_description = si.description;
      auto dei = resolve_desktop_entry_for_stream(ids);
      if (dei) {
        if (!dei->icon.empty())
          si.icon_name = dei->icon;
      }
      if (dei && !dei->name.empty()) {
        si.app_name = dei->name;
      } else {
        if (!si.description.empty() && !is_generic_runtime_binary(lower(si.description))) {
          si.app_name = si.description;
        } else if (!si.app_name.empty() && !is_generic_runtime_binary(lower(si.app_name))) {
        } else {
          auto candidate = [&](const std::string& s) -> std::string {
            if (s.empty()) return {};
            std::string low = lower(s);
            if (is_generic_runtime_binary(low)) return {};
            low[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(low[0])));
            return low;
          };
          std::string name;
          if (name.empty() && !si.process_binary.empty()) {
            std::string bin = si.process_binary;
            const auto slash = bin.find_last_of("/\\");
            if (slash != std::string::npos && slash + 1 < bin.size()) bin = bin.substr(slash + 1);
            name = candidate(bin);
          }
          if (name.empty()) name = candidate(si.app_id);
          if (name.empty()) name = candidate(si.node_name);
          if (!name.empty()) si.app_name = name;
        }
      }
    }
    if (si.app_name.empty()) si.app_name = "Unknown";

    out.streams.push_back(std::move(si));
  }
  return out;
}

// Per-app UI state.

struct AppUiState {
  bool eqExpanded = false;
  bool eqEnabled = false;
  int eqPresetIdx = 0;
  std::array<int, kEqBandCount> eqBands{};
  bool routerExpanded = false;
};

struct MixerUiState {
  bool viewOutput = true;
  std::unordered_map<std::uint32_t, AppUiState> apps{};
  int dragStreamId = -1;
  int dragDeviceIdx = -1;
  int sliderDragStartPct = -1;
};

MixerUiState& ui() {
  static MixerUiState s;
  return s;
}

// Layout helpers.

constexpr double content_w() { return static_cast<double>(kVolumeMixerPopupW) - kPad * 2.0; }

double row_start_x() { return kPad; }
double row_end_x()   { return static_cast<double>(kVolumeMixerPopupW) - kPad; }

struct SliderGeom {
  double sx, sy, sw, sh;
};

SliderGeom device_slider_geom(int row_index, int /*count*/, double header_bottom) {
  const double row_y = header_bottom + static_cast<double>(row_index) * kRowH;
  const double slider_left = row_start_x() + kRadioR * 2.0 + 8.0 + kDevIconSz + 8.0 + kNameMaxW + 8.0 + kMuteBtnSz + 4.0;
  const double slider_w = std::max(kSliderMinW, row_end_x() - slider_left - kPad);
  return {slider_left, row_y + (kRowH - kSliderTrackH) * 0.5, slider_w, kSliderTrackH};
}

SliderGeom stream_slider_geom(int row_index, double header_bottom, double devices_h) {
  const double row_y = header_bottom + devices_h + 16.0 + 1.0 + 28.0 + static_cast<double>(row_index) * kRowH;
  const double slider_left = row_start_x() + kAppIconSz + 8.0 + kAppNameMaxW + 4.0 + kMuteBtnSz + 4.0;
  const double slider_right = row_end_x() - kPad - kPickerBtnW - 4.0 - kEqBtnSz - 4.0 - kCloseBtnSz;
  const double slider_w = std::max(kSliderMinW, slider_right - slider_left);
  return {slider_left, row_y + (kRowH - kSliderTrackH) * 0.5, slider_w, kSliderTrackH};
}

// Paint helpers.

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double rad = std::min({r, w * 0.5, h * 0.5});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - rad, y + rad,     rad, -M_PI_2,      0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad,       0, M_PI_2);
  cairo_arc(cr, x + rad,     y + h - rad, rad,  M_PI_2,   M_PI);
  cairo_arc(cr, x + rad,     y + rad,     rad,     M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

void paint_slider(cairo_t* cr, double x, double y, double w, double t,
                  double dimR, double dimG, double dimB,
                  double accR, double accG, double accB,
                  double fillR, double fillG, double fillB) {
  const double fillW = w * t;
  const double midY = y + kSliderTrackH * 0.5;
  const double thumbCx = std::clamp(x + w * t, x + kSliderThumbR, x + w - kSliderThumbR);

  // Track with glassy
  {
    m3::Box tr;
    tr.setColor(static_cast<float>(dimR), static_cast<float>(dimG), static_cast<float>(dimB), 0.55f);
    tr.setRadius(static_cast<float>(kSliderTrackH * 0.5));
    tr.setGeometry(static_cast<float>(x), static_cast<float>(y),
                   static_cast<float>(w), static_cast<float>(kSliderTrackH));
    tr.setGlassy(true);
    tr.paint(cr);
  }

  if (fillW > 1.0) {
    m3::Box fl;
    fl.setColor(static_cast<float>(accR), static_cast<float>(accG), static_cast<float>(accB), 0.90f);
    fl.setRadius(static_cast<float>(kSliderTrackH * 0.5));
    fl.setGeometry(static_cast<float>(x), static_cast<float>(y),
                   static_cast<float>(fillW), static_cast<float>(kSliderTrackH));
    fl.setGlassy(true);
    fl.paint(cr);
  }

  cairo_new_path(cr);
  cairo_arc(cr, thumbCx, midY, kSliderThumbR, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, fillR, fillG, fillB, 0.98);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, accR, accG, accB, 0.45);
  cairo_set_line_width(cr, 1.5);
  cairo_stroke(cr);
}

const char* volume_glyph(int pct, bool muted) {
  if (muted || pct <= 0) return "volume_off";
  if (pct < 34) return "volume_down";
  return "volume_up";
}

const char* device_glyph(const std::string& name) {
  const std::string lower(name);
  if (lower.find("bluez") != std::string::npos ||
      lower.find("headphone") != std::string::npos ||
      lower.find("wh-") != std::string::npos)
    return "headphones";
  if (lower.find("hdmi") != std::string::npos) return "tv";
  if (lower.find("usb") != std::string::npos) return "usb";
  if (lower.find("mic") != std::string::npos) return "mic";
  return "speaker";
}

// Hover helpers.

enum class HoverKey : int {
  None = -1,
  OutputPill,
  InputPill,
  DeviceRowStart,
  DeviceMuteStart = DeviceRowStart + 100,
  DeviceSliderStart = DeviceRowStart + 200,
  StreamRowStart = DeviceRowStart + 300,
  StreamMuteStart = DeviceRowStart + 400,
  StreamSliderStart = DeviceRowStart + 500,
  PickerBtnStart = DeviceRowStart + 600,
  EqBtnStart = DeviceRowStart + 700,
  CloseBtnStart = DeviceRowStart + 800,
  SinkRowStart = DeviceRowStart + 900,
};

template<typename A>
HoverKey current_hover(const A& app, int devCount, int streamCount,
                       double headerBottom, double devicesH, double streamsTop, bool hasSources) {
  if (app.pointerSurface != app.popupSurface) return HoverKey::None;
  const double x = app.pointerX;
  const double y = app.pointerY;
  const double cw = content_w();
  const double cx = row_start_x();
  if (x < 0 || x >= static_cast<double>(kVolumeMixerPopupW) || y < 0) return HoverKey::None;

  const double pillY = 10.0;
  if (y >= pillY && y < pillY + kPillH) {
    const double outPillW = 28.0 + 8.0 + 100.0;
    if (x >= cx && x < cx + outPillW) return HoverKey::OutputPill;
    if (hasSources) {
      const double inPillW = 28.0 + 8.0 + 80.0;
      if (x >= cx + cw - inPillW && x < cx + cw) return HoverKey::InputPill;
    }
  }

  const double devRowsY = headerBottom + 4.0;
  for (int i = 0; i < devCount; ++i) {
    const double rowY = devRowsY + static_cast<double>(i) * kRowH;
    if (y >= rowY && y < rowY + kRowH) {
      const double muteX = cx + kRadioR * 2.0 + 8.0 + kDevIconSz + 8.0 + kNameMaxW + 8.0;
      if (x >= muteX && x < muteX + kMuteBtnSz) return static_cast<HoverKey>(static_cast<int>(HoverKey::DeviceMuteStart) + i);
      const auto sg = device_slider_geom(i, devCount, headerBottom);
      if (x >= sg.sx - 6.0 && x <= sg.sx + sg.sw + 6.0 && y >= sg.sy - 15.0 && y <= sg.sy + kSliderTrackH + 15.0)
        return static_cast<HoverKey>(static_cast<int>(HoverKey::DeviceSliderStart) + i);
      if (x >= cx && x <= cx + cw) return static_cast<HoverKey>(static_cast<int>(HoverKey::DeviceRowStart) + i);
    }
  }

  for (int i = 0; i < streamCount; ++i) {
    const double rowY = streamsTop + static_cast<double>(i) * kRowH;
    if (y >= rowY && y < rowY + kRowH) {
      const double muteX = cx + kAppIconSz + 8.0 + kAppNameMaxW + 4.0;
      if (x >= muteX && x < muteX + kMuteBtnSz) return static_cast<HoverKey>(static_cast<int>(HoverKey::StreamMuteStart) + i);
      const auto sg = stream_slider_geom(i, headerBottom, 4.0 + devicesH + 16.0 + 1.0 + 28.0);
      if (x >= sg.sx - 6.0 && x <= sg.sx + sg.sw + 6.0 && y >= sg.sy - 15.0 && y <= sg.sy + kSliderTrackH + 15.0)
        return static_cast<HoverKey>(static_cast<int>(HoverKey::StreamSliderStart) + i);
      const double pickerX = row_end_x() - kPad - kPickerBtnW - 4.0 - kEqBtnSz - 4.0 - kCloseBtnSz;
      const double pickerY = rowY + (kRowH - kPickerBtnH) * 0.5;
      if (x >= pickerX && x < pickerX + kPickerBtnW && y >= pickerY && y < pickerY + kPickerBtnH)
        return static_cast<HoverKey>(static_cast<int>(HoverKey::PickerBtnStart) + i);
      const double eqBtnX = pickerX + kPickerBtnW + 4.0;
      const double eqBtnY = rowY + (kRowH - kEqBtnSz) * 0.5;
      if (x >= eqBtnX && x < eqBtnX + kEqBtnSz && y >= eqBtnY && y < eqBtnY + kEqBtnSz)
        return static_cast<HoverKey>(static_cast<int>(HoverKey::EqBtnStart) + i);
      const double closeX = eqBtnX + kEqBtnSz + 4.0;
      if (x >= closeX && x < closeX + kCloseBtnSz && y >= eqBtnY && y < eqBtnY + kCloseBtnSz)
        return static_cast<HoverKey>(static_cast<int>(HoverKey::CloseBtnStart) + i);
      return static_cast<HoverKey>(static_cast<int>(HoverKey::StreamRowStart) + i);
    }
  }

  return HoverKey::None;
}

// Icon helpers.

template<typename A>
const cairo_surface_t* stream_icon_surface(const A& app, const StreamInfo& st) {
  auto& icons = const_cast<A&>(app).icons;
  eh::shell::mixer_icon::StreamIconIds ids{};
  ids.icon_name      = st.icon_name;
  ids.app_id         = st.app_id;
  ids.process_binary = st.process_binary;
  ids.app_name       = st.app_name;
  if (const auto* ic = eh::shell::mixer_icon::resolve_mixer_stream_theme_icon(icons, ids))
    if (ic->surface) return ic->surface;
  return nullptr;
}

template<typename A>
void paint_stream_app_icon(cairo_t* cr, const A& app, const StreamInfo& st, double cx, double cy, double size) {
  if (const auto* surf = stream_icon_surface(app, st)) {
    auto* msurf = const_cast<cairo_surface_t*>(surf);
    const int iw = cairo_image_surface_get_width(msurf);
    const int ih = cairo_image_surface_get_height(msurf);
    if (iw > 0 && ih > 0) {
      const double sc = std::min(size / static_cast<double>(iw), size / static_cast<double>(ih));
      const double dw = static_cast<double>(iw) * sc;
      const double dh = static_cast<double>(ih) * sc;
      const double dx = cx - dw * 0.5;
      const double dy = cy - dh * 0.5;
      cairo_save(cr);
      cairo_translate(cr, dx, dy);
      cairo_scale(cr, sc, sc);
      cairo_set_source_surface(cr, msurf, 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
      return;
    }
  }
  eh::shell::draw_material_glyph(cr, cx, cy, size * 0.6, "apps", 0.78, 0.78, 0.78, 0.78);
}

// Highlight paint.

void paint_hover_highlight(cairo_t* cr, double x, double y, double w, double h, double r,
                           double hr, double hg, double hb) {
  rounded_rect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, hr, hg, hb, 0.08);
  cairo_fill(cr);
}

} // anonymous namespace

template<typename A>
inline void dock_volume_mixer_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  const double W = static_cast<double>(kVolumeMixerPopupW);
  const double H = static_cast<double>(app.popupH);

  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);

  // Shadow.
  cairo_save(cr);
  rounded_rect(cr, kShadOffX, kShadOffY, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, 0, 0, 0, kShadAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  // Background.
  {
    m3::Box box;
    box.setColor(static_cast<float>(mc.dockFillR * 0.35), static_cast<float>(mc.dockFillG * 0.35),
                 static_cast<float>(mc.dockFillB * 0.35), 0.78f);
    box.setRadius(static_cast<float>(kCornerRadius));
    box.setGeometry(0, 0, static_cast<float>(W), static_cast<float>(H));
    box.setGlassy(true);
    box.paint(cr);
  }

  rounded_rect(cr, 0.5, 0.5, W - 1.0, H - 1.0, kCornerRadius);
  // Border.
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const MixerSnapshot snap = take_snapshot();
  const MixerUiState& u = ui();
  const auto& devices = u.viewOutput ? snap.sinks : snap.sources;
  const auto& streams = u.viewOutput ? snap.streams : std::vector<StreamInfo>();

  const double cw = content_w();
  const double cx = row_start_x();
  const double top = 0.0;
  const double headerBottom = top + kHeaderH;
  const double devicesH = static_cast<double>(devices.size()) * kRowH;
  const double appsDividerY = headerBottom + 4.0 + devicesH + 16.0;
  const double appsHeaderH = 28.0;
  const double streamsTop = appsDividerY + appsHeaderH;
  const int devCount = static_cast<int>(devices.size());
  const int streamCount = static_cast<int>(streams.size());

  const bool hasSources = !snap.sources.empty();
  const HoverKey hk = current_hover(app, devCount, streamCount, headerBottom, devicesH, streamsTop, hasSources);

  // Helper lambda: is a given hover key in a range
  auto hk_in_range = [](HoverKey hk, HoverKey start, int count) -> int {
    int v = static_cast<int>(hk);
    int s = static_cast<int>(start);
    if (v >= s && v < s + count) return v - s;
    return -1;
  };

  // Header — Output / Input toggle pills
  const double headerPadTop = 10.0;
  const double headerY = top + headerPadTop;
  const double pillY = headerY;

  // Output pill
  const char* outGlyph = "volume_up";
  const double outPillW = 28.0 + 8.0 + 100.0;
  {
    const bool sel = u.viewOutput;
    const double px = cx;
    if (hk == HoverKey::OutputPill)
      paint_hover_highlight(cr, px, pillY, outPillW, kPillH, kPillR, mc.accentR, mc.accentG, mc.accentB);
    rounded_rect(cr, px, pillY, outPillW, kPillH, kPillR);
    if (sel)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.18);
    else
      cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.25);
    cairo_fill(cr);

    if (sel) {
      rounded_rect(cr, px, pillY, outPillW, kPillH, kPillR);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.45);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    eh::shell::draw_material_glyph(cr, px + 14.0, pillY + kPillH * 0.5, 14.0, outGlyph,
                                   sel ? mc.accentR : mc.textR,
                                   sel ? mc.accentG : mc.textG,
                                   sel ? mc.accentB : mc.textB, 0.90);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, sel ? mc.accentR : mc.textR,
                             sel ? mc.accentG : mc.textG,
                             sel ? mc.accentB : mc.textB, 0.92);
    cairo_move_to(cr, px + 28.0, pillY + kPillH * 0.5 + 5.0);
    cairo_show_text(cr, u.viewOutput ? "Output" : "Input");
  }

  // Input pill (right-aligned)
  if (u.viewOutput && hasSources) {
    const bool sel = !u.viewOutput;
    const double inPillW = 28.0 + 8.0 + 80.0;
    const double px = cx + cw - inPillW;
    if (hk == HoverKey::InputPill)
      paint_hover_highlight(cr, px, pillY, inPillW, kPillH, kPillR, mc.accentR, mc.accentG, mc.accentB);
    rounded_rect(cr, px, pillY, inPillW, kPillH, kPillR);
    if (sel)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.18);
    else
      cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.25);
    cairo_fill(cr);

    if (sel) {
      rounded_rect(cr, px, pillY, inPillW, kPillH, kPillR);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.45);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    eh::shell::draw_material_glyph(cr, px + 14.0, pillY + kPillH * 0.5, 14.0, "mic",
                                   sel ? mc.accentR : mc.textR,
                                   sel ? mc.accentG : mc.textG,
                                   sel ? mc.accentB : mc.textB, 0.90);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, sel ? mc.accentR : mc.textR,
                             sel ? mc.accentG : mc.textG,
                             sel ? mc.accentB : mc.textB, 0.92);
    cairo_move_to(cr, px + 28.0, pillY + kPillH * 0.5 + 5.0);
    cairo_show_text(cr, "Input");
  }

  // Header divider
  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, cx, headerBottom);
  cairo_line_to(cr, cx + cw, headerBottom);
  cairo_stroke(cr);

  // Device rows
  const double devRowsY = headerBottom + 4.0;

  for (int i = 0; i < devCount; ++i) {
    const auto& dev = devices[static_cast<size_t>(i)];
    const double rowY = devRowsY + static_cast<double>(i) * kRowH;

    if (hk_in_range(hk, HoverKey::DeviceRowStart, devCount) == i)
      paint_hover_highlight(cr, cx + 4.0, rowY + 3.0, cw - 8.0, kRowH - 6.0, 8.0, mc.accentR, mc.accentG, mc.accentB);

    cairo_new_path(cr);
    cairo_arc(cr, cx + kRadioR, rowY + kRowH * 0.5, kRadioR, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, dev.is_default ? mc.accentR : mc.outlineR,
                             dev.is_default ? mc.accentG : mc.outlineG,
                             dev.is_default ? mc.accentB : mc.outlineB,
                             dev.is_default ? 0.95 : 0.45);
    cairo_set_line_width(cr, dev.is_default ? 2.0 : 1.5);
    cairo_stroke(cr);

    if (dev.is_default) {
      cairo_new_path(cr);
      cairo_arc(cr, cx + kRadioR, rowY + kRowH * 0.5, kRadioInnerR, 0, 2 * M_PI);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.95);
      cairo_fill(cr);
    }

    const double iconX = cx + kRadioR * 2.0 + 8.0;
    const double iconY = rowY + kRowH * 0.5;
    eh::shell::draw_material_glyph(cr, iconX + kDevIconSz * 0.5, iconY, kDevIconSz,
                                   device_glyph(dev.name),
                                   dev.is_default ? mc.accentR : mc.textR,
                                   dev.is_default ? mc.accentG : mc.textG,
                                   dev.is_default ? mc.accentB : mc.textB,
                                   dev.is_default ? 0.92 : 0.72);

    const double nameX = iconX + kDevIconSz + 8.0;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           dev.is_default ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB,
                          dev.is_default ? 0.92 : 0.68);

    cairo_text_extents_t te;
    cairo_text_extents(cr, dev.name.c_str(), &te);
    if (te.x_advance <= kNameMaxW) {
      cairo_move_to(cr, nameX, rowY + kRowH * 0.5 + 5.0);
      cairo_show_text(cr, dev.name.c_str());
    } else {
      std::string truncated;
      for (size_t ci = 0; ci < dev.name.size(); ++ci) {
        std::string test = dev.name.substr(0, ci) + "...";
        cairo_text_extents(cr, test.c_str(), &te);
        if (te.x_advance > kNameMaxW) break;
        truncated = test;
      }
      if (truncated.empty()) truncated = dev.name.substr(0, 1) + "...";
      cairo_move_to(cr, nameX, rowY + kRowH * 0.5 + 5.0);
      cairo_show_text(cr, truncated.c_str());
    }

    const double muteX = nameX + kNameMaxW + 8.0;
    const bool muteHov = (hk_in_range(hk, HoverKey::DeviceMuteStart, devCount) == i);
    if (muteHov)
      paint_hover_highlight(cr, muteX, rowY + (kRowH - kMuteBtnSz) * 0.5, kMuteBtnSz, kMuteBtnSz, kMuteBtnSz * 0.5,
                            mc.accentR, mc.accentG, mc.accentB);
    eh::shell::draw_material_glyph(cr, muteX + kMuteBtnSz * 0.5, rowY + kRowH * 0.5, kMuteIconSz,
                                   volume_glyph(dev.volume_pct, dev.muted),
                                   dev.muted ? 0.85 : mc.textR,
                                   dev.muted ? 0.30 : mc.textG,
                                   dev.muted ? 0.30 : mc.textB,
                                   (muteHov || dev.muted) ? 0.95 : 0.80);

    const auto sg = device_slider_geom(i, devCount, headerBottom);
    const double t = std::clamp(static_cast<double>(dev.volume_pct) / 100.0, 0.0, 1.0);
    paint_slider(cr, sg.sx, sg.sy, sg.sw, t,
                 mc.drawerDimR, mc.drawerDimG, mc.drawerDimB,
                 mc.accentR, mc.accentG, mc.accentB,
                 mc.dockFillR + 0.12, mc.dockFillG + 0.12, mc.dockFillB + 0.12);
  }

  // Apps section

  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.18);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, cx, appsDividerY);
  cairo_line_to(cr, cx + cw, appsDividerY);
  cairo_stroke(cr);

  const double appsLabelY = appsDividerY + 4.0;
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 10.0);
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.50);
  cairo_move_to(cr, cx + 4.0, appsLabelY + 14.0);
  cairo_show_text(cr, "APPS");

  // App stream rows
  for (int i = 0; i < streamCount; ++i) {
    const auto& st = streams[static_cast<size_t>(i)];
    const double rowY = streamsTop + static_cast<double>(i) * kRowH;

    if (hk_in_range(hk, HoverKey::StreamRowStart, streamCount) == i)
      paint_hover_highlight(cr, cx + 2.0, rowY + 3.0, cw - 4.0, kRowH - 6.0, 8.0, mc.accentR, mc.accentG, mc.accentB);

    paint_stream_app_icon(cr, app, st, cx + kAppIconSz * 0.5, rowY + kRowH * 0.5, kAppIconSz);

    const double nameX = cx + kAppIconSz + 8.0;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.87);
    cairo_text_extents_t te;
    cairo_text_extents(cr, st.app_name.c_str(), &te);
    if (te.x_advance > kAppNameMaxW) {
      std::string truncated;
      for (size_t ci = 0; ci < st.app_name.size(); ++ci) {
        std::string test = st.app_name.substr(0, ci) + "...";
        cairo_text_extents(cr, test.c_str(), &te);
        if (te.x_advance > kAppNameMaxW) break;
        truncated = test;
      }
      if (truncated.empty()) truncated = st.app_name.substr(0, 1) + "...";
      cairo_move_to(cr, nameX, rowY + kRowH * 0.5 + 5.0);
      cairo_show_text(cr, truncated.c_str());
    } else {
      cairo_move_to(cr, nameX, rowY + kRowH * 0.5 + 5.0);
      cairo_show_text(cr, st.app_name.c_str());
    }

    const double muteX = nameX + kAppNameMaxW + 4.0;
    const bool smuteHov = (hk_in_range(hk, HoverKey::StreamMuteStart, streamCount) == i);
    if (smuteHov)
      paint_hover_highlight(cr, muteX, rowY + (kRowH - kMuteBtnSz) * 0.5, kMuteBtnSz, kMuteBtnSz, kMuteBtnSz * 0.5,
                            mc.accentR, mc.accentG, mc.accentB);
    eh::shell::draw_material_glyph(cr, muteX + kMuteBtnSz * 0.5, rowY + kRowH * 0.5, kMuteIconSz,
                                   volume_glyph(st.volume_pct, st.muted),
                                   st.muted ? 0.85 : mc.textR,
                                   st.muted ? 0.30 : mc.textG,
                                   st.muted ? 0.30 : mc.textB,
                                   (smuteHov || st.muted) ? 0.95 : 0.80);

    const auto sg = stream_slider_geom(i, headerBottom, 4.0 + devicesH + 16.0 + 1.0 + appsHeaderH);
    const double sliderT = std::clamp(static_cast<double>(st.volume_pct) / 100.0, 0.0, 1.0);
    paint_slider(cr, sg.sx, sg.sy, sg.sw, sliderT,
                 mc.drawerDimR, mc.drawerDimG, mc.drawerDimB,
                 mc.accentR, mc.accentG, mc.accentB,
                 mc.dockFillR + 0.12, mc.dockFillG + 0.12, mc.dockFillB + 0.12);

    const double pickerX = row_end_x() - kPad - kPickerBtnW - 4.0 - kEqBtnSz - 4.0 - kCloseBtnSz;
    const double pickerY = rowY + (kRowH - kPickerBtnH) * 0.5;
    const auto& aus = u.apps.find(st.node_id);
    const bool routerOpen = aus != u.apps.end() && aus->second.routerExpanded;
    const bool pickerHov = (hk_in_range(hk, HoverKey::PickerBtnStart, streamCount) == i);
    if (pickerHov)
      paint_hover_highlight(cr, pickerX, pickerY, kPickerBtnW, kPickerBtnH, 6.0, mc.accentR, mc.accentG, mc.accentB);
    {
      m3::Box pb;
      pb.setColor(static_cast<float>(mc.drawerDimR), static_cast<float>(mc.drawerDimG),
                  static_cast<float>(mc.drawerDimB), routerOpen ? 0.60f : 0.35f);
      pb.setRadius(6.0f);
      pb.setGeometry(static_cast<float>(pickerX), static_cast<float>(pickerY),
                     static_cast<float>(kPickerBtnW), static_cast<float>(kPickerBtnH));
      pb.setGlassy(true);
      pb.paint(cr);
    }

    const std::string pickerLabel = st.routed_sink_name.empty() ? "Default" : st.routed_sink_name;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.0);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.80);
    cairo_move_to(cr, pickerX + 8.0, pickerY + kPickerBtnH * 0.5 + 4.0);
    cairo_text_extents(cr, pickerLabel.c_str(), &te);
    if (te.x_advance > kPickerBtnW - 16.0) {
      std::string pt;
      for (size_t ci = 0; ci < pickerLabel.size(); ++ci) {
        std::string test = pickerLabel.substr(0, ci) + "..";
        cairo_text_extents(cr, test.c_str(), &te);
        if (te.x_advance > kPickerBtnW - 16.0) break;
        pt = test;
      }
      cairo_show_text(cr, pt.c_str());
    } else {
      cairo_show_text(cr, pickerLabel.c_str());
    }

    const double eqBtnX = pickerX + kPickerBtnW + 4.0;
    const double eqBtnY = rowY + (kRowH - kEqBtnSz) * 0.5;
    const bool eqOpen = aus != u.apps.end() && aus->second.eqExpanded;
    const bool eqHov = (hk_in_range(hk, HoverKey::EqBtnStart, streamCount) == i);
    if (eqHov)
      paint_hover_highlight(cr, eqBtnX, eqBtnY, kEqBtnSz, kEqBtnSz, kEqBtnSz * 0.5, mc.accentR, mc.accentG, mc.accentB);
    {
      m3::Box eb;
      eb.setColor(static_cast<float>(mc.drawerDimR), static_cast<float>(mc.drawerDimG),
                  static_cast<float>(mc.drawerDimB), eqOpen ? 0.60f : 0.35f);
      eb.setRadius(static_cast<float>(kEqBtnSz * 0.5));
      eb.setGeometry(static_cast<float>(eqBtnX), static_cast<float>(eqBtnY),
                     static_cast<float>(kEqBtnSz), static_cast<float>(kEqBtnSz));
      eb.setGlassy(true);
      eb.paint(cr);
    }

    const double barW = 3.0;
    const double barSpacing = 3.0;
    const double barsTotal = 3.0 * barW + 2.0 * barSpacing;
    const double barStartX = eqBtnX + (kEqBtnSz - barsTotal) * 0.5;
    const double barCenterY = eqBtnY + kEqBtnSz * 0.5;
    const double barHeights[] = {10.0, 6.0, 8.0};
    for (int b = 0; b < 3; ++b) {
      const double bx = barStartX + static_cast<double>(b) * (barW + barSpacing);
      const double bh = barHeights[b];
      rounded_rect(cr, bx, barCenterY - bh * 0.5, barW, bh, barW * 0.5);
      cairo_set_source_rgba(cr, eqOpen ? mc.accentR : mc.textR,
                               eqOpen ? mc.accentG : mc.textG,
                               eqOpen ? mc.accentB : mc.textB, 0.85);
      cairo_fill(cr);
    }

    if (eqOpen || routerOpen) {
      const double closeX = eqBtnX + kEqBtnSz + 4.0;
      const double closeY = eqBtnY;
      const bool closeHov = (hk_in_range(hk, HoverKey::CloseBtnStart, streamCount) == i);
      if (closeHov)
        paint_hover_highlight(cr, closeX, closeY, kCloseBtnSz, kCloseBtnSz, kCloseBtnSz * 0.5, 0.85, 0.30, 0.30);
      {
        m3::Box cb;
        cb.setColor(static_cast<float>(mc.drawerDimR), static_cast<float>(mc.drawerDimG),
                    static_cast<float>(mc.drawerDimB), 0.30f);
        cb.setRadius(static_cast<float>(kCloseBtnSz * 0.5));
        cb.setGeometry(static_cast<float>(closeX), static_cast<float>(closeY),
                       static_cast<float>(kCloseBtnSz), static_cast<float>(kCloseBtnSz));
        cb.setGlassy(true);
        cb.paint(cr);
      }
      eh::shell::draw_material_glyph(cr, closeX + kCloseBtnSz * 0.5, closeY + kCloseBtnSz * 0.5,
                                     14.0, "close", mc.textR, mc.textG, mc.textB, 0.72);
    }

    cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.07);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, cx + 12.0, rowY + kRowH);
    cairo_line_to(cr, cx + cw - 12.0, rowY + kRowH);
    cairo_stroke(cr);

    // EQ panel (expanded)
    if (eqOpen && aus != u.apps.end()) {
      const auto& appState = aus->second;
      const double eqY = rowY + kRowH;
      const double eqPanelH = kEqPanelPad + kEqPresetBtnH + 8.0 + kEqBandH + 4.0 + kEqPanelPad;
      const double eqX = cx;
      const double eqW = cw;

      rounded_rect(cr, eqX, eqY, eqW, eqPanelH, 8.0);
      cairo_set_source_rgba(cr, mc.panelFillR, mc.panelFillG, mc.panelFillB, 0.92);
      cairo_fill(cr);

      const double eqHeaderY = eqY + kEqPanelPad;

      const double presetX = eqX + eqW - kEqPanelPad - kEqPresetBtnW;
      rounded_rect(cr, presetX, eqHeaderY, kEqPresetBtnW, kEqPresetBtnH, 6.0);
      cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.35);
      cairo_fill(cr);

      static const char* kPresetLabels[] = {"Flat", "Bass Boost", "Treble Boost", "V-Shape", "Vocal", "Classical"};
      const int presetCount = 6;
      const char* presetLabel = "Flat";
      if (appState.eqPresetIdx >= 0 && appState.eqPresetIdx < presetCount)
        presetLabel = kPresetLabels[appState.eqPresetIdx];

      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 11.0);
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.75);
      cairo_move_to(cr, presetX + 8.0, eqHeaderY + kEqPresetBtnH * 0.5 + 4.0);
      cairo_show_text(cr, presetLabel);

      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12.0);
      cairo_set_source_rgba(cr, appState.eqEnabled ? mc.accentR : mc.textR,
                               appState.eqEnabled ? mc.accentG : mc.textG,
                               appState.eqEnabled ? mc.accentB : mc.textB, 0.85);
      cairo_move_to(cr, eqX + kEqPanelPad + 4.0, eqHeaderY + kEqPresetBtnH * 0.5 + 5.0);
      cairo_show_text(cr, "EQ");

      const double bandsY = eqHeaderY + kEqPresetBtnH + 8.0;
      const double bandsW = eqW - kEqPanelPad * 2.0;
      const double bandSpacing = bandsW / static_cast<double>(kEqBandCount);
      const double bandH = kEqBandH;

      static const char* kBandLabels[] = {"32", "64", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};

      cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.30);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, eqX + kEqPanelPad, bandsY + bandH * 0.5);
      cairo_line_to(cr, eqX + eqW - kEqPanelPad, bandsY + bandH * 0.5);
      cairo_stroke(cr);

      for (int b = 0; b < kEqBandCount; ++b) {
        const double bx = eqX + kEqPanelPad + static_cast<double>(b) * bandSpacing + bandSpacing * 0.5;
        const double bv = std::clamp(appState.eqBands[static_cast<size_t>(b)], kEqBandMinDb, kEqBandMaxDb);
        const double norm = (bv - kEqBandMinDb) / static_cast<double>(kEqBandMaxDb - kEqBandMinDb);
        const double barH = bandH * norm;

        const double eqBarW = std::max(2.0, bandSpacing * 0.3);
        if (barH > 0.5) {
          rounded_rect(cr, bx - eqBarW * 0.5, bandsY + bandH - barH, eqBarW, barH, eqBarW * 0.5);
          cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB,
                                appState.eqEnabled ? 0.75 : 0.35);
          cairo_fill(cr);
        }

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 8.0);
        cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.45);
        cairo_text_extents_t teB;
        cairo_text_extents(cr, kBandLabels[b], &teB);
        cairo_move_to(cr, bx - teB.x_advance * 0.5, bandsY + bandH + 12.0);
        cairo_show_text(cr, kBandLabels[b]);
      }
    }

    // Router panel (expanded)
    if (routerOpen) {
      const double routerY = rowY + kRowH;
      const double routerW = cw;
      const int sinkCount = static_cast<int>(snap.sinks.size());
      const double sinkRowH = 36.0;
      const double routerH = 8.0 + static_cast<double>(sinkCount) * sinkRowH + 8.0;

      rounded_rect(cr, cx, routerY, routerW, routerH, 8.0);
      cairo_set_source_rgba(cr, mc.panelFillR, mc.panelFillG, mc.panelFillB, 0.85);
      cairo_fill(cr);

      for (int si = 0; si < sinkCount; ++si) {
        const auto& sink = snap.sinks[static_cast<size_t>(si)];
        const double sinkY = routerY + 8.0 + static_cast<double>(si) * sinkRowH;
        const bool isSystemDefault = sink.is_default;
        const bool isChecked = st.routed_sink_name == sink.name || (st.routed_sink_name.empty() && isSystemDefault);

        const bool sinkHov = (app.pointerSurface == app.popupSurface &&
                              app.pointerX >= cx + 4.0 && app.pointerX <= cx + routerW - 4.0 &&
                              app.pointerY >= sinkY && app.pointerY < sinkY + sinkRowH);
        if (sinkHov)
          paint_hover_highlight(cr, cx + 4.0, sinkY + 2.0, routerW - 8.0, sinkRowH - 4.0, 6.0, mc.accentR, mc.accentG, mc.accentB);

        cairo_new_path(cr);
        cairo_arc(cr, cx + 12.0, sinkY + sinkRowH * 0.5, 8.0, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, isChecked ? mc.accentR : mc.outlineR,
                                 isChecked ? mc.accentG : mc.outlineG,
                                 isChecked ? mc.accentB : mc.outlineB,
                                 isChecked ? 0.95 : 0.45);
        cairo_set_line_width(cr, isChecked ? 2.0 : 1.5);
        cairo_stroke(cr);

        if (isChecked) {
          cairo_new_path(cr);
          cairo_arc(cr, cx + 12.0, sinkY + sinkRowH * 0.5, 4.5, 0, 2 * M_PI);
          cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.95);
          cairo_fill(cr);
        }

        eh::shell::draw_material_glyph(cr, cx + 28.0, sinkY + sinkRowH * 0.5, 16.0,
                                       device_glyph(sink.name),
                                       isChecked ? mc.accentR : mc.textR,
                                       isChecked ? mc.accentG : mc.textG,
                                       isChecked ? mc.accentB : mc.textB,
                                       isChecked ? 0.87 : 0.60);

        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               isChecked ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12.0);
        cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, isChecked ? 0.87 : 0.60);
        cairo_move_to(cr, cx + 44.0, sinkY + sinkRowH * 0.5 + 4.5);
        cairo_show_text(cr, sink.name.c_str());

        if (isSystemDefault) {
          eh::shell::draw_material_glyph(cr, row_end_x() - kPad - 8.0, sinkY + sinkRowH * 0.5, 12.0,
                                         "star", mc.textR, mc.textG, mc.textB, 0.50);
        }
      }
    }
  }
}

}
