#include "decode/formats/wuffs/decoder.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <cstring>
#include <iostream>

namespace hpv {

namespace {

bool match_magic(const uint8_t* data, size_t size, const uint8_t* magic, size_t magic_len) {
    if (size < magic_len) return false;
    return std::memcmp(data, magic, magic_len) == 0;
}

bool is_png(const uint8_t* data, size_t size) {
    const uint8_t png_magic[] = {0x89, 0x50, 0x4E, 0x47};
    return match_magic(data, size, png_magic, 4);
}

bool is_gif(const uint8_t* data, size_t size) {
    const uint8_t gif_magic[] = {'G', 'I', 'F', '8'};
    return match_magic(data, size, gif_magic, 4);
}

bool is_bmp(const uint8_t* data, size_t size) {
    const uint8_t bmp_magic[] = {'B', 'M'};
    return match_magic(data, size, bmp_magic, 2);
}

bool is_tiff(const uint8_t* data, size_t size) {
    if (size < 4) return false;
    return (data[0] == 'I' && data[1] == 'I' && data[2] == 0x2A && data[3] == 0x00) ||
           (data[0] == 'M' && data[1] == 'M' && data[2] == 0x00 && data[3] == 0x2A);
}

}

// --- WuffsDecoder (stub - will use Wuffs C amalgamation later) ---

bool WuffsDecoder::can_decode(const uint8_t* data, size_t size) {
    return is_png(data, size) || is_gif(data, size) || is_bmp(data, size) || is_tiff(data, size);
}

DecodeResult WuffsDecoder::decode(const uint8_t* /*data*/, size_t /*size*/,
                                   int /*target_width*/, int /*target_height*/) {
    return DecodeResult{};
}

// --- StbDecoder (universal fallback via stb_image.h) ---

bool StbDecoder::can_decode(const uint8_t* /*data*/, size_t /*size*/) {
    return true;
}

DecodeResult StbDecoder::decode(const uint8_t* data, size_t size,
                                 int /*target_width*/, int /*target_height*/) {
    DecodeResult result;
    int w = 0, h = 0, channels = 0;

    unsigned char* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    if (!pixels) {
        std::cerr << "stb_image: " << stbi_failure_reason() << "\n";
        return result;
    }

    result.width = w;
    result.height = h;
    result.pixels.resize((size_t)w * h * 4);
    std::memcpy(result.pixels.data(), pixels, (size_t)w * h * 4);
    stbi_image_free(pixels);
    return result;
}

}
