#include "decode/formats/heif/decoder.hpp"
#include <libheif/heif.h>
#include <cstring>
#include <iostream>

namespace hpv {

static bool is_heif_magic(const uint8_t* data, size_t size) {
    if (size < 12) return false;
    for (size_t i = 0; i + 8 < size; i++) {
        if (data[i] == 'f' && data[i+1] == 't' && data[i+2] == 'y' && data[i+3] == 'p') {
            if (i + 12 > size) return false;
            return (data[i+4] == 'h' && data[i+5] == 'e' && data[i+6] == 'i' &&
                    (data[i+7] == 'c' || data[i+7] == 'x'));
        }
    }
    return false;
}

bool HeifDecoder::can_decode(const uint8_t* data, size_t size) {
    return is_heif_magic(data, size);
}

DecodeResult HeifDecoder::decode(const uint8_t* data, size_t size,
                                  int /*target_width*/, int /*target_height*/) {
    DecodeResult result;
    result.format_name = "HEIF";

    struct heif_context* ctx = heif_context_alloc();
    if (!ctx) {
        std::cerr << "heif: failed to allocate context\n";
        return result;
    }

    struct heif_error err = heif_context_read_from_memory(ctx, data, size, nullptr);
    if (err.code != 0) {
        std::cerr << "heif: " << err.message << "\n";
        heif_context_free(ctx);
        return result;
    }

    struct heif_image_handle* handle;
    err = heif_context_get_primary_image_handle(ctx, &handle);
    if (err.code != 0 || !handle) {
        std::cerr << "heif: no primary image\n";
        heif_context_free(ctx);
        return result;
    }

    result.width = heif_image_handle_get_width(handle);
    result.height = heif_image_handle_get_height(handle);

    heif_color_profile_nclx* nclx = nullptr;
    if (heif_image_handle_get_nclx_color_profile(handle, &nclx) == 0 && nclx) {
        heif_color_profile_nclx_free(nclx);
    }

    size_t icc_size = heif_image_handle_get_raw_color_profile_size(handle);
    if (icc_size > 0) {
        result.icc_profile.resize(icc_size);
        heif_image_handle_get_raw_color_profile(handle, result.icc_profile.data());
    }

    struct heif_image* image;
    err = heif_decode_image(handle, &image, heif_colorspace_RGB,
                            heif_chroma_interleaved_RGBA, nullptr);
    if (err.code != 0 || !image) {
        std::cerr << "heif: decode failed: " << err.message << "\n";
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return result;
    }

    int stride;
    const uint8_t* src = heif_image_get_plane_readonly(image, heif_channel_interleaved, &stride);
    if (!src) {
        std::cerr << "heif: no plane data\n";
        heif_image_release(image);
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return result;
    }

    size_t pixel_count = (size_t)result.width * result.height;
    result.pixels.resize(pixel_count * 4);

    const uint8_t* row_src = src;
    uint8_t* dst = result.pixels.data();
    for (int y = 0; y < result.height; y++) {
        std::memcpy(dst, row_src, (size_t)result.width * 4);
        dst += (size_t)result.width * 4;
        row_src += stride;
    }

    heif_image_release(image);
    heif_image_handle_release(handle);
    heif_context_free(ctx);

    return result;
}

}
