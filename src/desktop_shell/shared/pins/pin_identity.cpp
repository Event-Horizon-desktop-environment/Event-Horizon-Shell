#include "desktop_shell/shared/pins/pin_identity.hpp"

#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <vector>

using eh::shell::paths::normalize_desktop_app_id;
using eh::shell::str::file_exists;
using eh::shell::str::trim;

namespace {

struct DesktopPinParseFields {
  std::string startup_wm_class;
  std::string name;
};

std::optional<DesktopPinParseFields> read_desktop_pin_identity_fields(const std::string& desktopFilePath) {
   
  FILE* f = fopen(desktopFilePath.c_str(), "rb");
  if (!f) return std::nullopt;
  DesktopPinParseFields out{};
  char* line = nullptr;
  size_t n = 0;
  std::string section;
  while (getline(&line, &n, f) != -1) {
    std::string s(line);
    s = trim(s);
    if (s.empty() || s[0] == '#') continue;
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
      section = s.substr(1, s.size() - 2);
      continue;
    }
    if (section != "Desktop Entry") continue;
    const auto eq = s.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(s.substr(0, eq));
    const std::string val = trim(s.substr(eq + 1));
    if (key == "StartupWMClass" && out.startup_wm_class.empty()) out.startup_wm_class = val;
    else if (key == "Name" && out.name.empty()) out.name = val;
  }
  if (line) free(line);
  fclose(f);
  return out;
}

void pin_identity_push_unique(std::vector<std::string>& out, const std::string& rawToken) {
   
  const std::string k = normalize_desktop_app_id(rawToken);
  if (k.empty() || k == "unknown") return;
  for (const auto& e : out)
    if (e == k) return;
  out.push_back(k);
}

std::vector<std::string> pin_identity_compute_keys(const std::string& pinRaw) {
   
  std::vector<std::string> out;
  pin_identity_push_unique(out, pinRaw);

  if (pinRaw.size() >= 9 && pinRaw.ends_with(".desktop") && file_exists(pinRaw)) {
    if (auto pf = read_desktop_pin_identity_fields(pinRaw)) {
      if (!pf->startup_wm_class.empty()) pin_identity_push_unique(out, pf->startup_wm_class);
    }
    const size_t slash = pinRaw.rfind('/');
    const std::string baseFile = slash == std::string::npos ? pinRaw : pinRaw.substr(slash + 1);
    if (baseFile.size() > 8 && baseFile.ends_with(".desktop")) {
      pin_identity_push_unique(out, baseFile.substr(0, baseFile.size() - 8));
    }
  }

  if (auto path = find_desktop_file_for_appid(pinRaw)) {
    if (auto pf = read_desktop_pin_identity_fields(*path)) {
      if (!pf->startup_wm_class.empty()) pin_identity_push_unique(out, pf->startup_wm_class);
    }
    const size_t slash = path->rfind('/');
    const std::string baseFile = slash == std::string::npos ? *path : path->substr(slash + 1);
    if (baseFile.size() > 8 && baseFile.ends_with(".desktop")) {
      pin_identity_push_unique(out, baseFile.substr(0, baseFile.size() - 8));
    }
  }
  return out;
}

}

bool pin_identity_same_resolved_desktop(const std::string& normA, const std::string& normB) {
   
  if (normA.empty() || normB.empty() || normA == "unknown" || normB == "unknown") return false;
  if (normA == normB) return true;
  const auto pa = find_desktop_file_for_appid(normA);
  const auto pb = find_desktop_file_for_appid(normB);
  return pa.has_value() && pb.has_value() && *pa == *pb;
}

bool pin_identity_pin_raw_matches_key(const std::string& pinRaw, const std::string& keyNorm) {
   
  if (keyNorm.empty() || keyNorm == "unknown" || keyNorm == eh::shell::kSettingsAppId) return false;
  const std::string pn = normalize_desktop_app_id(pinRaw);
  if (pn.empty() || pn == "unknown" || pn == eh::shell::kSettingsAppId) return false;
  if (pin_identity_same_resolved_desktop(pn, keyNorm)) return true;
  for (const auto& k : pin_identity_compute_keys(pinRaw)) {
    if (k == keyNorm || pin_identity_same_resolved_desktop(k, keyNorm)) return true;
  }
  for (const auto& k : pin_identity_compute_keys(keyNorm)) {
    if (k == pn || pin_identity_same_resolved_desktop(k, pn)) return true;
  }
  return false;
}

std::vector<std::string> pin_identity_keys_for_raw(const std::string& pinLikeRaw) {
   
  return pin_identity_compute_keys(pinLikeRaw);
}
