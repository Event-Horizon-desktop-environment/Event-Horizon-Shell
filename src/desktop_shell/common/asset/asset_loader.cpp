#include "desktop_shell/common/asset/asset_loader.hpp"

#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/common/glyph/bundled_assets.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/log/verbose_log.hpp"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <array>
#include <unistd.h>

#ifdef EH_HAVE_RSVG
#include <librsvg/rsvg.h>
#endif

using eh::shell::str::file_exists;

namespace {

std::string exe_dir() {
   
  std::array<char, 4096> buf;
  const ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (n <= 0) return ".";
  buf[n] = 0;
  std::string p(buf.data());
  const auto slash = p.find_last_of('/');
  if (slash == std::string::npos) return ".";
  return p.substr(0, slash);
}

cairo_surface_t* load_asset_png(const char* filename) {
   
  auto try_path = [&](const std::string& base) -> cairo_surface_t* {
    const std::string p = base + "/" + filename;
    if (!file_exists(p)) return nullptr;
    cairo_surface_t* s = cairo_image_surface_create_from_png(p.c_str());
    if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
      if (s) cairo_surface_destroy(s);
      return nullptr;
    }
    return s;
  };
  if (const char* d = std::getenv("EH_ASSETS_DIR")) {
    if (auto* s = try_path(d)) return s;
  }
  for (const char* base : {"/usr/local/share/event-horizon/assets", "/usr/share/event-horizon/assets"}) {
    if (auto* s = try_path(base)) return s;
  }
  if (auto* s = try_path("assets")) return s;
  if (auto* s = try_path("src/assets")) return s;
  {
    const std::string p = exe_dir() + "/../assets";
    if (auto* s = try_path(p)) return s;
  }
  {
    const std::string p = exe_dir() + "/../src/assets";
    if (auto* s = try_path(p)) return s;
  }
  return nullptr;
}

#ifdef EH_HAVE_RSVG
cairo_surface_t* cairo_from_svg_file(const std::string& path, int max_px) {
   
  GError* err = nullptr;
  RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
  if (!handle) {
    if (err) g_error_free(err);
    return nullptr;
  }
  double w = 256.0, h = 256.0;
  double iw = 0.0, ih = 0.0;
  if (rsvg_handle_get_intrinsic_size_in_pixels(handle, &iw, &ih)) {
    if (iw > 0.0) w = iw;
    if (ih > 0.0) h = ih;
  }
  double scale = 1.0;
  if (std::max(w, h) > static_cast<double>(max_px)) scale = static_cast<double>(max_px) / std::max(w, h);
  const int out_w = std::max(1, static_cast<int>(std::lround(w * scale)));
  const int out_h = std::max(1, static_cast<int>(std::lround(h * scale)));

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, out_w, out_h);
  cairo_t* cr = cairo_create(surf);
  RsvgRectangle viewport{0.0, 0.0, w * scale, h * scale};
  if (!rsvg_handle_render_document(handle, cr, &viewport, &err)) {
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    g_object_unref(handle);
    if (err) g_error_free(err);
    return nullptr;
  }
  cairo_destroy(cr);
  g_object_unref(handle);
  return surf;
}
#endif

cairo_surface_t* load_asset(const char* filename_png, const char* filename_svg, int max_px) {
  (void)filename_svg;
  (void)max_px;
    
  // Try SVG first if available
#ifdef EH_HAVE_RSVG
  {
    std::string svg_path;
    if (const char* d = std::getenv("EH_ASSETS_DIR")) {
      svg_path = std::string(d) + "/" + filename_svg;
      if (file_exists(svg_path)) {
        cairo_surface_t* s = cairo_from_svg_file(svg_path, max_px);
        if (s) return s;
      }
    }
    for (const char* base : {"/usr/local/share/event-horizon/assets", "/usr/share/event-horizon/assets"}) {
      svg_path = std::string(base) + "/" + filename_svg;
      if (file_exists(svg_path)) {
        cairo_surface_t* s = cairo_from_svg_file(svg_path, max_px);
        if (s) return s;
      }
    }
    {
      std::string svg_path = "assets/" + std::string(filename_svg);
      if (file_exists(svg_path)) {
        cairo_surface_t* s = cairo_from_svg_file(svg_path, max_px);
        if (s) return s;
      }
    }
    {
      std::string svg_path = "src/assets/" + std::string(filename_svg);
      if (file_exists(svg_path)) {
        cairo_surface_t* s = cairo_from_svg_file(svg_path, max_px);
        if (s) return s;
      }
    }
    {
      std::string svg_path = exe_dir() + "/../assets/" + filename_svg;
      if (file_exists(svg_path)) {
        cairo_surface_t* s = cairo_from_svg_file(svg_path, max_px);
        if (s) return s;
      }
    }
    {
      std::string svg_path = exe_dir() + "/../src/assets/" + filename_svg;
      if (file_exists(svg_path)) {
        cairo_surface_t* s = cairo_from_svg_file(svg_path, max_px);
        if (s) return s;
      }
    }
  }
#endif
  // Fallback to PNG
  return load_asset_png(filename_png);
}

}  // namespace

