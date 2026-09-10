#include "platform/common/fs/shell_paths.hpp"

#include <cctype>
#include <cstdlib>
#include <string>

namespace eh::shell::paths {

std::string state_home_dir() {
   
  if (const char* d = std::getenv("XDG_STATE_HOME")) return d;
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/state";
  return "/tmp";
}

std::string legacy_settings_ini_path() {
  return state_home_dir() + "/event-horizon/settings.ini";
}

std::string normalize_desktop_app_id(std::string s) {
   
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (out.size() > 8 && out.ends_with(".desktop")) out.resize(out.size() - 8);
  return out.empty() ? std::string("unknown") : out;
}

}
