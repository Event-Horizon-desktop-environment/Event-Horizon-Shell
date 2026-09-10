#include "decode/formats/webp/decoder.hpp"
#include <webp/decode.h>
#include <cstring>
#include <iostream>

namespace hpv {

static bool is_webp_magic(const uint8_t* data, size_t size) {
    if (size < 12) return false;
    return data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
           data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P';
}

bool WebPDecoder::can_decode(const uint8_t* data, size_t size) {
    return is_webp_magic(data, size);
}

DecodeResult WebPDecoder::decode(const uint8_t* data, size_t size,
                                  int target_width, int target_height) {
    if (target_width > 0 && target_height > 0) {
        DecodeResult result;
        result.format_name = "WebP";
        WebPDecoderConfig config{};
        if (!WebPInitDecoderConfig(&config)) return result;
        config.options.use_scaling = 1;
        config.options.scaled_width = target_width;
        config.options.scaled_height = target_height;
        config.output.colorspace = MODE_RGBA;
        if (WebPDecode(data, size, &config) != VP8_STATUS_OK) return result;
        result.width = (int)config.output.width;
        result.height = (int)config.output.height;
        size_t pixel_count = (size_t)result.width * result.height;
        result.pixels.resize(pixel_count * 4);
        std::memcpy(result.pixels.data(), config.output.u.RGBA.rgba, pixel_count * 4);
        WebPFreeDecBuffer(&config.output);
        return result;
    }

    DecodeResult result;
    result.format_name = "WebP";

    int w = 0, h = 0;
    uint8_t* rgba = WebPDecodeRGBA(data, size, &w, &h);
    if (!rgba) {
        std::cerr << "WebP decode failed\n";
        return result;
    }

    result.width = w;
    result.height = h;
    result.pixels.resize((size_t)w * h * 4);
    std::memcpy(result.pixels.data(), rgba, (size_t)w * h * 4);
    WebPFree(rgba);

    return result;
}

}
