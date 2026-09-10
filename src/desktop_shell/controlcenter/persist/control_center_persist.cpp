#include "desktop_shell/controlcenter/persist/control_center_persist.hpp"

#include <toml++/toml.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

#include "configuration/shell_config.hpp"

namespace eh::shell::control_center {

static std::string cc_state_dir() {
  return eh::config::state_event_horizon_dir() + "/ControlCenter";
}

static std::string cc_state_path() {
  return cc_state_dir() + "/controlcenter.toml";
}

void cc_save_sink(const std::string& sink) {
  const auto path = cc_state_path();
  std::error_code ec;
  std::filesystem::create_directories(cc_state_dir(), ec);

  toml::table root;
  if (std::filesystem::exists(path, ec)) {
    try { root = toml::parse_file(path); } catch (const toml::parse_error&) {}
  }
  root.insert_or_assign("saved_sink", sink);

  const std::string tmp = path + ".__ehtmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "# Control Center preferences\n\n";
    out << root;
    out.flush();
    if (!out.good()) { out.close(); std::filesystem::remove(tmp, ec); return; }
    out.close();
  }
  std::filesystem::rename(tmp, path, ec);
}

void cc_save_source(const std::string& source) {
  const auto path = cc_state_path();
  std::error_code ec;
  std::filesystem::create_directories(cc_state_dir(), ec);

  toml::table root;
  if (std::filesystem::exists(path, ec)) {
    try { root = toml::parse_file(path); } catch (const toml::parse_error&) {}
  }
  root.insert_or_assign("saved_source", source);

  const std::string tmp = path + ".__ehtmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "# Control Center preferences\n\n";
    out << root;
    out.flush();
    if (!out.good()) { out.close(); std::filesystem::remove(tmp, ec); return; }
    out.close();
  }
  std::filesystem::rename(tmp, path, ec);
}

std::pair<std::string, std::string> cc_load_audio_prefs() {
  const auto path = cc_state_path();
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return {};

  try {
    const toml::table root = toml::parse_file(path);
    std::string sink, source;
    if (auto* v = root["saved_sink"].as_string()) sink = v->get();
    if (auto* v = root["saved_source"].as_string()) source = v->get();
    return {sink, source};
  } catch (const toml::parse_error& e) {
    std::cerr << "[cc-persist] parse error: " << e.description() << "\n";
    return {};
  }
}

}
