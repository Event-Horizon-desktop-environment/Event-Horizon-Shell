#pragma once

#include <string>
#include <cairo/cairo.h>

namespace eh::file_browser {

bool is_epub_extension(const std::string& path);
cairo_surface_t* load_epub_thumbnail(const std::string& path, int max_px);

} // namespace eh::file_browser
