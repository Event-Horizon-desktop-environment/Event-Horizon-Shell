#pragma once

#include <cairo/cairo.h>
#include <string>

namespace eh::file_browser {

cairo_surface_t* load_cached_thumbnail(const std::string& source_path, int max_px);
void save_thumbnail_cache(const std::string& source_path, int max_px, cairo_surface_t* surface);

} // namespace eh::file_browser
