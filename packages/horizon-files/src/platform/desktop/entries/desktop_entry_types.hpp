#pragma once

#include <string>
#include <vector>

struct DesktopAction {
  std::string id;
  std::string name;
  std::string exec;
  std::string icon;
};

struct DesktopEntryInfo {

  std::string type = "application";
  std::string name;
  std::string icon;
  std::string exec;

  std::string url;
  bool dbus_activatable = false;
  bool terminal = false;
  std::string startup_wm_class;
  std::vector<DesktopAction> actions;

  // ── Autostart / desktop entry spec fields (populated by read_desktop_entry_info) ──
  bool hidden = false;
  bool noDisplay = false;
  std::vector<std::string> onlyShowIn;
  std::vector<std::string> notShowIn;
  std::string tryExec;
  bool autostartEnabled = true;   // X-GNOME-Autostart-enabled (defaults true)
  int autostartDelaySec = 0;      // X-GNOME-Autostart-Delay or similar
};
