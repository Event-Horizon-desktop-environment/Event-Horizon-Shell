#include "desktop_shell/spotlight/providers/seeker_registry.hpp"

namespace eh::shell::seeker {

namespace {

void power_query(std::string_view q, std::vector<SpotlightHit>& out) {
  struct Row {
    const char* match;
    const char* name;
    const char* action;
    const char* icon;
  };
  static constexpr Row kRows[] = {
      {"lock", "Lock session", "power:0", "system-lock-screen"},
      {"logout", "Log out", "power:1", "system-log-out"},
      {"log out", "Log out", "power:1", "system-log-out"},
      {"exit", "Log out", "power:1", "system-log-out"},
      {"reboot", "Reboot", "power:2", "system-reboot"},
      {"restart", "Reboot", "power:2", "system-reboot"},
      {"shutdown", "Shut down", "power:3", "system-shutdown"},
      {"poweroff", "Shut down", "power:3", "system-shutdown"},
      {"power off", "Shut down", "power:3", "system-shutdown"},
  };
  std::string needle(q);
  for (char& c : needle) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  if (needle.empty()) {
    for (const auto& r : kRows) {
      if (std::string(r.match).find(' ') != std::string::npos) continue;
      SpotlightHit h;
      h.name = r.name;
      h.genericName = "Session";
      h.seekerAction = r.action;
      h.iconKey = r.icon;
      h.score = 60;
      out.push_back(std::move(h));
    }
    return;
  }
  const char* seen[4] = {nullptr, nullptr, nullptr, nullptr};
  for (const auto& r : kRows) {
    std::string hay = std::string(r.match) + " " + r.name;
    for (char& c : hay) {
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    if (hay.find(needle) == std::string::npos) continue;
    int idx = r.action[6] - '0';
    if (idx < 0 || idx > 3 || seen[idx]) continue;
    seen[idx] = r.name;
    SpotlightHit h;
    h.name = r.name;
    h.genericName = "Session";
    h.seekerAction = r.action;
    h.iconKey = r.icon;
    h.score = 60;
    out.push_back(std::move(h));
  }
}

struct PowerReg {
  PowerReg() {
    Provider p;
    p.name = "power";
    p.prefix = "power";
    p.global = false;
    p.query = power_query;
    register_provider(std::move(p));
  }
};

PowerReg g_powerReg;

}
}