namespace eh::shell::asset {

cairo_surface_t* load_brand_logo_surface() {
   
  return eh::shell::create_material_glyph_surface("settings", 48);
}

cairo_surface_t* load_launchpad_logo_surface(bool light) {
   
  static bool loggedDark = false;
  static bool loggedLight = false;
  if (light) {
    cairo_surface_t* s = load_asset("Light_Launchpad.png", "Light_Launchpad.svg", 128);
    if (s) {
      if (!loggedLight && eh_verbose_enabled()) {
        loggedLight = true;
        std::cout << "[assets] launchpad_logo_light: loaded\n";
      }
      return s;
    }
    if (!loggedLight && eh_verbose_enabled()) {
      loggedLight = true;
      std::cout << "[assets] launchpad_logo_light: not_found\n";
    }
    return nullptr;
  }
  cairo_surface_t* s = load_asset("Dark_Launchpad.png", "Dark_Launchpad.svg", 128);
  if (s) {
    if (!loggedDark && eh_verbose_enabled()) {
      loggedDark = true;
      std::cout << "[assets] launchpad_logo_dark: loaded\n";
    }
    return s;
  }
  if (!loggedDark && eh_verbose_enabled()) {
    loggedDark = true;
    std::cout << "[assets] launchpad_logo_dark: not_found\n";
  }
  return nullptr;
}

cairo_surface_t* load_trash_empty_surface() {
   
  static bool logged = false;
  cairo_surface_t* s = load_asset("MacOS-Trash-Empty.png", "MacOS-Trash-Empty.svg", 128);
  if (s) {
    if (!logged && eh_verbose_enabled()) {
      logged = true;
      std::cout << "[assets] trash_empty: loaded\n";
    }
    return s;
  }
  if (!logged && eh_verbose_enabled()) {
    logged = true;
    std::cout << "[assets] trash_empty: not_found\n";
  }
  return nullptr;
}

cairo_surface_t* load_trash_full_surface() {
   
  static bool logged = false;
  cairo_surface_t* s = load_asset("MacOS-Trash-Full.png", "MacOS-Trash-Full.svg", 128);
  if (s) {
    if (!logged && eh_verbose_enabled()) {
      logged = true;
      std::cout << "[assets] trash_full: loaded\n";
    }
    return s;
  }
  if (!logged && eh_verbose_enabled()) {
    logged = true;
    std::cout << "[assets] trash_full: not_found\n";
  }
  return nullptr;
}

cairo_surface_t* load_asset_svg(const char* subdir, const char* filename, int max_px) {
  (void)max_px;
    
  if (!filename || filename[0] == 0) return nullptr;
  std::string svg_name = (subdir && subdir[0]) ? std::string(subdir) + "/" + filename : filename;
#ifdef EH_HAVE_RSVG
  {
    auto try_path = [&](const std::string& base) -> cairo_surface_t* {
      const std::string p = base + "/" + svg_name;
      if (!file_exists(p)) return nullptr;
      return cairo_from_svg_file(p, max_px);
    };
    if (const char* d = std::getenv("EH_ASSETS_DIR")) {
      if (auto* s = try_path(d)) return s;
    }
    for (const char* base : {"/usr/local/share/event-horizon/assets",
                              "/usr/share/event-horizon/assets"}) {
      if (auto* s = try_path(base)) return s;
    }
    if (auto* s = try_path("assets")) return s;
    if (auto* s = try_path("src/assets")) return s;
    if (auto* s = try_path("../assets")) return s;
    if (auto* s = try_path(exe_dir() + "/../assets")) return s;
    if (auto* s = try_path(exe_dir() + "/../src/assets")) return s;
  }
#endif
  return nullptr;
}

}  // namespace eh::shell::asset