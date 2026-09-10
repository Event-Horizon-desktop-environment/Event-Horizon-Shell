#include "desktop_shell/controlcenter/mixer/mixer_stream_icon_resolve.hpp"

#include "desktop_shell/desktop/entries/desktop_entries.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace eh::shell::mixer_icon {
namespace {

std::string basename_path(std::string p) {
   
  while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
  const auto slash = p.find_last_of("/\\");
  if (slash != std::string::npos && slash + 1 < p.size()) return p.substr(slash + 1);
  return p;
}

std::string last_dot_segment(const std::string& id) {
   
  const auto pos = id.find_last_of('.');
  if (pos == std::string::npos || pos + 1 >= id.size()) return {};
  return id.substr(pos + 1);
}

std::string lower_copy(std::string s) {
   
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string app_name_icon_slug(const std::string& name) {
   
  std::string out;
  out.reserve(name.size());
  for (unsigned char uc : name) {
    if (std::isspace(uc)) out.push_back('-');
    else out.push_back(static_cast<char>(std::tolower(uc)));
  }
  return out;
}

std::string first_alpha_token_lower(std::string_view sv) {
   
  std::string tok;
  for (char ch : sv) {
    const unsigned char uc = static_cast<unsigned char>(ch);
    if (std::isalnum(uc) || ch == '.' || ch == '-' || ch == '_') tok.push_back(static_cast<char>(std::tolower(uc)));
    else if (!tok.empty()) break;
  }
  return tok;
}

void append_unique_hint(std::vector<std::string>& v, std::string s) {
   
  if (s.empty()) return;
  if (std::find(v.begin(), v.end(), s) != v.end()) return;
  v.push_back(std::move(s));
}

std::vector<std::string> mixer_theme_hint_keys(const StreamIconIds& s) {
    
  std::vector<std::string> hints;
  const std::string bin = basename_path(s.process_binary);
  const std::string bl = lower_copy(bin);
  const std::string an = lower_copy(s.app_name);
  const std::string pb = s.process_binary.empty() ? basename_path(s.process_path) : bin;
  const std::string pbl = lower_copy(pb);

  if (pbl == "msedge" || pbl.starts_with("msedge") || an.find("microsoft edge") != std::string::npos) {
    append_unique_hint(hints, "com.microsoft.Edge");
    append_unique_hint(hints, "microsoft-edge");
    append_unique_hint(hints, "microsoft-edge-stable");
    append_unique_hint(hints, "msedge");
  }

  if (an.find("runescape") != std::string::npos || an.find("jagex") != std::string::npos ||
      pbl.find("runescape") != std::string::npos || pbl.find("jagex") != std::string::npos) {
    append_unique_hint(hints, "com.jagex.JagexLauncher");
    append_unique_hint(hints, "jagexlauncher");
    append_unique_hint(hints, "runescape");
    append_unique_hint(hints, "oldschool");
  }

  if (pbl == "cava" || an == "cava") {
    append_unique_hint(hints, "cava");
    append_unique_hint(hints, "audio-card");
    append_unique_hint(hints, "multimedia-volume-control");
  }

  if (pbl.find("speech-dispatcher") != std::string::npos || an.find("speech-dispatcher") != std::string::npos) {
    append_unique_hint(hints, "audio-headphones");
    append_unique_hint(hints, "preferences-desktop-sound");
  }

  return hints;
}

}  // namespace

const eh::icons::IconEntry* resolve_mixer_stream_theme_icon(eh::icons::IconCache& icons, const StreamIconIds& s) {
   
  const eh::icons::IconEntry* icon = nullptr;

  auto try_tray = [&](const std::string& k) {
    if (icon && icon->surface) return;
    if (k.empty()) return;
    const eh::icons::IconEntry* e = icons.tray_icon(k);
    if (e && e->surface) icon = e;
  };
  auto try_app = [&](const std::string& k) {
    if (icon && icon->surface) return;
    if (k.empty()) return;
    const eh::icons::IconEntry* e = icons.app_icon(k);
    if (e && e->surface) icon = e;
  };

  if (!s.icon_name.empty()) {
    if (s.icon_name[0] == '/') {
      try_tray(s.icon_name);
    } else {
      if (s.icon_name.find('.') != std::string::npos) try_app(s.icon_name);
      try_tray(s.icon_name);
      try_tray(lower_copy(s.icon_name));
    }
  }

  if (!s.app_id.empty()) {
    try_app(s.app_id);
    std::string app_id_hyphens = s.app_id;
    for (char& c : app_id_hyphens)
      if (c == '_') c = '-';
    if (app_id_hyphens != s.app_id) {
      try_app(app_id_hyphens);
      try_tray(lower_copy(app_id_hyphens));
    }
    const std::string seg = last_dot_segment(s.app_id);
    if (!seg.empty()) {
      try_app(seg);
      try_tray(lower_copy(seg));
    }
    try_tray(s.app_id);
    try_tray(lower_copy(s.app_id));
  }

  const std::string bin = basename_path(s.process_binary);
  const std::string bin_lower = lower_copy(bin);
  const bool bin_is_wrapper = (bin_lower == "bwrap" || bin_lower == "flatpak-bwrap" || bin_lower == "fuse-overlayfs" ||
                                bin_lower == "snap-confine" || bin_lower == "xdg-dbus-proxy");
  if (!bin.empty() && !bin_is_wrapper) {
    try_app(bin);
    try_tray(bin);
    try_tray(bin_lower);
  }

  if (!(icon && icon->surface) && !bin.empty() && !bin_is_wrapper) {
    const eh::icons::IconEntry* e = icons.app_icon_from_exec_basename(bin);
    if (e && e->surface) icon = e;
  }

  // Fallback: try process_path basename if process_binary was empty
  if (!(icon && icon->surface) && bin.empty()) {
    const std::string path_bin = basename_path(s.process_path);
    const std::string path_bin_lower = lower_copy(path_bin);
    if (!path_bin.empty() && !is_generic_runtime_binary(path_bin_lower)) {
      try_app(path_bin);
      try_tray(path_bin);
      try_tray(path_bin_lower);
      if (!(icon && icon->surface)) {
        const eh::icons::IconEntry* e = icons.app_icon_from_exec_basename(path_bin);
        if (e && e->surface) icon = e;
      }
    }
  }

  for (const std::string& h : mixer_theme_hint_keys(s)) {
    // unguarded — hints should override fallback icons from earlier steps
    if (!h.empty()) {
      const eh::icons::IconEntry* e = icons.app_icon(h);
      if (e && e->surface) icon = e;
      e = icons.tray_icon(h);
      if (e && e->surface) icon = e;
    }
  }

  if (!s.app_name.empty() && s.app_name != "Unknown App") {
    try_app(s.app_name);
    try_tray(app_name_icon_slug(s.app_name));
    const std::string tok = first_alpha_token_lower(s.app_name);
    if (tok.size() >= 3 && tok != bin_lower) {
      try_app(tok);
      try_tray(tok);
    }
  }

  // Desktop entry Icon= resolution (cross-reference .desktop files)
  if (!(icon && icon->surface)) {
    StreamDesktopIds ids;
    ids.process_binary = s.process_binary;
    ids.app_id = s.app_id;
    ids.app_name = s.app_name;
    ids.node_name = {};
    ids.node_description = {};
    auto dei = resolve_desktop_entry_for_stream(ids);
    if (dei && !dei->icon.empty()) {
      try_app(dei->icon);
      try_tray(dei->icon);
      try_tray(lower_copy(dei->icon));
    }
  }

  return icon;
}

}  // namespace eh::shell::mixer_icon
