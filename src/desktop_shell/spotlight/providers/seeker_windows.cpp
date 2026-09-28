#include "desktop_shell/spotlight/providers/seeker_registry.hpp"

namespace eh::shell::seeker {

namespace {

void windows_query(std::string_view q, std::vector<SpotlightHit>& out) {
  std::string needle(q);
  for (char& c : needle) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  const auto& wins = staged_window_list();
  int added = 0;
  for (const auto& w : wins) {
    if (added >= 8) break;
    if (w.title.empty() && w.appId.empty()) continue;
    std::string hay = w.title + " " + w.appId;
    for (char& c : hay) {
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    if (!needle.empty() && hay.find(needle) == std::string::npos) continue;
    SpotlightHit h;
    h.name = w.title.empty() ? w.appId : w.title;
    h.genericName = "Window";
    h.seekerAction = "window:" + std::to_string(w.serial);
    h.iconKey = w.appId;
    h.score = 70;
    out.push_back(std::move(h));
    ++added;
  }
}

struct WindowsReg {
  WindowsReg() {
    Provider p;
    p.name = "windows";
    p.prefix = "win";
    p.global = false;
    p.query = windows_query;
    register_provider(std::move(p));
  }
};

WindowsReg g_windowsReg;

}
}
