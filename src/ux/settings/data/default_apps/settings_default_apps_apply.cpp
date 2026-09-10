#include "ux/settings/data/default_apps/settings_default_apps_apply.hpp"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "ux/settings/data/default_apps/settings_default_apps.hpp"

namespace eh::settings::default_apps {
namespace {

constexpr size_t kMaxDesktopIdLen = 256;

[[nodiscard]] std::string shell_quote_single(const std::string& s) {
  std::string o = "'";
  for (char c : s) {
    if (c == '\'')
      o += "'\\''";
    else
      o += c;
  }
  return o + "'";
}

[[nodiscard]] bool desktop_id_ok(std::string_view id) {
  if (id.size() < 9 || id.size() > kMaxDesktopIdLen) return false;
  if (id.substr(id.size() - 8) != ".desktop") return false;
  for (char c : id.substr(0, id.size() - 8)) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

[[nodiscard]] std::string normalize_desktop_id(std::string_view stored) {
  std::string s(stored);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) a++;
  if (a > 0) s.erase(0, a);
  if (const size_t sl = s.rfind('/'); sl != std::string::npos) s.erase(0, sl + 1);
  constexpr std::string_view suf = ".desktop";
  if (s.size() <= suf.size() || s.compare(s.size() - suf.size(), suf.size(), suf) != 0) s += suf;
  return s;
}

void run_xdg_mime_default(const std::string& desktop, const char* mime) {
  if (!desktop_id_ok(desktop) || !mime || !mime[0]) return;
  const std::string inner =
      std::string("command -v xdg-mime >/dev/null 2>&1 && xdg-mime default ") + shell_quote_single(desktop) + " " +
      mime;
  const std::string cmd = "/bin/sh -c " + shell_quote_single(inner);
  if (const int st = std::system(cmd.c_str()); st != 0)
    std::cerr << "[default-apps-apply] xdg-mime default failed status=" << st << " desktop=" << desktop
              << " mime=" << mime << '\n';
}

[[nodiscard]] std::string config_signature(const eh::config::DefaultAppsSettings& d) {
  std::string sig;
  sig.reserve(256);
  for (int i = 0; i < kNumCategories; ++i) {
    switch (i) {
      case 0: sig += d.web; break;
      case 1: sig += d.mail; break;
      case 2: sig += d.calendar; break;
      case 3: sig += d.fileManager; break;
      case 4: sig += d.terminal; break;
      case 5: sig += d.music; break;
      case 6: sig += d.video; break;
      case 7: sig += d.images; break;
      case 8: sig += d.pdf; break;
      default: break;
    }
    sig.push_back('\x1f');
  }
  return sig;
}

}

void apply_category_default(const std::string& desktop_id, int category_index) {
  if (desktop_id.empty()) return;
  const char* const* mimes = category_mime_list(category_index);
  if (!mimes) return;
  const std::string id = normalize_desktop_id(desktop_id);
  for (size_t m = 0; mimes[m] != nullptr; ++m) run_xdg_mime_default(id, mimes[m]);
}

void apply_from_config(const eh::config::DefaultAppsSettings& d) {
  static std::string last_applied;
  const std::string sig = config_signature(d);
  if (sig == last_applied) return;
  last_applied = sig;
  for (int i = 0; i < kNumCategories; ++i) {
    std::string id;
    switch (i) {
      case 0: id = d.web; break;
      case 1: id = d.mail; break;
      case 2: id = d.calendar; break;
      case 3: id = d.fileManager; break;
      case 4: id = d.terminal; break;
      case 5: id = d.music; break;
      case 6: id = d.video; break;
      case 7: id = d.images; break;
      case 8: id = d.pdf; break;
      default: continue;
    }
    if (!id.empty()) apply_category_default(id, i);
  }
}

}
