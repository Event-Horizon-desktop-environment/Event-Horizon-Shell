#pragma once

#include <cairo/cairo.h>

#include <optional>
#include <string>

namespace eh_os_logo {

struct OsReleaseInfo {
  std::string id;
  std::string logo_icon_name;
};

std::optional<OsReleaseInfo> read_os_release();

std::optional<std::string> resolve_logo_image_path(const OsReleaseInfo& info);

cairo_surface_t* load_distro_logo_cairo_surface();

void paint_logo_surface_scaled(cairo_t* cr, cairo_surface_t* src, double dst_x, double dst_y, double scale,
                               bool matugen_tint, double accent_r, double accent_g, double accent_b);

void paint_logo_surface_in_square(cairo_t* cr, cairo_surface_t* src, double box_x, double box_y, double box,
                                  bool matugen_tint, double accent_r, double accent_g, double accent_b);

}
