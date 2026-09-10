#include "desktop_shell/controlcenter/widgets/audio/control_center_audio_widget.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint_utils.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"
#include "desktop_shell/ui/slider/ui_slider.hpp"

#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "services/audio/pipewire_service.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include <cairo/cairo.h>

namespace {

std::string trim_copy(const std::string& s) {
  const auto* p = s.data();
  const auto* pe = p + s.size();
  while (p != pe && std::isspace(static_cast<unsigned char>(*p))) ++p;
  while (pe != p && std::isspace(static_cast<unsigned char>(*(pe - 1)))) --pe;
  return std::string(p, pe);
}

} // anonymous namespace

namespace eh::shell::dock::control_center {

using paint_utils::rrect;
using paint_utils::cc_paint_glass_card_mc;
using paint_utils::truncate_to_width;
using paint_utils::kSurfR;
using paint_utils::kSurfG;
using paint_utils::kSurfB;

ControlCenterAudioOutputState control_center_audio_output_state(const ControlCenterState* cs) {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const eh::audio::Snapshot s = pw.snapshot();

  ControlCenterAudioOutputState out{};
  out.device_name = "Default Sink";
  out.muted = false;
  out.volume_pct = 100;
  for (const auto& d : s.sinks) {
    if (d.is_default) {
      out.device_name = d.display_name.empty() ? d.name : d.display_name;
      out.muted = d.muted;
      out.volume_pct = d.volume_pct;
      break;
    }
  }
  if (cs && cs->audioDragActive && cs->audioDragVisualT >= 0.0) {
    out.volume_fill_t_override = std::clamp(cs->audioDragVisualT, 0.0, 1.0);
  }
  return out;
}

std::vector<ControlCenterOutputDevice> control_center_output_devices() {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const eh::audio::Snapshot s = pw.snapshot();

  std::vector<ControlCenterOutputDevice> out;
  out.reserve(s.sinks.size());
  for (const auto& d : s.sinks) {
    ControlCenterOutputDevice row{};
    row.sink_name = std::to_string(d.node_id);
    row.display_name = d.display_name.empty() ? d.name : d.display_name;
    row.is_default = d.is_default;
    if (!row.sink_name.empty()) out.push_back(std::move(row));
  }
  return out;
}

std::vector<ControlCenterInputDevice> control_center_input_devices() {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const eh::audio::Snapshot s = pw.snapshot();

  std::vector<ControlCenterInputDevice> out;
  out.reserve(s.sources.size());
  for (const auto& d : s.sources) {
    ControlCenterInputDevice row{};
    row.source_name = std::to_string(d.node_id);
    row.display_name = d.display_name.empty() ? d.name : d.display_name;
    row.is_default = d.is_default;
    if (!row.source_name.empty()) out.push_back(std::move(row));
  }
  return out;
}

void control_center_set_audio_output_volume(double normalized_0_1) {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const eh::audio::Snapshot s = pw.snapshot();
  if (s.default_sink != 0) pw.set_node_volume(s.default_sink, normalized_0_1);
}

void control_center_set_audio_output_mute(bool muted) {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const eh::audio::Snapshot s = pw.snapshot();
  if (s.default_sink != 0) pw.set_node_mute(s.default_sink, muted);
}

void control_center_set_default_sink(std::string_view sink_name) {
  const std::string s = trim_copy(std::string(sink_name));
  if (s.empty()) return;
  const int id = std::atoi(s.c_str());
  if (id <= 0) return;
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  pw.set_default_sink(static_cast<std::uint32_t>(id));
}

ControlCenterAudioInputState control_center_audio_input_state(const ControlCenterState* cs) {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const eh::audio::Snapshot s = pw.snapshot();

  ControlCenterAudioInputState out{};
  out.device_name = "Default Source";
  out.muted = false;
  out.volume_pct = 100;
  for (const auto& d : s.sources) {
    if (d.is_default) {
      out.device_name = d.display_name.empty() ? d.name : d.display_name;
      out.muted = d.muted;
      out.volume_pct = d.volume_pct;
      break;
    }
  }
  if (cs && cs->inputDragActive && cs->inputDragVisualT >= 0.0) {
    out.volume_fill_t_override = std::clamp(cs->inputDragVisualT, 0.0, 1.0);
  }
  return out;
}

void control_center_set_audio_input_volume(double normalized_0_1) {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const eh::audio::Snapshot s = pw.snapshot();
  if (s.default_source != 0) pw.set_node_volume(s.default_source, normalized_0_1);
}

void control_center_set_audio_input_mute(bool muted) {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const eh::audio::Snapshot s = pw.snapshot();
  if (s.default_source != 0) pw.set_node_mute(s.default_source, muted);
}

void control_center_set_default_source(std::string_view source_name) {
  const std::string s = trim_copy(std::string(source_name));
  if (s.empty()) return;
  const int id = std::atoi(s.c_str());
  if (id <= 0) return;
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  pw.set_default_source(static_cast<std::uint32_t>(id));
}

void paint_control_center_audio_output_card(cairo_t* cr, double x, double y, double w, double h,
                                            const ControlCenterAudioOutputState& as, double inner_glass_scale) {
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  const double s = std::clamp(inner_glass_scale, 0.0, 1.0);
  const double r = std::max(14.0, std::min(w, h) * 0.16);
  cc_paint_glass_card_mc(cr, x, y, w, h, r, s, mc);

  const double icx = x + 24.0;
  const double icy = y + 28.0;
  cairo_save(cr);
  cairo_arc(cr, icx, icy, 14.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, !as.muted ? 0.18 * s : 0.08 * s);
  cairo_fill(cr);
  cairo_restore(cr);

  const int volGlyph = (as.volume_fill_t_override >= 0.0 && as.volume_fill_t_override <= 1.0)
                           ? static_cast<int>(std::lround(std::clamp(as.volume_fill_t_override, 0.0, 1.0) * 100.0))
                           : as.volume_pct;
  const char* glyph =
      (as.muted || volGlyph <= 0) ? "volume_off" : (volGlyph < 34 ? "volume_down" : "volume_up");
  const double igr = as.muted ? 0.65 : mc.accentR;
  const double igg = as.muted ? 0.68 : mc.accentG;
  const double igb = as.muted ? 0.72 : mc.accentB;
  eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0, glyph, igr, igg, igb, 0.96);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 14.0);
  cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, 0.95);
  cairo_move_to(cr, x + 48.0, y + 30.0);
  const double kNameMaxW = w - 78.0;
  std::string nameTrunc;
  truncate_to_width(cr, as.device_name, kNameMaxW, &nameTrunc);
  cairo_show_text(cr, nameTrunc.c_str());

  namespace cc = eh::shell::cc_slider;
  const float t = static_cast<float>(
      (as.volume_fill_t_override >= 0.0 && as.volume_fill_t_override <= 1.0)
          ? std::clamp(as.volume_fill_t_override, 0.0, 1.0)
          : std::clamp(static_cast<double>(as.volume_pct) / 100.0, 0.0, 1.0));
  const double as_h = 38.0;
  eh::ui::Slider sl;
  sl.setRange(0.0f, 1.0f);
  sl.setStep(0.01f);
  sl.setValue(t);
  sl.setGeometry(static_cast<float>(x + 10.0),
                 static_cast<float>((y + cc::kAudioTrackYFromCardTop) - as_h * 0.5),
                 static_cast<float>(w - 20.0),
                 static_cast<float>(as_h));
  sl.setAccentColor(static_cast<float>(mc.accentR), static_cast<float>(mc.accentG), static_cast<float>(mc.accentB));
  sl.setTrackColor(static_cast<float>(mc.drawerDimR), static_cast<float>(mc.drawerDimG), static_cast<float>(mc.drawerDimB));
  sl.paint(cr);
}

