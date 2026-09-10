#include "platform/desktop/entries/desktop_xdg_ops.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace eh::shell::desktop::xdg {

std::string canonical_absolute_path(const std::string& path) { return path; }

std::string file_uri_for_path(const std::string& abs_path) {
  return std::string("file://") + abs_path;
}

std::string expand_desktop_exec_tokens(std::string exec, const std::string&, const std::string&) {
  return exec;
}

void spawn_sh_lc_detached(const std::string& script) {
  std::system(("sh -c '" + script + "' &").c_str());
}

void open_path_in_default_application(const std::string& abs_path) {
  std::string cmd = "xdg-open " + abs_path + " &";
  std::system(cmd.c_str());
}

void open_uri(const std::string& uri) {
  std::string cmd = "xdg-open '" + uri + "' &";
  std::system(cmd.c_str());
}

bool dbus_filemanager_show_items_select_uri(const std::string&) { return false; }

void open_properties_for_desktop_file(const std::string&) {}

void open_file_location(const std::string&, const DesktopEntryInfo*) {}

void clipboard_files_cut_copy(bool, const std::string&) {}

void clipboard_files_cut_copy_multi(bool, const std::vector<std::string>&) {}

bool clipboard_paste_into_directory(const std::string&, std::vector<std::string>*, bool*) {
  return false;
}

bool trash_file(const std::string& abs_path) {
  std::string cmd = "gio trash '" + abs_path + "' 2>/dev/null";
  return std::system(cmd.c_str()) == 0;
}

void launch_expanded_exec_line(const std::string& expanded_exec, bool) {
  std::system((expanded_exec + " &").c_str());
}

void launch_pkexec_exec_raw(const std::string&, const std::string&, const std::string&, bool) {}

void open_desktop_default(const std::string&, const std::optional<DesktopEntryInfo>&,
                           const std::string&) {}

void launch_action_exec(const std::string&, const std::string&, const std::string&, bool) {}

std::string prompt_rename_text(const std::string&, const std::string&) { return {}; }

}
