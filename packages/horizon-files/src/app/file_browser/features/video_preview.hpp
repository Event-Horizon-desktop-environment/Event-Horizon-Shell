#pragma once

#include <cairo/cairo.h>

#include <string>

namespace eh::file_browser {

bool is_video_extension(const std::string& path);

cairo_surface_t* load_video_thumbnail(const std::string& path, int max_px);

} // namespace eh::file_browser
