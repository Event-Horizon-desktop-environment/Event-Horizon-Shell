#include "ui/overlay.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>

#include <stb/stb_truetype.h>
#define NANOSVG_IMPLEMENTATION
#include <nanosvg/nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg/nanosvgrast.h>

namespace hpv {

Overlay::~Overlay() {
    destroy_icons();
}

void Overlay::draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    cairo_move_to(cr, x + r, y);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, -M_PI_2);
    cairo_close_path(cr);
}

bool Overlay::ready_icon_from_codepoint(unsigned char* fdata, size_t /*fsize*/,
                                         int codepoint, int icon_size) {
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, fdata, 0)) return false;

    float scale = stbtt_ScaleForPixelHeight(&info, (float)icon_size);
    int gw, gh, gx, gy;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(&info, scale, scale, codepoint, &gw, &gh, &gx, &gy);
    if (!bitmap || gw <= 0 || gh <= 0) {
        if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);
        icon_surfaces_.push_back(nullptr);
        return false;
    }

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gw, gh);
    unsigned char* data = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);

    for (int y = 0; y < gh; y++) {
        for (int x = 0; x < gw; x++) {
            uint8_t a = bitmap[(size_t)y * (size_t)gw + (size_t)x];
            size_t off = (size_t)y * (size_t)stride + (size_t)x * 4;
            data[off + 0] = 0;
            data[off + 1] = 0;
            data[off + 2] = 0;
            data[off + 3] = a;
        }
    }
    cairo_surface_mark_dirty(surf);
    icon_surfaces_.push_back(surf);

    stbtt_FreeBitmap(bitmap, nullptr);
    return true;
}

bool Overlay::ready_icon_from_svg(const char* svg_path, int icon_size) {
    NSVGimage* img = nsvgParseFromFile(svg_path, "px", 96);
    if (!img) {
        icon_surfaces_.push_back(nullptr);
        return false;
    }

    float scale = (float)icon_size / fmaxf(img->width, img->height);

    unsigned char* rgba = (unsigned char*)malloc((size_t)icon_size * icon_size * 4);
    if (!rgba) {
        std::cerr << "ready_icon_from_svg: malloc(" << (icon_size * icon_size * 4) << ") failed\n";
        nsvgDelete(img);
        icon_surfaces_.push_back(nullptr);
        return false;
    }
    memset(rgba, 0, (size_t)icon_size * icon_size * 4);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        std::cerr << "ready_icon_from_svg: nsvgCreateRasterizer failed\n";
        free(rgba);
        nsvgDelete(img);
        icon_surfaces_.push_back(nullptr);
        return false;
    }

    nsvgRasterize(rast, img, 0, 0, scale, rgba, icon_size, icon_size, icon_size * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);

    // Create a cairo surface from the RGBA buffer — use ARGB32 (BGRA on little-endian)
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, icon_size, icon_size);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        std::cerr << "ready_icon_from_svg: cairo_image_surface_create failed\n";
        free(rgba);
        icon_surfaces_.push_back(nullptr);
        return false;
    }

    // nanosvg outputs premultiplied RGBA, cairo expects premultiplied ARGB32 (BGRA on LE)
    unsigned char* dst = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    for (int y = 0; y < icon_size; y++) {
        for (int x = 0; x < icon_size; x++) {
            int si = (y * icon_size + x) * 4;
            int di = y * stride + x * 4;
            dst[di + 0] = rgba[si + 2]; // B
            dst[di + 1] = rgba[si + 1]; // G
            dst[di + 2] = rgba[si + 0]; // R
            dst[di + 3] = rgba[si + 3]; // A
        }
    }
    cairo_surface_mark_dirty(surf);
    free(rgba);

    icon_surfaces_.push_back(surf);
    return true;
}

