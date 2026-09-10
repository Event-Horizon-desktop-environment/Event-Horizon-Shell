#include "cache/thumbnail/thumb_cache.hpp"

#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <webp/encode.h>
#include <webp/decode.h>

namespace hpv {

static bool ensure_dir(const std::string& dir) {
    if (dir.empty()) return false;
    struct stat st;
    if (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return true;
    if (mkdir(dir.c_str(), 0755) == 0) return true;
    // EEXIST is fine — race with another process
    return errno == EEXIST;
}

std::string ThumbCache::cache_dir() {
    const char* home = getenv("XDG_CACHE_HOME");
    std::string dir;
    if (home && home[0]) {
        dir = home;
    } else {
        home = getenv("HOME");
        if (!home) return "";
        dir = std::string(home) + "/.cache";
    }
    dir += "/horizon-photo-viewer";
    ensure_dir(dir);
    return dir;
}

uint64_t ThumbCache::fnv1a(const std::string& s) {
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

uint64_t ThumbCache::file_mtime(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return (uint64_t)st.st_mtim.tv_sec * 1000000000ULL
         + (uint64_t)st.st_mtim.tv_nsec;
}

std::string ThumbCache::cache_path(const std::string& filepath) {
    auto dir = cache_dir();
    if (dir.empty()) return "";
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)fnv1a(filepath));
    return dir + "/" + hex + ".webp";
}

bool ThumbCache::save(const std::string& filepath, const uint8_t* rgba,
                       int w, int h) {
    if (w <= 0 || h <= 0 || !rgba) return false;
    auto path = cache_path(filepath);
    if (path.empty()) return false;

    uint8_t* webp_data = nullptr;
    size_t webp_size = WebPEncodeRGBA(rgba, w, h, w * 4, 80, &webp_data);
    if (!webp_data) return false;

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        WebPFree(webp_data);
        return false;
    }
    fwrite(webp_data, 1, webp_size, f);
    fclose(f);
    WebPFree(webp_data);
    return true;
}

bool ThumbCache::load(const std::string& filepath, std::vector<uint8_t>& rgba,
                       int& out_w, int& out_h) {
    auto path = cache_path(filepath);
    if (path.empty()) return false;

    // Check mtime: source must not be newer than cache
    uint64_t src_mtime = file_mtime(filepath);
    uint64_t cache_mtime = file_mtime(path);
    if (src_mtime == 0 || cache_mtime == 0 || src_mtime > cache_mtime) {
        return false;
    }

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return false; }

    std::vector<uint8_t> webp_buf((size_t)size);
    if (fread(webp_buf.data(), 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return false;
    }
    fclose(f);

    int w = 0, h = 0;
    uint8_t* pixels = WebPDecodeRGBA(webp_buf.data(), webp_buf.size(), &w, &h);
    if (!pixels || w <= 0 || h <= 0) {
        WebPFree(pixels);
        return false;
    }

    rgba.resize((size_t)w * h * 4);
    std::memcpy(rgba.data(), pixels, rgba.size());
    out_w = w;
    out_h = h;
    WebPFree(pixels);
    return true;
}

// ---- SVG render disk cache ----

std::string ThumbCache::svg_cache_path(const std::string& filepath) {
    auto base = cache_dir();
    if (base.empty()) return "";
    std::string dir = base + "/svg";
    ensure_dir(dir);
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)fnv1a(filepath));
    return dir + "/" + hex + ".png";
}

bool ThumbCache::save_svg_surface(const std::string& filepath,
                                   cairo_surface_t* surface) {
    if (!surface) return false;
    auto path = svg_cache_path(filepath);
    if (path.empty()) return false;
    return cairo_surface_write_to_png(surface, path.c_str()) == CAIRO_STATUS_SUCCESS;
}

cairo_surface_t* ThumbCache::load_svg_surface(const std::string& filepath) {
    auto path = svg_cache_path(filepath);
    if (path.empty()) return nullptr;

    // Check mtime
    uint64_t src_mtime = file_mtime(filepath);
    uint64_t cache_mtime = file_mtime(path);
    if (src_mtime == 0 || cache_mtime == 0 || src_mtime > cache_mtime)
        return nullptr;

    return cairo_image_surface_create_from_png(path.c_str());
}

} // namespace hpv
