#include "archive_viewer/fb/fb_icons.hpp"

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/src/nanosvg.h"
#include "nanosvg/src/nanosvgrast.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace archive_viewer {

// ── Base icon directories (XDG spec) ───────────────────────────────

std::vector<std::string> IconLoader::icon_base_dirs() {
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
  return dirs;
}

// ── Detect current icon theme ──────────────────────────────────────

std::string IconLoader::detect_icon_theme() {
  const char* env = std::getenv("EH_ICON_THEME");
  if (env && *env) return env;

  // Try gsettings
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
        if (!theme.empty()) return theme;
      }
    } else {
      pclose(gs);
    }
  }

  // Try GTK settings.ini
  const char* home = std::getenv("HOME");
  if (home) {
    std::string path = std::string(home) + "/.config/gtk-3.0/settings.ini";
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
      if (line.find("gtk-icon-theme-name=") == 0) {
        auto theme = line.substr(21);
        if (!theme.empty()) return theme;
      }
    }
  }

  return "Adwaita";
}

// ── Read inherited themes from index.theme ─────────────────────────

std::vector<std::string> IconLoader::read_inherited_themes(const std::string& theme_dir) {
  std::vector<std::string> result;
  auto index = fs::path(theme_dir) / "index.theme";
  if (!fs::exists(index)) return result;
  std::ifstream f(index);
  if (!f.is_open()) return result;
  std::string line;
  bool in_icon_theme = false;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    if (line[0] == '[') {
      in_icon_theme = (line == "[Icon Theme]");
      continue;
    }
    if (!in_icon_theme) continue;
    if (line.find("Inherits=") == 0) {
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

// ── Find icon file in theme directory ──────────────────────────────

std::string IconLoader::find_icon_file(const std::string& theme_dir_str,
                                        const std::string& icon_name,
                                        int size) {
  fs::path theme_dir(theme_dir_str);
  if (!fs::is_directory(theme_dir)) return {};
  auto size_str = std::to_string(size);

  std::vector<fs::path> candidates = {
    theme_dir / "scalable" / "places" / (icon_name + ".svg"),
    theme_dir / "scalable" / "apps" / (icon_name + ".svg"),
    theme_dir / "scalable" / "actions" / (icon_name + ".svg"),
    theme_dir / "scalable" / "devices" / (icon_name + ".svg"),
    theme_dir / "scalable" / "status" / (icon_name + ".svg"),
    theme_dir / "scalable" / "emblems" / (icon_name + ".svg"),
    theme_dir / "scalable" / "mimetypes" / (icon_name + ".svg"),

    theme_dir / (size_str + "x" + size_str) / "places" / (icon_name + ".png"),
    theme_dir / (size_str + "x" + size_str) / "places" / (icon_name + ".svg"),
    theme_dir / (size_str + "x" + size_str) / "apps" / (icon_name + ".png"),
    theme_dir / (size_str + "x" + size_str) / "apps" / (icon_name + ".svg"),
    theme_dir / (size_str + "x" + size_str) / "actions" / (icon_name + ".png"),
    theme_dir / (size_str + "x" + size_str) / "devices" / (icon_name + ".png"),
    theme_dir / (size_str + "x" + size_str) / "devices" / (icon_name + ".svg"),
    theme_dir / (size_str + "x" + size_str) / "status" / (icon_name + ".png"),
    theme_dir / (size_str + "x" + size_str) / "mimetypes" / (icon_name + ".png"),

    theme_dir / "symbolic" / "places" / (icon_name + "-symbolic.svg"),
    theme_dir / "symbolic" / "actions" / (icon_name + "-symbolic.svg"),
    theme_dir / "symbolic" / "status" / (icon_name + "-symbolic.svg"),

    theme_dir / "places" / "scalable" / (icon_name + ".svg"),
    theme_dir / "apps" / "scalable" / (icon_name + ".svg"),
    theme_dir / "devices" / "scalable" / (icon_name + ".svg"),
    theme_dir / "actions" / "scalable" / (icon_name + ".svg"),
    theme_dir / "mimes" / "scalable" / (icon_name + ".svg"),
    theme_dir / "mimes" / "symbolic" / (icon_name + "-symbolic.svg"),
    theme_dir / "places" / size_str / (icon_name + ".svg"),
    theme_dir / "apps" / size_str / (icon_name + ".svg"),
    theme_dir / "devices" / size_str / (icon_name + ".svg"),
    theme_dir / "actions" / size_str / (icon_name + ".svg"),
    theme_dir / "mimes" / size_str / (icon_name + ".svg"),
  };

  for (const auto& p : candidates) {
    if (fs::exists(p)) return p.string();
  }
  return {};
}

// ── Load SVG → cairo surface ───────────────────────────────────────

static cairo_surface_t* load_svg_surface(const std::string& path, int size) {
  NSVGimage* image = nsvgParseFromFile(path.c_str(), "px", 96.0f);
  if (!image) return nullptr;

  // Some theme SVGs embed base64 PNGs that nanosvg can't render
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

  for (int i = 0; i < w * h; i++) {
    unsigned char* p = rgba + i * 4;
    unsigned char tmp = p[0];
    p[0] = p[2];
    p[2] = tmp;
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

// ── IconLoader implementation ──────────────────────────────────────

IconLoader::IconLoader() {
  theme_ = detect_icon_theme();
}

IconLoader::~IconLoader() {
  for (auto& [key, entry] : cache_) {
    if (entry.surface) cairo_surface_destroy(entry.surface);
  }
}

void IconLoader::rebuild_search_dirs() {
  search_dirs_.clear();

  std::vector<std::string> themes_to_check;
  themes_to_check.push_back(theme_);

  for (const auto& base : icon_base_dirs()) {
    auto td = fs::path(base) / theme_;
    if (fs::is_directory(td)) {
      auto inherited = read_inherited_themes(td.string());
      for (auto& t : inherited) {
        if (!t.empty() && std::find(themes_to_check.begin(), themes_to_check.end(), t) == themes_to_check.end())
          themes_to_check.push_back(t);
      }
      break;
    }
  }
  themes_to_check.push_back("hicolor");

  for (const auto& t : themes_to_check) {
    for (const auto& base : icon_base_dirs()) {
      auto td = fs::path(base) / t;
      if (fs::is_directory(td)) {
        search_dirs_.push_back(td.string());
      }
    }
  }
}

const IconEntry* IconLoader::load(const std::string& icon_name) {
  if (icon_name.empty()) return nullptr;

  // Check cache
  auto it = cache_.find(icon_name);
  if (it != cache_.end()) return &it->second;

  if (search_dirs_.empty()) rebuild_search_dirs();

  int load_size = 256; // load at high resolution

  for (const auto& td : search_dirs_) {
    auto path = find_icon_file(td, icon_name, load_size);
    if (path.empty()) continue;

    // Determine if it's SVG or PNG
    cairo_surface_t* surf = nullptr;
    if (path.size() > 4 && (path.substr(path.size() - 4) == ".svg" || path.substr(path.size() - 4) == ".SVG")) {
      surf = load_svg_surface(path, load_size);
    } else if (path.size() > 4 && (path.substr(path.size() - 4) == ".png" || path.substr(path.size() - 4) == ".PNG")) {
      int w = 0, h = 0, n = 0;
      unsigned char* rgba = stbi_load(path.c_str(), &w, &h, &n, 4);
      if (rgba && w > 0 && h > 0) {
        for (int i = 0; i < w * h; ++i) {
          unsigned char* pp = rgba + i * 4;
          unsigned char pt = pp[0]; pp[0] = pp[2]; pp[2] = pt;
        }
        static const cairo_user_data_key_t kPngKey = {};
        surf = cairo_image_surface_create_for_data(
            rgba, CAIRO_FORMAT_ARGB32, w, h, w * 4);
        cairo_surface_set_user_data(surf, &kPngKey, rgba,
            [](void* d) { stbi_image_free(d); });
      }
    }

    if (surf) {
      auto& entry = cache_[icon_name];
      entry.surface = surf;
      entry.width = cairo_image_surface_get_width(surf);
      entry.height = cairo_image_surface_get_height(surf);
      return &entry;
    }
  }

  // Cache miss — store nullptr to avoid re-looking-up
  cache_[icon_name] = IconEntry{};
  return nullptr;
}

} // namespace archive_viewer
