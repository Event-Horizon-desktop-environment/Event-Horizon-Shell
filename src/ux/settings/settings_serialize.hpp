#pragma once

#include <string>
#include <unordered_set>
#include <vector>

struct Settings;

namespace eh::config {
struct ShellConfig;
}

std::string join_list(const std::vector<std::string>& v);
std::string join_set(const std::unordered_set<std::string>& s);
std::unordered_set<std::string> parse_semicolon_set(const std::string& s);
std::string get_state_dir();
Settings load_settings();
void save_settings(const Settings& s);
std::unordered_set<std::string> load_desktop_widget_disabled_slots();
eh::config::ShellConfig settings_to_shell_config(const Settings& s);
int settings_dock_preview_auto_bar_px(const Settings& s);
void wallpaper_apply_if_digest_changed(const Settings& s);
bool widget_slot_enabled_for_settings(const std::unordered_set<std::string>& disabled, const std::string& id);
void patch_workspaces_widget_in_shell_config(eh::config::ShellConfig& sc, const Settings& s);
void patch_widget_slot_enabled_into_shell_config(eh::config::ShellConfig& sc, const Settings& s);
