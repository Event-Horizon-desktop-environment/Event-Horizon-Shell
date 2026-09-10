#pragma once

#include <cairo/cairo.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace archive_viewer {

struct IconEntry {
  cairo_surface_t* surface = nullptr;
  int width = 0;
  int height = 0;
};

class IconLoader {
public:
  IconLoader();
  ~IconLoader();

  const IconEntry* load(const std::string& icon_name);

  static std::string detect_icon_theme();

private:
  void rebuild_search_dirs();
  static std::vector<std::string> icon_base_dirs();
  static std::vector<std::string> read_inherited_themes(const std::string& theme_dir);
  static std::string find_icon_file(const std::string& theme_dir_string,
                                     const std::string& icon_name, int size);

  std::vector<std::string> search_dirs_;
  std::unordered_map<std::string, IconEntry> cache_;
  std::string theme_;
};

} // namespace archive_viewer
