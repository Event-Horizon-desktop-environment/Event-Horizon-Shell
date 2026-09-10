#pragma once
#include <string>
#include <vector>

#include "ux/settings/theme_data.hpp"

struct UserTheme {
  std::string name;
  std::string source;
  std::string variant;
  std::string filePath;
  eh::settings::theme::ThemePalette palette;
};

std::string theme_directory();
std::vector<UserTheme> scan_user_themes();
bool load_theme_from_file(const std::string& path, UserTheme& out);
bool save_theme_to_file(const UserTheme& theme);
bool delete_theme_file(const std::string& path);
void apply_theme_to_app(struct App& app, const eh::settings::theme::ThemePalette& palette);
