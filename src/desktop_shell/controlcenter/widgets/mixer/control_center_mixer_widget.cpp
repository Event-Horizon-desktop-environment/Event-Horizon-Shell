#include "desktop_shell/controlcenter/widgets/mixer/control_center_mixer_widget.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint_utils.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"

#include "services/audio/pipewire_service.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace eh::shell::dock::control_center {

using paint_utils::lower_copy;

namespace {

bool is_generic_electron_icon_name(const std::string& icon_name) {
  const std::string n = lower_copy(icon_name);
  if (n.empty()) return true;
  if (n == "chromium" || n == "electron") return true;
  if (n == "chromium-browser" || n == "google-chrome" || n == "google-chrome-stable") return true;
  if (n.starts_with("electron") && n.size() > 8) return true;
  return false;
}

std::string to_lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

void enrich_stream_name_from_desktop(ControlCenterMixerStream& row) {
  if (is_generic_electron_icon_name(row.icon_name))
    row.icon_name.clear();

  StreamDesktopIds ids;
  ids.process_binary = row.process_binary;
  ids.app_id = row.app_id;
  ids.app_name = row.app_name;
  ids.node_name = row.node_name;
  ids.node_description = row.description;
  auto dei = resolve_desktop_entry_for_stream(ids);
  if (dei) {
    if (!dei->icon.empty())
      row.icon_name = dei->icon;
    if (!dei->name.empty()) {
      row.app_name = dei->name;
      return;
    }
  }

  if (!row.description.empty()) {
    row.app_name = row.description;
    return;
  }

  if (!row.app_name.empty()) {
    return;
  }

  auto candidate = [&](const std::string& s) -> std::string {
    if (s.empty()) return {};
    std::string low = to_lower_copy(s);
    low[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(low[0])));
    return low;
  };
  if (!row.process_binary.empty()) {
    std::string bin = row.process_binary;
    const auto slash = bin.find_last_of("/\\");
    if (slash != std::string::npos && slash + 1 < bin.size()) bin = bin.substr(slash + 1);
    std::string name = candidate(bin);
    if (!name.empty()) { row.app_name = name; return; }
  }
  if (!row.app_id.empty()) {
    std::string name = candidate(row.app_id);
    if (!name.empty()) { row.app_name = name; return; }
  }
  if (!row.node_name.empty()) {
    std::string name = candidate(row.node_name);
    if (!name.empty()) { row.app_name = name; return; }
  }
}

} // anonymous namespace

std::vector<ControlCenterMixerStream> control_center_mixer_streams(const ControlCenterState* cs) {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const eh::audio::Snapshot s = pw.snapshot();

  std::vector<ControlCenterMixerStream> out;
  out.reserve(s.output_streams.size());
  for (const auto& st : s.output_streams) {
    ControlCenterMixerStream row{};
    row.sink_input_id = static_cast<int>(st.node_id);
    row.volume_pct = st.volume_pct;
    row.muted = st.muted;
    row.app_name = st.app_name;
    row.icon_name = st.icon_name;
    row.app_id = st.app_id;
    row.process_binary = st.process_binary;
    row.process_path = st.process_path;
    row.node_name = st.node_name;
    row.description = st.description;
    if (row.app_name.empty()) row.app_name = "Unknown App";
    enrich_stream_name_from_desktop(row);
    if (cs && cs->mixerDragActive && !cs->mixerDragIsInput &&
        cs->mixerDragStreamId == row.sink_input_id && cs->mixerDragUiPct >= 0) {
      row.volume_pct = cs->mixerDragUiPct;
    }
    out.push_back(std::move(row));
  }
  return out;
}

std::vector<ControlCenterMixerStream> control_center_input_mixer_streams(const ControlCenterState* cs) {
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const eh::audio::Snapshot s = pw.snapshot();

  std::vector<ControlCenterMixerStream> out;
  out.reserve(s.input_streams.size());
  for (const auto& st : s.input_streams) {
    ControlCenterMixerStream row{};
    row.sink_input_id = static_cast<int>(st.node_id);
    row.volume_pct = st.volume_pct;
    row.muted = st.muted;
    row.app_name = st.app_name;
    row.icon_name = st.icon_name;
    row.app_id = st.app_id;
    row.process_binary = st.process_binary;
    row.process_path = st.process_path;
    row.node_name = st.node_name;
    row.description = st.description;
    if (row.app_name.empty()) row.app_name = "Unknown App";
    enrich_stream_name_from_desktop(row);
    if (cs && cs->mixerDragActive && cs->mixerDragIsInput &&
        cs->mixerDragStreamId == row.sink_input_id && cs->mixerDragUiPct >= 0) {
      row.volume_pct = cs->mixerDragUiPct;
    }
    out.push_back(std::move(row));
  }
  return out;
}

void control_center_set_mixer_stream_volume(int sink_input_id, double normalized_0_1) {
  if (sink_input_id <= 0) return;
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  pw.set_node_volume(static_cast<std::uint32_t>(sink_input_id), normalized_0_1);
}

void control_center_set_input_mixer_stream_volume(int source_output_id, double normalized_0_1) {
  if (source_output_id <= 0) return;
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  pw.set_node_volume(static_cast<std::uint32_t>(source_output_id), normalized_0_1);
}

} // namespace eh::shell::dock::control_center