bool Overlay::init_icons(const char* font_path, const char* crop_svg_path,
                         const char* flip_svg_path) {
    destroy_icons();

    FILE* f = fopen(font_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }

    font_data_ = new unsigned char[(size_t)sz];
    font_size_ = (size_t)sz;
    if (fread(font_data_, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        destroy_icons();
        return false;
    }
    fclose(f);

    icon_surfaces_.reserve(kNumIcons);
    for (int i = 0; i < kNumIcons; i++) {
        if (i == 10 && crop_svg_path) {
            ready_icon_from_svg(crop_svg_path, kIconSize);
        } else if (i == 15 && flip_svg_path) {
            ready_icon_from_svg(flip_svg_path, kIconSize);
        } else {
            ready_icon_from_codepoint(font_data_, font_size_, kIconCodepoints[i], kIconSize);
        }
    }

    // Mirror RotR icon (index 13) — shares the left-pointing codepoint with RotL
    if (icon_surfaces_.size() > 13 && icon_surfaces_[13]) {
        cairo_surface_t* src = icon_surfaces_[13];
        int iw = cairo_image_surface_get_width(src);
        int ih = cairo_image_surface_get_height(src);
        cairo_surface_t* flipped = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
        cairo_t* cr = cairo_create(flipped);
        cairo_scale(cr, -1, 1);
        cairo_translate(cr, -iw, 0);
        cairo_set_source_surface(cr, src, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(src);
        icon_surfaces_[13] = flipped;
    }

    return !icon_surfaces_.empty();
}

bool Overlay::init_screenshot_icons(const char* screen_svg, const char* window_svg,
                                     const char* focused_svg, const char* selection_svg,
                                     const char* copy_svg) {
    // Ensure vector is sized for all kNumIcons
    if (icon_surfaces_.size() < static_cast<size_t>(kNumIcons)) {
        icon_surfaces_.resize(kNumIcons, nullptr);
    }

    // Indices 16-20: Screen, Window, Focused, Selection, Copy
    const char* paths[] = { screen_svg, window_svg, focused_svg, selection_svg, copy_svg };
    for (int i = 0; i < 5; i++) {
        int idx = kScreenIconIdx + i;
        if (icon_surfaces_[idx]) {
            cairo_surface_destroy(icon_surfaces_[idx]);
            icon_surfaces_[idx] = nullptr;
        }
        if (paths[i]) {
            ready_icon_from_svg(paths[i], kIconSize);
            if (!icon_surfaces_.empty()) {
                icon_surfaces_[idx] = icon_surfaces_.back();
                icon_surfaces_.pop_back();
            }
        }
    }

    for (int i = 0; i < 5; i++) {
        if (!icon_surfaces_[kScreenIconIdx + i])
            return false;
    }
    return true;
}

bool Overlay::init_panel_icon(const char* panel_svg) {
    if (icon_surfaces_.size() < static_cast<size_t>(kNumIcons)) {
        icon_surfaces_.resize(kNumIcons, nullptr);
    }
    if (kPanelIconIdx >= (int)icon_surfaces_.size()) {
        icon_surfaces_.resize(kPanelIconIdx + 1, nullptr);
    }
    if (icon_surfaces_[kPanelIconIdx]) {
        cairo_surface_destroy(icon_surfaces_[kPanelIconIdx]);
        icon_surfaces_[kPanelIconIdx] = nullptr;
    }
    if (panel_svg) {
        ready_icon_from_svg(panel_svg, kIconSize);
        if (!icon_surfaces_.empty()) {
            icon_surfaces_[kPanelIconIdx] = icon_surfaces_.back();
            icon_surfaces_.pop_back();
        }
        return icon_surfaces_[kPanelIconIdx] != nullptr;
    }
    return false;
}

bool Overlay::init_upload_icon(const char* upload_svg) {
    if (icon_surfaces_.size() < static_cast<size_t>(kNumIcons)) {
        icon_surfaces_.resize(kNumIcons, nullptr);
    }
    if (kUploadIconIdx >= (int)icon_surfaces_.size()) {
        icon_surfaces_.resize(kUploadIconIdx + 1, nullptr);
    }
    if (icon_surfaces_[kUploadIconIdx]) {
        cairo_surface_destroy(icon_surfaces_[kUploadIconIdx]);
        icon_surfaces_[kUploadIconIdx] = nullptr;
    }
    if (upload_svg) {
        ready_icon_from_svg(upload_svg, kIconSize);
        if (!icon_surfaces_.empty()) {
            icon_surfaces_[kUploadIconIdx] = icon_surfaces_.back();
            icon_surfaces_.pop_back();
        }
        return icon_surfaces_[kUploadIconIdx] != nullptr;
    }
    return false;
}

void Overlay::destroy_icons() {
    for (auto* surf : icon_surfaces_) {
        if (surf) cairo_surface_destroy(surf);
    }
    icon_surfaces_.clear();
    delete[] font_data_;
    font_data_ = nullptr;
    font_size_ = 0;
}

void Overlay::render_overlay(cairo_t* cr, int win_w, int win_h, const OverlayState& state) {
    if (state.image_width <= 0) {
        render_placeholder(cr, win_w, win_h);
    }

    if (state.show_info && !state.filename.empty()) {
        render_info(cr, win_w, win_h, state);
    }
}

} // namespace hpv
