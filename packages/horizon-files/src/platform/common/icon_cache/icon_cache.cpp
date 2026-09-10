#include "platform/common/icon_cache/icon_cache.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg/src/nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/src/nanosvgrast.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace eh::icons {

static bool icon_debug() {
  static bool on = [] {
    const char* e = std::getenv("EH_ICON_DEBUG");
    return e && *e && e[0] != '0';
  }();
  return on;
}

#define ICON_DBG(...) do { if (icon_debug()) fprintf(stderr, "[icon] " __VA_ARGS__); } while (false)

// ── helpers ──────────────────────────────────────────────────────────

static std::vector<std::string> icon_base_dirs() {
  std::vector<std::string> dirs;
  const char* home = std::getenv("HOME");
  if (home) {
    dirs.push_back(std::string(home) + "/.local/share/icons");
    dirs.push_back(std::string(home) + "/.icons");
  }
  const char* xdg = std::getenv("XDG_DATA_DIRS");
  if (xdg) {
    std::string s(xdg);
    size_t pos = 0, end;
    while ((end = s.find(':', pos)) != std::string::npos) {
      dirs.push_back(s.substr(pos, end - pos) + "/icons");
      pos = end + 1;
    }
    dirs.push_back(s.substr(pos) + "/icons");
  }
  dirs.emplace_back("/usr/local/share/icons");
  dirs.emplace_back("/usr/share/icons");
  if (icon_debug()) {
    fprintf(stderr, "[icon] base dirs:");
    for (auto& d : dirs) fprintf(stderr, " %s", d.c_str());
    fprintf(stderr, "\n");
  }
  return dirs;
}

static std::string find_icon_file(const fs::path& theme_dir, const std::string& icon_name, int size) {
  if (!fs::is_directory(theme_dir)) return {};
  std::string theme_name = theme_dir.filename().string();
  auto size_str = std::to_string(size);

  // Try standard Freedesktop paths first (fast, no iteration)
  std::vector<fs::path> candidates = {
    // scalable/<cat>/icon.svg    (standard)
    theme_dir / "scalable" / "places" / (icon_name + ".svg"),
    theme_dir / "scalable" / "apps" / (icon_name + ".svg"),
    theme_dir / "scalable" / "actions" / (icon_name + ".svg"),
    theme_dir / "scalable" / "devices" / (icon_name + ".svg"),
    theme_dir / "scalable" / "status" / (icon_name + ".svg"),
    theme_dir / "scalable" / "emblems" / (icon_name + ".svg"),
    theme_dir / "scalable" / "mimetypes" / (icon_name + ".svg"),

    // <size>x<size>/<cat>/icon.png  (standard)
    theme_dir / (size_str + "x" + size_str) / "places" / (icon_name + ".png"),
    theme_dir / (size_str + "x" + size_str) / "places" / (icon_name + ".svg"),
    theme_dir / (size_str + "x" + size_str) / "apps" / (icon_name + ".png"),
    theme_dir / (size_str + "x" + size_str) / "apps" / (icon_name + ".svg"),
    theme_dir / (size_str + "x" + size_str) / "actions" / (icon_name + ".png"),
    theme_dir / (size_str + "x" + size_str) / "devices" / (icon_name + ".png"),
    theme_dir / (size_str + "x" + size_str) / "devices" / (icon_name + ".svg"),
    theme_dir / (size_str + "x" + size_str) / "status" / (icon_name + ".png"),
    theme_dir / (size_str + "x" + size_str) / "mimetypes" / (icon_name + ".png"),

    // symbolic/<cat>/icon-symbolic.svg
    theme_dir / "symbolic" / "places" / (icon_name + "-symbolic.svg"),
    theme_dir / "symbolic" / "actions" / (icon_name + "-symbolic.svg"),
    theme_dir / "symbolic" / "status" / (icon_name + "-symbolic.svg"),

    // MacTahoe layout: <cat>/scalable/icon.svg
    theme_dir / "places" / "scalable" / (icon_name + ".svg"),
    theme_dir / "apps" / "scalable" / (icon_name + ".svg"),
    theme_dir / "devices" / "scalable" / (icon_name + ".svg"),
    theme_dir / "actions" / "scalable" / (icon_name + ".svg"),

    // MacTahoe mimes: mimes/scalable/icon.svg
    theme_dir / "mimes" / "scalable" / (icon_name + ".svg"),

    // MacTahoe mimes: mimes/symbolic/icon-symbolic.svg
    theme_dir / "mimes" / "symbolic" / (icon_name + "-symbolic.svg"),

    // MacTahoe: <cat>/<size>/icon.svg
    theme_dir / "places" / size_str / (icon_name + ".svg"),
    theme_dir / "apps" / size_str / (icon_name + ".svg"),
    theme_dir / "devices" / size_str / (icon_name + ".svg"),
    theme_dir / "actions" / size_str / (icon_name + ".svg"),

    // MacTahoe mimes: mimes/<size>/icon.svg
    theme_dir / "mimes" / size_str / (icon_name + ".svg"),
  };

  // Also try nearby sizes for MacTahoe style
  for (int s : {16, 22, 24, 32, 48, 64, 96, 128, 256}) {
    if (s == size) continue;
    auto ss = std::to_string(s);
    candidates.push_back(theme_dir / "places" / ss / (icon_name + ".svg"));
    candidates.push_back(theme_dir / "apps" / ss / (icon_name + ".svg"));
    candidates.push_back(theme_dir / "devices" / ss / (icon_name + ".svg"));
    candidates.push_back(theme_dir / "mimes" / ss / (icon_name + ".svg"));
  }

  for (auto& p : candidates) {
    if (fs::exists(p)) {
      ICON_DBG("  found '%s' at %s\n", icon_name.c_str(), p.c_str());
      return p.string();
    }
  }

  ICON_DBG("  NOT found '%s' in theme %s\n", icon_name.c_str(), theme_name.c_str());
  return {};
}

