#include "desktop_shell/common/os_logo/os_logo.hpp"

#include "desktop_shell/common/glyph/bundled_assets.hpp"
#include "desktop_shell/common/log/verbose_log.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <cstdint>
#include <iostream>
#include <unordered_map>

#ifdef EH_HAVE_RSVG
#include <librsvg/rsvg.h>
#endif

namespace eh_os_logo {

static bool file_readable(const std::string& p) { return access(p.c_str(), R_OK) == 0; }

static std::string trim(std::string_view s) {
   
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return std::string{s};
}

std::optional<OsReleaseInfo> read_os_release() {
   
  constexpr const char* paths[] = {"/etc/os-release", "/usr/lib/os-release"};
  for (const char* path : paths) {
    std::ifstream in(path);
    if (!in) continue;
    OsReleaseInfo out{};
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#') continue;
      auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string key = trim(std::string_view(line.data(), eq));
      std::string val = trim(line.substr(eq + 1));
      if (!val.empty() && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
      if (key == "ID") out.id = val;
      else if (key == "LOGO") out.logo_icon_name = val;
    }
    if (!out.id.empty() || !out.logo_icon_name.empty()) return out;
  }
  return std::nullopt;
}

std::optional<std::string> resolve_logo_image_path(const OsReleaseInfo& info) {
   
  const bool id_cachy = info.id == "cachyos";
  if (id_cachy && file_readable("/usr/share/icons/cachyos.svg")) return std::string("/usr/share/icons/cachyos.svg");

  const std::string logo = info.logo_icon_name;
  if (logo.empty()) return std::nullopt;

  static constexpr const char* dirs[] = {
      "/usr/share/icons/hicolor/scalable/apps/",
      "/usr/share/icons/hicolor/symbolic/apps/",
      "/usr/share/icons/hicolor/128x128/apps/",
      "/usr/share/icons/hicolor/256x256/apps/",
      "/usr/share/icons/hicolor/512x512/apps/",
      "/usr/share/icons/hicolor/64x64/apps/",
      "/usr/share/pixmaps/",
      "/usr/share/icons/",
  };
  static constexpr const char* exts[] = {".svg", ".png"};
  for (const char* dir : dirs) {
    for (const char* ext : exts) {
      std::string p = std::string(dir) + logo + ext;
      if (file_readable(p)) return p;
    }
  }

  return std::nullopt;
}

static cairo_surface_t* cairo_png_from_file(const std::string& path) {
   
  cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
  if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    if (s) cairo_surface_destroy(s);
    return nullptr;
  }
  return s;
}

