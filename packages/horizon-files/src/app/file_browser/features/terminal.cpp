#include "../app.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "config/shell_config.hpp"
#include "platform/desktop/entries/desktop_xdg_ops.hpp"
#include "platform/widgets/app_drawer/list/desktop_list.hpp"

namespace xdg = eh::shell::desktop::xdg;

namespace eh::file_browser {

static std::string sh_quote(const std::string& s) {
  std::string q = "'";
  for (char c : s) {
    if (c == '\'') q += "'\\''";
    else q += c;
  }
  q += '\'';
  return q;
}

// ── terminal helpers ─────────────────────────────────────────────

void scan_terminal_apps(AppState& app) {
  app.term_chooser_apps.clear();
  auto entries = eh::app_drawer::copy_desktop_entries();
  for (const auto& e : entries) {
    if (e.noDisplay || e.hidden) continue;
    bool is_term = false;
    std::string cats_lower = e.categories;
    for (char& ch : cats_lower)
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::string cat_check = ";" + cats_lower + ";";
    if (cat_check.find(";terminalemulator;") != std::string::npos)
      is_term = true;
    if (!is_term && e.mimeTypesLower.find("application/x-terminal-emulator") != std::string::npos)
      is_term = true;
    if (!is_term) continue;

    AppState::TerminalApp ta;
    ta.desktop_id = e.path;
    ta.name = e.name.empty() ? e.path : e.name;
    ta.exec = e.exec;
    app.term_chooser_apps.push_back(std::move(ta));
  }
}

// ── terminal workdir flags ───────────────────────────────────────
//
// Many terminals (Konsole, GNOME Terminal, Xfce4 Terminal) ignore the
// inherited working directory and always start in $HOME.  We need to
// pass an explicit flag.  This table mirrors what KDE's
// KTerminalLauncherJob uses internally.

struct TerminalInfo {
  const char* stem;
  const char* flag; // nullptr = inherits CWD, no flag needed
};

static const TerminalInfo kKnownTerminals[] = {
  {"org.kde.konsole",        "--workdir"},
  {"konsole",                "--workdir"},
  {"gnome-terminal",         "--working-directory"},
  {"gnome-terminal.wrapper", "--working-directory"},
  {"xfce4-terminal",         "--working-directory"},
  {"lxterminal",             "--working-directory"},
  {"terminator",             "--working-directory"},
  {"tilix",                  "--working-directory"},
  {"qterminal",              "--workdir"},
  {"kitty",                  "--directory"},
  {"deepin-terminal",        "--workdir"},
  {"cool-retro-term",        "--workdir"},

  // Terminals that honour inherited CWD — no flag needed.
  {"alacritty",              nullptr},
  {"foot",                   nullptr},
  {"st",                     nullptr},
  {"urxvt",                  nullptr},
  {"rxvt",                   nullptr},
  {"xterm",                  nullptr},
  {"sakura",                 nullptr},
};

static const char* terminal_workdir_flag(const std::string& desktop_id) {
  std::string stem = desktop_id;
  auto slash = stem.rfind('/');
  if (slash != std::string::npos) stem = stem.substr(slash + 1);
  auto dot = stem.rfind('.');
  if (dot != std::string::npos) stem = stem.substr(0, dot);

  for (const auto& t : kKnownTerminals) {
    if (stem == t.stem) return t.flag;
  }
  return nullptr; // unknown → rely on cd
}

// Remove %-placeholders from a desktop-file Exec line.
static std::string clean_exec_line(const std::string& exec_line) {
  std::string cleaned;
  for (size_t j = 0; j < exec_line.size(); ++j) {
    if (exec_line[j] == '%' && j + 1 < exec_line.size()) {
      if (exec_line[j + 1] == '%') {
        cleaned += '%';
        ++j;
      }
      ++j;
      continue;
    }
    cleaned += exec_line[j];
  }
  // Trim leading spaces.
  while (!cleaned.empty() && cleaned[0] == ' ') cleaned.erase(0, 1);
  return cleaned;
}

void open_terminal_at(AppState& app, const std::string& dir) {
  const auto& sc = eh::config::shell_config_snapshot();
  std::string term_id = sc.defaultApps.terminal;

  if (term_id.empty()) {
    scan_terminal_apps(app);
    if (app.term_chooser_apps.empty()) return;
    app.term_chooser_open = true;
    app.term_chooser_target_dir = dir;
    app.term_chooser_hover = -1;
    app.term_chooser_scroll = 0;
    app.term_chooser_w = 400;
    app.term_chooser_x = (app.width - app.term_chooser_w) / 2;
    return;
  }

  scan_terminal_apps(app);
  bool found = false;
  for (const auto& ta : app.term_chooser_apps) {
    std::string stem = ta.desktop_id;
    auto slash = stem.rfind('/');
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    auto dot = stem.rfind('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    if (stem == term_id || ta.desktop_id == term_id || ta.desktop_id == term_id + ".desktop") {
      std::string cleaned = clean_exec_line(ta.exec);
      const char* wd_flag = terminal_workdir_flag(ta.desktop_id);

      std::string full;
      if (wd_flag) {
        // Known terminal – pass directory via explicit flag.
        full = cleaned + " " + wd_flag + " " + sh_quote(dir) + " >/dev/null 2>&1";
      } else {
        // Unknown terminal / inherits CWD – cd first, then launch.
        full = "cd " + sh_quote(dir) + " && " + cleaned + " >/dev/null 2>&1";
      }
      xdg::spawn_sh_lc_detached(full);
      found = true;
      break;
    }
  }
  if (!found) {
    // No .desktop entry matched – try term_id as a raw command name.
    const char* wd_flag = terminal_workdir_flag(term_id);
    std::string full;
    if (wd_flag) {
      full = term_id + " " + wd_flag + " " + sh_quote(dir) + " >/dev/null 2>&1";
    } else {
      full = "cd " + sh_quote(dir) + " && " + term_id + " >/dev/null 2>&1";
    }
    xdg::spawn_sh_lc_detached(full);
  }
}

} // namespace eh::file_browser