static cairo_surface_t* ensure_min_size(cairo_surface_t* surf, int min_px) {
  if (!surf) return nullptr;
  int w = cairo_image_surface_get_width(surf);
  int h = cairo_image_surface_get_height(surf);
  if (w >= min_px && h >= min_px) return surf;
  double sc = static_cast<double>(min_px) / std::max(w, h);
  int dw = std::max(1, static_cast<int>(std::round(w * sc)));
  int dh = std::max(1, static_cast<int>(std::round(h * sc)));
  cairo_surface_t* dst = cairo_surface_create_similar_image(surf, CAIRO_FORMAT_ARGB32, dw, dh);
  if (!dst || cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(dst);
    return surf;
  }
  cairo_t* cr = cairo_create(dst);
  cairo_scale(cr, sc, sc);
  cairo_set_source_surface(cr, surf, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(dst);
  cairo_surface_destroy(surf);
  return dst;
}

static cairo_surface_t* load_png(const std::string& path, int min_px) {
  int w, h, n;
  unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (!data) return nullptr;

  // stb_image outputs RGBA but CAIRO_FORMAT_ARGB32 expects BGRA on little-endian
  for (int i = 0; i < w * h; i++) {
    unsigned char* p = data + i * 4;
    unsigned char tmp = p[0];
    p[0] = p[2];
    p[2] = tmp;
  }

  static const cairo_user_data_key_t kPngKey = {};
  cairo_surface_t* surf = cairo_image_surface_create_for_data(
      data, CAIRO_FORMAT_ARGB32, w, h, w * 4);
  cairo_surface_set_user_data(surf, &kPngKey, data,
      [](void* d) { stbi_image_free(d); });
  return ensure_min_size(surf, min_px);
}

static cairo_surface_t* load_svg(const std::string& path, int size) {
  NSVGimage* image = nsvgParseFromFile(path.c_str(), "px", 96.0f);
  if (!image) return nullptr;

  // Some themes ship SVGs that wrap an embedded base64 PNG via
  // <image xlink:href="data:image/png;base64,..."> instead of real
  // vector paths. nanosvg can't render these, so detect and handle
  // them ourselves.
  if (!image->shapes) {
    nsvgDelete(image);
    static const auto b64_d = []() -> std::array<unsigned char, 256> {
      std::array<unsigned char, 256> t = {};
      for (int i = 0; i < 26; ++i) {
        t[static_cast<unsigned char>('A' + i)] = static_cast<unsigned char>(i);
        t[static_cast<unsigned char>('a' + i)] = static_cast<unsigned char>(26 + i);
      }
      for (int i = 0; i < 10; ++i)
        t[static_cast<unsigned char>('0' + i)] = static_cast<unsigned char>(52 + i);
      t[static_cast<unsigned char>('+')] = 62;
      t[static_cast<unsigned char>('/')] = 63;
      return t;
    }();
    auto b64_c = [&](char c) -> unsigned char { return b64_d[static_cast<unsigned char>(c)]; };
    std::string svg_data;
    {
      std::ifstream ifs(path, std::ios::binary);
      if (ifs) {
        ifs.seekg(0, std::ios::end);
        svg_data.resize(static_cast<std::size_t>(ifs.tellg()));
        ifs.seekg(0, std::ios::beg);
        ifs.read(svg_data.data(), static_cast<std::streamsize>(svg_data.size()));
      }
    }
    const char kNeedle[] = "data:image/png;base64,";
    auto pos = svg_data.find(kNeedle);
    if (pos != std::string::npos) {
      pos += sizeof(kNeedle) - 1;
      auto epos = pos;
      while (epos < svg_data.size() && svg_data[epos] != '"' && svg_data[epos] != '\'')
        ++epos;
      std::string b64;
      for (auto i = pos; i < epos; ++i)
        if (svg_data[i] > ' ') b64.push_back(svg_data[i]);
      std::vector<unsigned char> png_bytes;
      png_bytes.reserve(b64.size() / 4 * 3 + 3);
      for (std::size_t i = 0; i + 3 < b64.size(); i += 4) {
        unsigned char d0 = b64_c(b64[i]), d1 = b64_c(b64[i+1]);
        unsigned char d2 = b64_c(b64[i+2]), d3 = b64_c(b64[i+3]);
        png_bytes.push_back(static_cast<unsigned char>((d0 << 2) | (d1 >> 4)));
        if (b64[i+2] != '=')
          png_bytes.push_back(static_cast<unsigned char>((d1 << 4) | (d2 >> 2)));
        if (b64[i+3] != '=')
          png_bytes.push_back(static_cast<unsigned char>((d2 << 6) | d3));
      }
      int pw = 0, ph = 0, pn = 0;
      unsigned char* png_rgba = stbi_load_from_memory(
          png_bytes.data(), static_cast<int>(png_bytes.size()), &pw, &ph, &pn, 4);
      if (png_rgba) {
        float sc = static_cast<float>(size) / std::max(pw, ph);
        int tw = std::max(1, static_cast<int>(pw * sc));
        int th = std::max(1, static_cast<int>(ph * sc));
        auto* scaled = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(tw) * th * 4));
        if (scaled) {
          cairo_surface_t* src = cairo_image_surface_create_for_data(
              png_rgba, CAIRO_FORMAT_ARGB32, pw, ph, pw * 4);
          for (int i = 0; i < pw * ph; ++i) {
            unsigned char* pp = png_rgba + i * 4;
            unsigned char pt = pp[0]; pp[0] = pp[2]; pp[2] = pt;
            unsigned int pa = pp[3];
            if (pa == 0) { pp[0] = pp[1] = pp[2] = 0; }
            else if (pa < 255) {
              pp[0] = static_cast<unsigned char>((static_cast<unsigned int>(pp[0]) * pa) / 255);
              pp[1] = static_cast<unsigned char>((static_cast<unsigned int>(pp[1]) * pa) / 255);
              pp[2] = static_cast<unsigned char>((static_cast<unsigned int>(pp[2]) * pa) / 255);
            }
          }
          cairo_surface_t* dst = cairo_image_surface_create_for_data(
              scaled, CAIRO_FORMAT_ARGB32, tw, th, tw * 4);
          cairo_t* cr = cairo_create(dst);
          cairo_scale(cr, static_cast<double>(tw) / pw, static_cast<double>(th) / ph);
          cairo_set_source_surface(cr, src, 0, 0);
          cairo_paint(cr);
          cairo_destroy(cr);
          cairo_surface_destroy(src);
          static const cairo_user_data_key_t kEmbedKey = {};
          cairo_surface_set_user_data(dst, &kEmbedKey, scaled,
              [](void* d) { std::free(d); });
          stbi_image_free(png_rgba);
          return dst;
        }
        stbi_image_free(png_rgba);
      }
    }
    return nullptr;
  }

  float scale = static_cast<float>(size) / std::max(image->width, image->height);
  int w = static_cast<int>(image->width * scale);
  int h = static_cast<int>(image->height * scale);
  if (w < 1) w = 1;
  if (h < 1) h = 1;

  auto* rgba = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(w) * h * 4));
  if (!rgba) { nsvgDelete(image); return nullptr; }

  auto* rast = nsvgCreateRasterizer();
  if (!rast) { std::free(rgba); nsvgDelete(image); return nullptr; }

  nsvgRasterize(rast, image, 0, 0, scale, rgba, w, h, w * 4);
  nsvgDeleteRasterizer(rast);
  nsvgDelete(image);

  // nanosvg outputs RGBA but CAIRO_FORMAT_ARGB32 expects BGRA on little-endian
  // with pre-multiplied alpha. nanosvg gives straight alpha; Cairo requires
  // pre-multiplied, so multiply each colour channel by alpha/255.
  for (int i = 0; i < w * h; i++) {
    unsigned char* p = rgba + i * 4;
    unsigned char tmp = p[0];
    p[0] = p[2];
    p[2] = tmp;
    // Pre-multiply alpha — now channels are B, G, R, A
    unsigned int a = p[3];
    if (a == 0) {
      p[0] = p[1] = p[2] = 0;
    } else if (a < 255) {
      p[0] = static_cast<unsigned char>((static_cast<unsigned int>(p[0]) * a) / 255);
      p[1] = static_cast<unsigned char>((static_cast<unsigned int>(p[1]) * a) / 255);
      p[2] = static_cast<unsigned char>((static_cast<unsigned int>(p[2]) * a) / 255);
    }
  }

  static const cairo_user_data_key_t kSvgKey = {};
  cairo_surface_t* surf = cairo_image_surface_create_for_data(
      rgba, CAIRO_FORMAT_ARGB32, w, h, w * 4);
  cairo_surface_set_user_data(surf, &kSvgKey, rgba,
      [](void* d) { std::free(d); });
  return surf;
}

