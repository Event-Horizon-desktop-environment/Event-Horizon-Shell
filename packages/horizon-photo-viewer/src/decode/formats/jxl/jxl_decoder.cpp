#include "decode/formats/jxl/decoder.hpp"
#include <jxl/decode.h>
#include <cstring>
#include <iostream>

namespace hpv {

static bool is_jxl_magic(const uint8_t* data, size_t size) {
    if (size < 12) return false;
    const uint8_t magic[] = {0x00, 0x00, 0x00, 0x0C, 0x4A, 0x58, 0x4C, 0x20, 0x0D, 0x0A, 0x87, 0x0A};
    return std::memcmp(data, magic, 12) == 0;
}

bool JxlDecoder::can_decode(const uint8_t* data, size_t size) {
    return is_jxl_magic(data, size);
}

DecodeResult JxlDecoder::decode(const uint8_t* data, size_t size,
                                 int /*target_width*/, int /*target_height*/) {
    DecodeResult result;
    result.format_name = "JPEG-XL";

    ::JxlDecoder* dec = ::JxlDecoderCreate(nullptr);
    if (!dec) {
        std::cerr << "JxlDecoderCreate failed\n";
        return result;
    }

    // Single-threaded decode (parallel runner library not available)
    JxlDecoderSetParallelRunner(dec, nullptr, nullptr);

    JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE);
    JxlDecoderSetInput(dec, data, size);
    JxlDecoderCloseInput(dec);

    JxlBasicInfo info{};
    bool info_got = false;

    for (;;) {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec);

        if (status == JXL_DEC_ERROR) {
            std::cerr << "JXL decode error\n";
            break;
        }
        if (status == JXL_DEC_SUCCESS) break;

        if (status == JXL_DEC_BASIC_INFO) {
            if (JxlDecoderGetBasicInfo(dec, &info) == JXL_DEC_SUCCESS) {
                info_got = true;
            }
        }

        if (status == JXL_DEC_COLOR_ENCODING) {
            size_t icc_size = 0;
            if (JxlDecoderGetICCProfileSize(dec, JXL_COLOR_PROFILE_TARGET_DATA, &icc_size) == JXL_DEC_SUCCESS && icc_size > 0) {
                std::vector<uint8_t> icc(icc_size);
                if (JxlDecoderGetColorAsICCProfile(dec, JXL_COLOR_PROFILE_TARGET_DATA, icc.data(), icc_size) == JXL_DEC_SUCCESS) {
                    result.icc_profile = std::move(icc);
                }
            }
        }

        if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            if (!info_got) break;

            JxlPixelFormat format{4, JXL_TYPE_UINT8, JXL_LITTLE_ENDIAN, 0};
            size_t bpp = 0;
            if (JxlDecoderImageOutBufferSize(dec, &format, &bpp) != JXL_DEC_SUCCESS) break;

            result.width = (int)info.xsize;
            result.height = (int)info.ysize;
            result.pixels.resize(bpp);

            void* buf = result.pixels.data();
            if (JxlDecoderSetImageOutBuffer(dec, &format, buf, bpp) != JXL_DEC_SUCCESS) {
                result.pixels.clear();
                break;
            }
        }
    }

    JxlDecoderDestroy(dec);

    return result;
}

}