#ifdef EH_HAVE_RSVG
static cairo_surface_t* cairo_from_svg_file(const std::string& path, int max_px) {
   
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

namespace {

cairo_surface_t* copy_surface_argb32(cairo_surface_t* src) {
   
  const int w = cairo_image_surface_get_width(src);
  const int h = cairo_image_surface_get_height(src);
  cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (cairo_surface_status(tmp) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(tmp);
    return nullptr;
  }
  cairo_t* cr = cairo_create(tmp);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_flush(tmp);
  return tmp;
}

cairo_surface_t* luminance_alpha_mask_a8(cairo_surface_t* argb_src) {
   
  cairo_surface_flush(argb_src);
  const int w = cairo_image_surface_get_width(argb_src);
  const int h = cairo_image_surface_get_height(argb_src);
  cairo_surface_t* mask = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
  if (cairo_surface_status(mask) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(mask);
    return nullptr;
  }
  unsigned char* md = cairo_image_surface_get_data(mask);
  const int mstride = cairo_image_surface_get_stride(mask);
  unsigned char* sd = cairo_image_surface_get_data(argb_src);
  const int sstride = cairo_image_surface_get_stride(argb_src);
  for (int yy = 0; yy < h; ++yy) {
    const unsigned char* srow = sd + yy * sstride;
    unsigned char* mrow = md + yy * mstride;
    for (int xx = 0; xx < w; ++xx) {
      std::uint32_t px{};
      std::memcpy(&px, srow + xx * 4, sizeof(px));
      const unsigned char a = static_cast<unsigned char>((px >> 24) & 0xffu);
      mrow[xx] = a;
    }
  }
  cairo_surface_mark_dirty(mask);
  return mask;
}

cairo_surface_t* mask_for_logo_surface(cairo_surface_t* src) {
   
  static std::unordered_map<const cairo_surface_t*, cairo_surface_t*> cache;
  auto it = cache.find(src);
  if (it != cache.end()) return it->second;

  cairo_surface_t* argb = src;
  cairo_surface_t* owned = nullptr;
  if (cairo_image_surface_get_format(src) != CAIRO_FORMAT_ARGB32) {
    owned = copy_surface_argb32(src);
    if (!owned) return nullptr;
    argb = owned;
  }
  cairo_surface_t* mask = luminance_alpha_mask_a8(argb);
  if (owned) cairo_surface_destroy(owned);
  if (!mask) return nullptr;
  cache[src] = mask;
  constexpr std::size_t kMaxLogoMaskEntries = 32;
  while (cache.size() > kMaxLogoMaskEntries) {
    auto b = cache.begin();
    if (b->second) cairo_surface_destroy(b->second);
    cache.erase(b);
  }
  return mask;
}

}

void paint_logo_surface_scaled(cairo_t* cr, cairo_surface_t* src, double dst_x, double dst_y, double scale,
                               bool matugen_tint, double accent_r, double accent_g, double accent_b) {
   
  if (!cr || !src || scale <= 0.0) return;
  cairo_save(cr);
  cairo_translate(cr, dst_x, dst_y);
  cairo_scale(cr, scale, scale);
  if (!matugen_tint) {
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_paint(cr);
  } else {
    cairo_surface_t* mask = mask_for_logo_surface(src);
    if (!mask) {
      cairo_set_source_surface(cr, src, 0, 0);
      cairo_paint(cr);
    } else {

      cairo_set_source_rgba(cr, accent_r, accent_g, accent_b, 1.0);
      cairo_mask_surface(cr, mask, 0, 0);
    }
  }
  cairo_restore(cr);
}

void paint_logo_surface_in_square(cairo_t* cr, cairo_surface_t* src, double box_x, double box_y, double box,
                                  bool matugen_tint, double accent_r, double accent_g, double accent_b) {
   
  if (!src || box <= 0.0) return;
  const int lw = cairo_image_surface_get_width(src);
  const int lh = cairo_image_surface_get_height(src);
  if (lw <= 0 || lh <= 0) return;
  const double sc = box / static_cast<double>(std::max(lw, lh));
  const double ox = box_x + (box - static_cast<double>(lw) * sc) * 0.5;
  const double oy = box_y + (box - static_cast<double>(lh) * sc) * 0.5;
  paint_logo_surface_scaled(cr, src, ox, oy, sc, matugen_tint, accent_r, accent_g, accent_b);
}

cairo_surface_t* load_distro_logo_cairo_surface() {
   
  const auto info = read_os_release();
  if (!info.has_value()) return nullptr;
  const auto path = resolve_logo_image_path(*info);
  if (!path.has_value()) return nullptr;

  const std::string& p = *path;
  const bool is_png = p.size() >= 4 && (p.compare(p.size() - 4, 4, ".png") == 0);
  const bool is_svg = p.size() >= 4 && (p.compare(p.size() - 4, 4, ".svg") == 0);

  if (is_png) {
    static bool logged = false;
    cairo_surface_t* s = cairo_png_from_file(p);
    if (s && !logged && eh_verbose_enabled()) {
      logged = true;
      std::cout << "[os_logo] loaded PNG path='" << p << "'\n";
    }
    return s;
  }

#ifdef EH_HAVE_RSVG
  if (is_svg) {
    static bool logged = false;
    cairo_surface_t* s = cairo_from_svg_file(p, 128);
    if (s && !logged && eh_verbose_enabled()) {
      logged = true;
      std::cout << "[os_logo] rendered SVG path='" << p << "'\n";
    }
    return s;
  }
#else
  (void)is_svg;
#endif

  return nullptr;
}

}