void paint_control_center_audio_input_card(cairo_t* cr, double x, double y, double w, double h,
                                           const ControlCenterAudioInputState& as, double inner_glass_scale) {
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  const double s = std::clamp(inner_glass_scale, 0.0, 1.0);
  const double r = std::max(14.0, std::min(w, h) * 0.16);
  cc_paint_glass_card_mc(cr, x, y, w, h, r, s, mc);

  const double icx = x + 24.0;
  const double icy = y + 28.0;
  cairo_save(cr);
  cairo_arc(cr, icx, icy, 14.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, !as.muted ? 0.18 * s : 0.08 * s);
  cairo_fill(cr);
  cairo_restore(cr);

  const char* glyph = as.muted ? "mic_off" : "mic";
  const double igr = as.muted ? 0.65 : mc.accentR;
  const double igg = as.muted ? 0.68 : mc.accentG;
  const double igb = as.muted ? 0.72 : mc.accentB;
  eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0, glyph, igr, igg, igb, 0.96);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 14.0);
  cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, 0.95);
  cairo_move_to(cr, x + 48.0, y + 30.0);
  {
    const double kNameMaxW = w - 78.0;
    std::string nameTrunc;
    truncate_to_width(cr, as.device_name, kNameMaxW, &nameTrunc);
    cairo_show_text(cr, nameTrunc.c_str());
  }

  namespace cc = eh::shell::cc_slider;
  const float t = static_cast<float>(
      (as.volume_fill_t_override >= 0.0 && as.volume_fill_t_override <= 1.0)
          ? std::clamp(as.volume_fill_t_override, 0.0, 1.0)
          : std::clamp(static_cast<double>(as.volume_pct) / 100.0, 0.0, 1.0));
  const double as_h = 38.0;
  eh::ui::Slider sl;
  sl.setRange(0.0f, 1.0f);
  sl.setStep(0.01f);
  sl.setValue(t);
  sl.setGeometry(static_cast<float>(x + 10.0),
                 static_cast<float>((y + cc::kAudioTrackYFromCardTop) - as_h * 0.5),
                 static_cast<float>(w - 20.0),
                 static_cast<float>(as_h));
  sl.setAccentColor(static_cast<float>(mc.accentR), static_cast<float>(mc.accentG), static_cast<float>(mc.accentB));
  sl.setTrackColor(static_cast<float>(mc.drawerDimR), static_cast<float>(mc.drawerDimG), static_cast<float>(mc.drawerDimB));
  sl.paint(cr);
}

} // namespace eh::shell::dock::control_center