// ── free functions ───────────────────────────────────────────────────

std::string detect_system_icon_theme() {
  // 0. Environment variable override
  const char* env_theme = std::getenv("EH_ICON_THEME");
  if (env_theme && *env_theme) {
    ICON_DBG("using EH_ICON_THEME override: '%s'\n", env_theme);
    return env_theme;
  }

  // 1. Try gsettings (GNOME/Wayland compositors)
  FILE* gs = popen("gsettings get org.gnome.desktop.interface icon-theme 2>/dev/null", "r");
  if (gs) {
    char buf[128] = {};
    if (fgets(buf, sizeof(buf), gs)) {
      pclose(gs);
      std::string s(buf);
      auto q1 = s.find('\'');
      auto q2 = s.rfind('\'');
      if (q1 != std::string::npos && q2 != q1) {
        auto theme = s.substr(q1 + 1, q2 - q1 - 1);
        if (!theme.empty()) { ICON_DBG("detected theme via gsettings: '%s'\n", theme.c_str()); return theme; }
      }
    } else {
      pclose(gs);
    }
  }

  // 2. Try GTK settings.ini
  const char* home = std::getenv("HOME");
  if (home) {
    std::string path = std::string(home) + "/.config/gtk-3.0/settings.ini";
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
      if (line.starts_with("gtk-icon-theme-name=")) {
        auto theme = line.substr(21);
        if (!theme.empty()) { ICON_DBG("detected theme via gtk ini: '%s'\n", theme.c_str()); return theme; }
      }
    }
  }

  // 3. Fallback
  ICON_DBG("no theme detected via gsettings or gtk ini, using Adwaita\n");
  return "Adwaita";
}

