// Regression test: explicitly emptied widget layouts must survive config
// load; defaults apply only when no widget keys exist at all.
//
// Drives the real load path via the public snapshot API with hermetic
// XDG_CONFIG_HOME / XDG_STATE_HOME dirs.
//
//   fresh_install_gets_defaults   no config files -> built-in layouts
//                                 (dock center contains app_drawer, panel has
//                                 workspaces/clock)
//   explicit_empty_stays_empty    all widget lists [] -> all stay empty
//   partial_config                dock keys present (empty) + panel absent ->
//                                 dock stays empty, panel gets defaults

#include "configuration/shell_config.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

static std::string g_cfg;
static std::string g_state;

static void setup_env() {
  char tmpl[] = "/tmp/eh-widget-defaults-test-XXXXXX";
  if (!::mkdtemp(tmpl)) {
    fprintf(stderr, "FAIL: mkdtemp\n");
    std::exit(1);
  }
  g_cfg = std::string(tmpl) + "/cfg";
  g_state = std::string(tmpl) + "/state";
  fs::create_directories(g_cfg + "/event-horizon");
  fs::create_directories(g_state + "/event-horizon");
  ::setenv("XDG_CONFIG_HOME", g_cfg.c_str(), 1);
  ::setenv("XDG_STATE_HOME", g_state.c_str(), 1);
}

static void write_state_toml(const std::string& body) {
  std::ofstream f(g_state + "/event-horizon/settings.toml", std::ios::trunc);
  f << body;
  f.close();
}

static void clear_state_toml() {
  std::error_code ec;
  fs::remove(g_state + "/event-horizon/settings.toml", ec);
}

static eh::config::ShellConfig fresh_snapshot() {
  eh::config::shell_config_invalidate();
  return eh::config::shell_config_snapshot_skip_matugen();
}

int main() {
  setup_env();

  // A. Fresh install: no files -> defaults.
  {
    clear_state_toml();
    const eh::config::ShellConfig c = fresh_snapshot();
    CHECK(!c.dock.leftWidgets.empty());
    bool hasDrawer = false;
    for (const auto& w : c.dock.centerWidgets)
      if (w == "app_drawer") hasDrawer = true;
    CHECK(!hasDrawer);
    CHECK(c.dock.centerWidgets ==
          (std::vector<std::string>{"pinned_apps", "running_apps", "trash"}));
    CHECK(c.taskbar.leftWidgets == (std::vector<std::string>{"smenu", "win7_tasks"}));
    CHECK(c.taskbar.centerWidgets.empty());
    CHECK(c.taskbar.rightWidgets ==
          (std::vector<std::string>{"tray", "weather", "clock", "pear_center", "settings_button"}));
    CHECK(c.taskbar.height == 48);
    CHECK(c.taskbar.radius == 0);
    CHECK(c.taskbar.opacity == 65);
    CHECK(c.taskbar.iconSize == 38);
    CHECK(c.taskbar.widthMode == 1);
    CHECK(c.taskbar.showLabels == true);
    CHECK(c.panel.leftWidgets == (std::vector<std::string>{"workspaces"}));
  }

  // B. Explicitly emptied everything -> stays empty.
  {
    write_state_toml(
        "[dock]\n"
        "left_widgets = []\n"
        "center_widgets = []\n"
        "right_widgets = []\n"
        "[taskbar]\n"
        "left_widgets = []\n"
        "center_widgets = []\n"
        "right_widgets = []\n"
        "[panel]\n"
        "left_widgets = []\n"
        "center_widgets = []\n"
        "right_widgets = []\n");
    const eh::config::ShellConfig c = fresh_snapshot();
    CHECK(c.dock.leftWidgets.empty());
    CHECK(c.dock.centerWidgets.empty());
    CHECK(c.dock.rightWidgets.empty());
    CHECK(c.taskbar.leftWidgets.empty());
    CHECK(c.taskbar.centerWidgets.empty());
    CHECK(c.taskbar.rightWidgets.empty());
    CHECK(c.panel.leftWidgets.empty());
    CHECK(c.panel.centerWidgets.empty());
    CHECK(c.panel.rightWidgets.empty());
  }

  // C. Mixed: dock emptied on purpose, panel untouched -> dock empty,
  // panel defaults.
  {
    write_state_toml(
        "[dock]\n"
        "left_widgets = []\n"
        "center_widgets = []\n"
        "right_widgets = []\n");
    const eh::config::ShellConfig c = fresh_snapshot();
    CHECK(c.dock.leftWidgets.empty());
    CHECK(c.dock.centerWidgets.empty());
    CHECK(c.dock.rightWidgets.empty());
    CHECK(c.panel.leftWidgets == (std::vector<std::string>{"workspaces"}));
    CHECK(c.panel.centerWidgets == (std::vector<std::string>{"clock"}));
  }

  // Cleanup.
  clear_state_toml();

  if (g_failures == 0) printf("widget_defaults: all tests passed\n");
  return g_failures == 0 ? 0 : 1;
}
