#include "core/viewer/app.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace fs = std::filesystem;

namespace hpv {

static void apply_rotation_flip(
    const std::vector<uint8_t>& src, int src_w, int src_h,
    std::vector<uint8_t>& dst, int& dst_w, int& dst_h,
    int rotation, bool flip_h, bool flip_v)
{
    dst_w = (rotation == 90 || rotation == 270) ? src_h : src_w;
    dst_h = (rotation == 90 || rotation == 270) ? src_w : src_h;
    dst.resize(dst_w * dst_h * 4);

    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            int sx = x, sy = y;

            // Undo flip
            if (flip_h) sx = dst_w - 1 - x;
            if (flip_v) sy = dst_h - 1 - y;

            // Undo rotation
            int rx, ry;
            switch (rotation) {
                case 90:  rx = dst_h - 1 - sy; ry = sx; break;
                case 180: rx = dst_w - 1 - sx; ry = dst_h - 1 - sy; break;
                case 270: rx = sy; ry = dst_w - 1 - sx; break;
                default:  rx = sx; ry = sy; break;
            }

            if (rx >= 0 && rx < src_w && ry >= 0 && ry < src_h) {
                int si = (ry * src_w + rx) * 4;
                int di = (y * dst_w + x) * 4;
                dst[di+0] = src[si+0];
                dst[di+1] = src[si+1];
                dst[di+2] = src[si+2];
                dst[di+3] = src[si+3];
            }
        }
    }
}

void App::write_png_file(const std::string& path) {
    if (decoded_image_.rgba.empty()) return;

    if (rotation_ != 0 || flip_h_ || flip_v_) {
        std::vector<uint8_t> transformed;
        int tw, th;
        apply_rotation_flip(decoded_image_.rgba, decoded_image_.width, decoded_image_.height,
                            transformed, tw, th, rotation_, flip_h_, flip_v_);
        stbi_write_png(path.c_str(), tw, th, 4, transformed.data(), tw * 4);
        std::cout << "Saved: " << path << " ("
                  << tw << "x" << th << ", transforms applied)\n";
    } else {
        stbi_write_png(path.c_str(),
                       decoded_image_.width,
                       decoded_image_.height,
                       4,
                       decoded_image_.rgba.data(),
                       decoded_image_.width * 4);
        std::cout << "Saved: " << path << " ("
                  << decoded_image_.width << "x" << decoded_image_.height << ")\n";
    }
}

void App::save_image() {
    if (!image_modified_ || current_path_.empty()) return;
    write_png_file(current_path_);
    image_modified_ = false;
    rotation_ = 0;
    flip_h_ = false;
    flip_v_ = false;
    update_title();
}

void App::save_as() {
    save_dialog_(false);
}

void App::save_as_copy() {
    save_dialog_(true);
}

void App::save_dialog_(bool as_copy) {
    if (decoded_image_.rgba.empty()) return;

    std::string parent_handle = "wayland:wl_surface@";
    uint32_t surface_id = wl_proxy_get_id((struct wl_proxy*)surface_);
    parent_handle += std::to_string(surface_id);

    fs::path current(current_path_);
    std::string suggested = current.empty() ? "image.png" : current.filename().string();
    std::string folder;
    if (!current.empty()) {
        folder = "file://" + current.parent_path().string();
    }

    portal_dialog_.save_file(parent_handle, suggested, folder,
        [this, as_copy](const std::string& path) {
            if (path.empty()) return;
            write_png_file(path);
            image_modified_ = false;
            rotation_ = 0;
            flip_h_ = false;
            flip_v_ = false;
            if (!as_copy) {
                open_file(path);
            } else {
                render();
            }
        });
}

}
