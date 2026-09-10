#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <cairo/cairo.h>

namespace eh::mpris {

using AlbumArtSurfacePtr = std::shared_ptr<cairo_surface_t>;

[[nodiscard]] AlbumArtSurfacePtr adopt_album_art_surface(cairo_surface_t* raw);

[[nodiscard]] std::string resolve_mpris_art_url(std::string_view dbus_art_url);

[[nodiscard]] std::string derive_youtube_thumbnail_url(std::string_view xesam_url);

[[nodiscard]] cairo_surface_t* load_album_art_surface(const std::string& resolved_url, int max_edge_px = 512);

}
