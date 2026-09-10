#include "render/text/text_renderer.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace hpv {

TextRenderer::~TextRenderer() {
    delete[] font_data_;
}

TextRenderer::TextRenderer(TextRenderer&& other) noexcept
    : font_data_(other.font_data_), font_size_(other.font_size_), font_scale_(other.font_scale_) {
    other.font_data_ = nullptr;
    other.font_size_ = 0;
}

TextRenderer& TextRenderer::operator=(TextRenderer&& other) noexcept {
    if (this != &other) {
        delete[] font_data_;
        font_data_ = other.font_data_;
        font_size_ = other.font_size_;
        font_scale_ = other.font_scale_;
        other.font_data_ = nullptr;
        other.font_size_ = 0;
    }
    return *this;
}

bool TextRenderer::init(const char* font_path, float font_size) {
    FILE* f = fopen(font_path, "rb");
    if (!f) {
        std::cerr << "text: cannot open font " << font_path << "\n";
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return false;
    }
    font_data_ = new unsigned char[(size_t)sz];
    if (fread(font_data_, 1, (size_t)sz, f) != (size_t)sz) {
        delete[] font_data_;
        font_data_ = nullptr;
        fclose(f);
        return false;
    }
    fclose(f);
    font_size_ = (size_t)sz;

    // Verify it's valid TTF data by attempting to init stb_truetype
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, font_data_, 0)) {
        std::cerr << "text: invalid font data\n";
        delete[] font_data_;
        font_data_ = nullptr;
        return false;
    }

    font_scale_ = font_size;
    std::cout << "text: loaded font \"" << font_path << "\" (" << sz << " bytes)\n";
    return true;
}

RenderedText TextRenderer::render(const std::string& text, uint32_t rgba_color) const {
    RenderedText result;
    if (!font_data_ || text.empty()) return result;

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, font_data_, 0)) return result;

    float scale = stbtt_ScaleForPixelHeight(&info, font_scale_);

    // Measure total width
    float x = 0;
    for (size_t i = 0; i < text.size(); i++) {
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, (unsigned char)text[i], &adv, &lsb);
        x += scale * (float)adv;
        if (i + 1 < text.size()) {
            x += scale * (float)stbtt_GetCodepointKernAdvance(&info, (unsigned char)text[i], (unsigned char)text[i + 1]);
        }
    }

    int w = (int)(x + 2);
    int h = (int)(font_scale_ * 1.5f) + 4;
    if (w < 1 || h < 1) return result;

    // Pad width to 4-byte alignment
    w = (w + 3) & ~3;

    result.rgba.resize((size_t)w * h * 4, 0);
    result.width = w;
    result.height = h;

    uint8_t r = (uint8_t)((rgba_color >> 24) & 0xFF);
    uint8_t g = (uint8_t)((rgba_color >> 16) & 0xFF);
    uint8_t b = (uint8_t)((rgba_color >> 8) & 0xFF);
    uint8_t a = (uint8_t)(rgba_color & 0xFF);
    if (a == 0) a = 255;

    // Render each glyph
    float xpos = 2.0f;
    int y_base = (int)((float)h * 0.75f);

    for (size_t i = 0; i < text.size(); i++) {
        int cp = (unsigned char)text[i];
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, cp, &adv, &lsb);

        int gw = 0, gh = 0, gx = 0, gy = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(&info, scale, scale, cp, &gw, &gh, &gx, &gy);
        if (!bitmap) {
            xpos += scale * (float)adv;
            continue;
        }

        for (int py = 0; py < gh; py++) {
            for (int px = 0; px < gw; px++) {
                int dst_x = (int)xpos + gx + px;
                int dst_y = y_base + gy + py;
                if (dst_x < 0 || dst_x >= w || dst_y < 0 || dst_y >= h) continue;

                uint8_t alpha = bitmap[(size_t)py * (size_t)gw + (size_t)px];
                if (alpha == 0) continue;

                uint8_t* dst = &result.rgba[((size_t)dst_y * (size_t)w + (size_t)dst_x) * 4];
                float src_alpha = (float)alpha / 255.0f * (float)a / 255.0f;
                dst[0] = (uint8_t)((float)dst[0] * (1.0f - src_alpha) + (float)r * src_alpha);
                dst[1] = (uint8_t)((float)dst[1] * (1.0f - src_alpha) + (float)g * src_alpha);
                dst[2] = (uint8_t)((float)dst[2] * (1.0f - src_alpha) + (float)b * src_alpha);
                dst[3] = (uint8_t)((float)dst[3] * (1.0f - src_alpha) + 255.0f * src_alpha);
            }
        }

        stbtt_FreeBitmap(bitmap, nullptr);
        xpos += scale * (float)adv;
        if (i + 1 < text.size()) {
            xpos += scale * (float)stbtt_GetCodepointKernAdvance(&info, cp, (unsigned char)text[i + 1]);
        }
    }

    return result;
}

}