std::vector<ThemeInfo> list_installed_icon_themes() {
  std::vector<ThemeInfo> themes;
  for (const auto& base : icon_base_dirs()) {
    if (!fs::is_directory(base)) continue;
    for (const auto& td : fs::directory_iterator(base)) {
      if (!td.is_directory()) continue;
      ThemeInfo ti;
      ti.path = td.path().string();
      ti.id = td.path().filename().string();
      ti.name = ti.id;
      themes.push_back(std::move(ti));
    }
  }
  return themes;
}

cairo_surface_t* load_theme_preview_icon(const std::string& themeDir, const std::string& iconName, int targetPx) {
  (void)themeDir; (void)iconName; (void)targetPx;
  return nullptr;
}

std::string theme_example_icon_name(const std::string& themeDir) {
  (void)themeDir;
  return "folder";
}

std::vector<std::string> theme_find_any_icons(const std::string& themeDir, int maxCount) {
  (void)themeDir; (void)maxCount;
  return {};
}

std::vector<std::string> list_qt_color_schemes() { return {}; }
std::string detect_current_qt_color_scheme() { return {}; }
bool apply_qt_color_scheme(const std::string&, bool) { return false; }

GtkThemeColors extract_gtk_theme_colors(const std::string&) { return {}; }
void clear_gtk_theme_colors_cache() {}
GtkThemeColors qt_style_preview_colors(const std::string&) { return {}; }
GtkThemeColors qt_color_scheme_preview_colors(const std::string&) { return {}; }
std::string detect_system_gtk_theme() { return "Adwaita"; }
std::vector<GtkThemeInfo> list_installed_gtk_themes() { return {}; }
bool apply_gtk_theme(const std::string&) { return false; }
std::string detect_system_qt_style() { return "Fusion"; }
std::vector<std::string> known_qt_styles() { return {"Fusion", "Breeze"}; }
bool apply_qt_style(const std::string&, bool) { return false; }
std::vector<PlasmaThemeInfo> list_installed_plasma_themes() { return {}; }
GtkThemeColors extract_plasma_theme_colors(const std::string&) { return {}; }
bool apply_plasma_theme(const std::string&) { return false; }
std::vector<CursorThemeInfo> list_installed_cursor_themes() { return {}; }
cairo_surface_t* load_cursor_shape_surface(const std::string&, const std::string&, int) { return nullptr; }
GtkThemeDesign extract_gtk_theme_design(const std::string&) { return {}; }

