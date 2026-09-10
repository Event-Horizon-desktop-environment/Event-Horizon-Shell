#pragma once

#include <cairo.h>
#include <cstdint>
#include <string>
#include <vector>

namespace hpv {

class ThumbCache {
public:
    static std::string cache_dir();
    static std::string cache_path(const std::string& filepath);

    // Save RGBA pixels as WebP to disk cache. Returns true on success.
    static bool save(const std::string& filepath, const uint8_t* rgba,
                     int w, int h);

    // Load from disk cache. Returns true and fills out/rgba on hit.
    static bool load(const std::string& filepath, std::vector<uint8_t>& rgba,
                     int& out_w, int& out_h);

    // Save a Cairo ARGB32 surface (native SVG render) as PNG to disk cache.
    // Uses a separate subdirectory so it doesn't collide with thumbnail cache.
    static bool save_svg_surface(const std::string& filepath,
                                 cairo_surface_t* surface);

    // Load a previously saved SVG render from disk cache.
    // Returns a new surface (caller must destroy), or nullptr on miss/stale.
    static cairo_surface_t* load_svg_surface(const std::string& filepath);

private:
    static uint64_t fnv1a(const std::string& s);
    static uint64_t file_mtime(const std::string& path);
    static std::string svg_cache_path(const std::string& filepath);
};

} // namespace hpv
