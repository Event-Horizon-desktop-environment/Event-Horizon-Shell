#pragma once

#include <cairo/cairo.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hpv::sc {

constexpr std::size_t kMaxIconMissLogged = 256;

struct ThemeInfo {
  std::string id;
  std::string name;
  std::string path;
  bool hidden = false;
};

std::string detect_system_icon_theme();
std::vector<ThemeInfo> list_installed_icon_themes();

struct IconEntry {
  cairo_surface_t* surface = nullptr;
  int width = 0;
  int height = 0;
  std::size_t bytes = 0;
  std::list<std::string>::iterator lru_it{};
};

struct IconCacheData {
  std::unordered_map<std::string, IconEntry> cache{};
  std::size_t totalBytes = 0;
  std::string keyBuf;
  std::list<std::string> lru{};
  std::unordered_set<std::string> missLogged{};
  std::unordered_set<std::string> execBasenameMiss{};
  std::string themeOverride;
  std::string resolvedThemeId;
  std::vector<std::string> searchDirs{};
  bool searchDirsBuilt = false;
  std::uint64_t generation = 0;
};

class IconCache {
public:
  IconCache();
  IconCache(const IconCache&) = delete;
  IconCache& operator=(const IconCache&) = delete;
  IconCache(IconCache&&) = delete;
  IconCache& operator=(IconCache&&) = delete;
  ~IconCache();

  const IconEntry* app_icon(const std::string& appId);
  const IconEntry* tray_icon(const std::string& iconName);

  void set_icon_theme(std::string themeId);
  const std::string& icon_theme_override() const { return d_->themeOverride; }

  bool refresh_auto_theme_if_needed();
  void prewarm_search_dirs();

private:
  std::shared_ptr<IconCacheData> d_;
  const IconEntry* resolve_and_cache(const std::string& key, const std::string& iconName, bool isTray);
  void clear();
  void rebuild_search_dirs_if_needed();
  void touch_lru_key(const std::string& key);
  void evict_excess_icon_cache_entries();
  void ensureFresh();
};

}
