#pragma once

#include "platform/desktop/entries/desktop_entry_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace eh::shell::desktop::xdg {

[[nodiscard]] std::string canonical_absolute_path(const std::string& path);
[[nodiscard]] std::string file_uri_for_path(const std::string& abs_path);

[[nodiscard]] std::string expand_desktop_exec_tokens(std::string exec, const std::string& desktop_abs_path,
                                                      const std::string& display_name);

void spawn_sh_lc_detached(const std::string& script);

void open_path_in_default_application(const std::string& abs_path);
void open_uri(const std::string& uri);

[[nodiscard]] bool dbus_filemanager_show_items_select_uri(const std::string& file_uri);

void open_properties_for_desktop_file(const std::string& desktop_abs_path);

void open_file_location(const std::string& desktop_abs_path, const DesktopEntryInfo* entry);

void clipboard_files_cut_copy(bool cut, const std::string& abs_path);

void clipboard_files_cut_copy_multi(bool cut, const std::vector<std::string>& abs_paths);

[[nodiscard]] bool clipboard_paste_into_directory(const std::string& dest_dir_abs,
                                                  std::vector<std::string>* out_created_paths = nullptr,
                                                  bool* out_was_cut = nullptr);

[[nodiscard]] bool trash_file(const std::string& abs_path);

void launch_expanded_exec_line(const std::string& expanded_exec, bool wrap_terminal);

void launch_pkexec_exec_raw(const std::string& exec_raw, const std::string& desktop_abs_path, const std::string& display_name,
                            bool terminal);

void open_desktop_default(const std::string& desktop_abs_path, const std::optional<DesktopEntryInfo>& entry,
                          const std::string& fallback_exec_line_from_icon_cache);

void launch_action_exec(const std::string& action_exec_raw, const std::string& desktop_abs_path, const std::string& display_name,
                        bool terminal);

[[nodiscard]] std::string prompt_rename_text(const std::string& title, const std::string& initial_value);

}
