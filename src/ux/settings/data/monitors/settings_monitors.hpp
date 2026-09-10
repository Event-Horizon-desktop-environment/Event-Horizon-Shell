#pragma once

#include "desktop_shell/unified/compositor_kind.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eh::settings_monitors {

struct OutputCaps {
  std::vector<std::string> resolutions;

  std::unordered_map<std::string, std::vector<double>> resolution_refresh_hz;
  std::vector<double> all_refresh_hz;
  bool vrr_capable = false;
  bool hdr_hint = false;
  std::string make;
  std::string model;
  std::string description;
  int width = 0;
  int height = 0;
  double current_hz = 0;
};

struct MonitorRow {
  std::string name;
  std::string resolution;
  std::string refresh_rate;
  std::string position;
  std::string scale = "1";

  std::string transform = "0";
  bool disabled = false;
  std::string bitdepth;
  std::string cm;
  std::string icc;

  std::string vrr;

  std::string mirror;
  std::string sdrbrightness;
  std::string sdrsaturation;
  std::string sdr_eotf;
  std::string supports_hdr;
  std::string supports_wide_color;
  std::string sdr_min_luminance;
  std::string sdr_max_luminance;
  std::string min_luminance;
  std::string max_luminance;
  std::string max_avg_luminance;
};

struct MonitorsTabState {
  CompositorKind kind = CompositorKind::Unknown;
  std::vector<MonitorRow> outputs;
  std::unordered_map<std::string, OutputCaps> caps;
  std::string conf_path;

  std::string preamble;
  std::string status;
  int scroll_px = 0;

  int selected_output = 0;
  bool dirty = false;

  std::vector<MonitorRow> baseline_outputs;
  std::string baseline_preamble;

  void refresh_from_system();

  bool save_and_reload(std::string& err_out);

  void revert_edits();

  void capture_baseline();

  [[nodiscard]] bool supports_color_hdr_ui() const {
    return kind == CompositorKind::Hyprland;
  }

  [[nodiscard]] bool supports_bitdepth_ui() const {
    return kind == CompositorKind::Hyprland || kind == CompositorKind::Niri;
  }
};

}