// ── IconCache ────────────────────────────────────────────────────────

static constexpr std::size_t kMaxCacheBytes = 16 * 1024 * 1024; // 16 MB

cairo_surface_t* IconCache::load_settings_logo_surface() { return nullptr; }

IconCache::IconCache()
  : d_(std::make_shared<IconCacheData>()) {
  d_->themeOverride = detect_system_icon_theme();
}

IconCache::~IconCache() = default;

void IconCache::set_icon_theme(std::string themeId) {
  if (d_->themeOverride == themeId) return;
  d_->themeOverride = std::move(themeId);
  d_->resolvedThemeId.clear();
  d_->searchDirsBuilt = false;
  clear();
}

void IconCache::prewarm_search_dirs() {
  rebuild_search_dirs_if_needed();
}

static std::vector<std::string> read_inherited_themes(const fs::path& theme_dir) {
  std::vector<std::string> result;
  auto index = theme_dir / "index.theme";
  if (!fs::exists(index)) return result;
  std::ifstream f(index);
  if (!f.is_open()) return result;
  std::string line;
  bool in_icon_theme = false;
  while (std::getline(f, line)) {
    if (line[0] == '[') {
      in_icon_theme = (line == "[Icon Theme]");
      continue;
    }
    if (!in_icon_theme) continue;
    if (line.starts_with("Inherits=")) {
      auto val = line.substr(9);
      size_t start = 0, end;
      while ((end = val.find(',', start)) != std::string::npos) {
        result.push_back(val.substr(start, end - start));
        start = end + 1;
      }
      result.push_back(val.substr(start));
      break;
    }
  }
  return result;
}

void IconCache::rebuild_search_dirs_if_needed() {
  if (d_->searchDirsBuilt) return;
  d_->searchDirs.clear();

  std::string theme = d_->themeOverride;
  if (theme.empty()) theme = "Adwaita";
  ICON_DBG("theme override: '%s'\n", d_->themeOverride.c_str());
  ICON_DBG("final theme: '%s'\n", theme.c_str());

  // Collect theme search path with inheritance
  std::vector<std::string> themes_to_check;
  themes_to_check.push_back(theme);

  // Read inheritance from index.theme
  for (const auto& base : icon_base_dirs()) {
    auto td = fs::path(base) / theme;
    if (fs::is_directory(td)) {
      auto inherited = read_inherited_themes(td);
      for (auto& t : inherited) {
        if (!t.empty() && std::find(themes_to_check.begin(), themes_to_check.end(), t) == themes_to_check.end())
          themes_to_check.push_back(t);
      }
      break;
    }
  }

  // Always add hicolor as final fallback
  themes_to_check.push_back("hicolor");

  // Build search dir list
  std::unordered_set<std::string> seen;
  for (const auto& t : themes_to_check) {
    for (const auto& base : icon_base_dirs()) {
      auto td = fs::path(base) / t;
      if (fs::is_directory(td) && seen.insert(td.string()).second) {
        ICON_DBG("  search dir: %s\n", td.c_str());
        d_->searchDirs.push_back(td.string());
      }
    }
  }

  d_->searchDirsBuilt = true;
}

