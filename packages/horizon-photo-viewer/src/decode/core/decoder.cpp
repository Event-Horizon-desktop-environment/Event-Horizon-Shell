#include "decode/core/decoder.hpp"

#include <cstring>
#include <iostream>

namespace hpv {

// Magic byte detection helpers for stub implementations
namespace {

#ifndef HAVE_LIBJPEG
bool is_jpeg(const uint8_t* data, size_t size) {
    if (size < 3) return false;
    const uint8_t magic[] = {0xFF, 0xD8, 0xFF};
    return std::memcmp(data, magic, 3) == 0;
}
#endif

#ifndef HAVE_LIBWEBP
bool is_webp(const uint8_t* data, size_t size) {
    if (size < 12) return false;
    return (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
            data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P');
}
#endif

#ifndef HAVE_LIBHEIF
bool is_heif(const uint8_t* data, size_t size) {
    if (size < 12) return false;
    for (size_t i = 0; i + 8 < size; i++) {
        if (data[i] == 'f' && data[i+1] == 't' && data[i+2] == 'y' && data[i+3] == 'p') {
            if (i + 12 > size) return false;
            return (data[i+4] == 'h' && data[i+5] == 'e' && data[i+6] == 'i' && (data[i+7] == 'c' || data[i+7] == 'x')) ||
                   (data[i+4] == 'm' && data[i+5] == 'i' && data[i+6] == 'f' && data[i+7] == '1') ||
                   (data[i+4] == 'a' && data[i+5] == 'v' && data[i+6] == 'i' && data[i+7] == 'f');
        }
    }
    return false;
}
#endif

#ifndef HAVE_LIBAVIF
bool is_avif(const uint8_t* data, size_t size) {
    if (size < 12) return false;
    for (size_t i = 0; i + 12 <= size; i++) {
        if (data[i] == 'f' && data[i+1] == 't' && data[i+2] == 'y' && data[i+3] == 'p') {
            if (i + 12 > size) return false;
            return (data[i+4] == 'a' && data[i+5] == 'v' && data[i+6] == 'i' && data[i+7] == 'f');
        }
    }
    return false;
}
#endif

#ifndef HAVE_LIBJXL
bool is_jxl(const uint8_t* data, size_t size) {
    if (size < 12) return false;
    const uint8_t magic[] = {0x00, 0x00, 0x00, 0x0C, 0x4A, 0x58, 0x4C, 0x20, 0x0D, 0x0A, 0x87, 0x0A};
    return std::memcmp(data, magic, 12) == 0;
}
#endif

}

#ifndef HAVE_LIBWEBP
bool WebPDecoder::can_decode(const uint8_t* data, size_t size) {
    return is_webp(data, size);
}

DecodeResult WebPDecoder::decode(const uint8_t* /*data*/, size_t /*size*/,
                                  int /*target_width*/, int /*target_height*/) {
    return DecodeResult{};
}
#endif

#ifndef HAVE_LIBJPEG
bool JpegDecoder::can_decode(const uint8_t* data, size_t size) {
    return is_jpeg(data, size);
}

DecodeResult JpegDecoder::decode(const uint8_t* /*data*/, size_t /*size*/,
                                  int /*target_width*/, int /*target_height*/) {
    return DecodeResult{};
}
#endif

#ifndef HAVE_LIBHEIF
bool HeifDecoder::can_decode(const uint8_t* data, size_t size) {
    return is_heif(data, size) && !is_avif(data, size);
}

DecodeResult HeifDecoder::decode(const uint8_t* /*data*/, size_t /*size*/,
                                  int /*target_width*/, int /*target_height*/) {
    return DecodeResult{};
}
#endif

#ifndef HAVE_LIBAVIF
bool AvifDecoder::can_decode(const uint8_t* data, size_t size) {
    return is_avif(data, size);
}

DecodeResult AvifDecoder::decode(const uint8_t* /*data*/, size_t /*size*/,
                                  int /*target_width*/, int /*target_height*/) {
    return DecodeResult{};
}
#endif

#ifndef HAVE_LIBRAW
bool RawDecoder::can_decode(const uint8_t* /*data*/, size_t /*size*/) {
    return false;
}

DecodeResult RawDecoder::decode(const uint8_t* /*data*/, size_t /*size*/,
                                  int /*target_width*/, int /*target_height*/) {
    return DecodeResult{};
}
#endif

#ifndef HAVE_LIBJXL
bool JxlDecoder::can_decode(const uint8_t* data, size_t size) {
    return is_jxl(data, size);
}

DecodeResult JxlDecoder::decode(const uint8_t* /*data*/, size_t /*size*/,
                                  int /*target_width*/, int /*target_height*/) {
    return DecodeResult{};
}
#endif

}