bool IconCache::refresh_auto_theme_if_needed() { return false; }

void IconCache::clear() {
  for (auto& [key, entry] : d_->cache) {
    if (entry.surface) cairo_surface_destroy(entry.surface);
  }
  d_->cache.clear();
  d_->lru.clear();
  d_->totalBytes = 0;
  d_->missLogged.clear();
  d_->execBasenameMiss.clear();
  d_->generation++;
}

void IconCache::touch_lru_key(const std::string& key) {
  auto it = d_->cache.find(key);
  if (it == d_->cache.end()) return;
  auto& entry = it->second;
  d_->lru.erase(entry.lru_it);
  d_->lru.push_front(key);
  entry.lru_it = d_->lru.begin();
}

void IconCache::evict_excess_icon_cache_entries() {
  while (d_->totalBytes > kMaxCacheBytes && !d_->lru.empty()) {
    auto key = d_->lru.back();
    d_->lru.pop_back();
    auto it = d_->cache.find(key);
    if (it != d_->cache.end()) {
      if (it->second.surface) cairo_surface_destroy(it->second.surface);
      d_->totalBytes -= it->second.bytes;
      d_->cache.erase(it);
    }
  }
}

const IconEntry* IconCache::resolve_and_cache(const std::string& key, const std::string& icon_name, bool is_tray) {
  (void)is_tray;
  rebuild_search_dirs_if_needed();

  ICON_DBG("resolve icon key='%s' name='%s'\n", key.c_str(), icon_name.c_str());

  // Check cache
  {
    auto it = d_->cache.find(key);
    if (it != d_->cache.end()) {
      touch_lru_key(key);
      ICON_DBG("  cache HIT for '%s'\n", key.c_str());
      return &it->second;
    }
  }

  // Look up icon
  IconEntry entry;
  int load_size = 256;

  for (const auto& theme_dir : d_->searchDirs) {
    ICON_DBG("  trying dir: %s\n", theme_dir.c_str());
    auto path = find_icon_file(theme_dir, icon_name, load_size);
    if (!path.empty()) {
      ICON_DBG("  found file: %s\n", path.c_str());
      cairo_surface_t* surf = nullptr;
      if (path.ends_with(".svg")) {
        surf = load_svg(path, load_size);
      } else {
        surf = load_png(path, load_size);
      }
      if (surf) {
        entry.surface = surf;
        entry.width = cairo_image_surface_get_width(surf);
        entry.height = cairo_image_surface_get_height(surf);
        entry.bytes = static_cast<std::size_t>(entry.width) * entry.height * 4;
        break;
      }
    }
  }

  if (!entry.surface) {
    // Log miss once
    ICON_DBG("  icon NOT FOUND: '%s'\n", key.c_str());
    if (d_->missLogged.size() < kMaxIconMissLogged) {
      d_->missLogged.insert(key);
    }
    return nullptr;
  }

  ICON_DBG("  icon FOUND: '%s' (%dx%d)\n", key.c_str(), entry.width, entry.height);
  d_->lru.push_front(key);
  entry.lru_it = d_->lru.begin();
  d_->totalBytes += entry.bytes;
  auto result = d_->cache.emplace(key, std::move(entry));
  evict_excess_icon_cache_entries();
  return &result.first->second;
}

const IconEntry* IconCache::app_icon(const std::string& app_id) {
  std::string key = "app:" + app_id;
  return resolve_and_cache(key, app_id, false);
}

const IconEntry* IconCache::tray_icon(const std::string& icon_name) {
  std::string key = "tray:" + icon_name;
  ICON_DBG("tray_icon called: name='%s'\n", icon_name.c_str());
  auto* result = resolve_and_cache(key, icon_name, true);
  if (result) {
    ICON_DBG("tray_icon FOUND: name='%s' %dx%d\n", icon_name.c_str(), result->width, result->height);
  } else {
    ICON_DBG("tray_icon MISS: name='%s'\n", icon_name.c_str());
  }
  return result;
}

const IconEntry* IconCache::app_icon_from_exec_basename(const std::string& exec_basename) {
  std::string key = "exec:" + exec_basename;
  return resolve_and_cache(key, exec_basename, false);
}

void IconCache::ensureFresh() {}

}
