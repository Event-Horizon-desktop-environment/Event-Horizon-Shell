#include "decode/formats/avif/decoder.hpp"
#include <avif/avif.h>
#include <cstring>
#include <iostream>

namespace hpv {

bool AvifDecoder::can_decode(const uint8_t* data, size_t size) {
    if (size < 12) return false;
    for (size_t i = 0; i + 8 < size; i++) {
        if (data[i] == 'f' && data[i+1] == 't' && data[i+2] == 'y' && data[i+3] == 'p') {
            if (i + 12 > size) return false;
            return (data[i+4] == 'a' && data[i+5] == 'v' && data[i+6] == 'i' && data[i+7] == 'f') ||
                   (data[i+4] == 'a' && data[i+5] == 'v' && data[i+6] == 'i' && data[i+7] == 's');
        }
    }
    return false;
}

DecodeResult AvifDecoder::decode(const uint8_t* data, size_t size,
                                  int /*target_width*/, int /*target_height*/) {
    DecodeResult result;
    result.format_name = "AVIF";

    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder) {
        std::cerr << "avif: failed to create decoder\n";
        return result;
    }

    avifResult parse = avifDecoderSetIOMemory(decoder, data, size);
    if (parse != AVIF_RESULT_OK) {
        std::cerr << "avif: set IO failed: " << avifResultToString(parse) << "\n";
        avifDecoderDestroy(decoder);
        return result;
    }

    avifResult parse_next = avifDecoderParse(decoder);
    if (parse_next != AVIF_RESULT_OK) {
        std::cerr << "avif: parse failed: " << avifResultToString(parse_next) << "\n";
        avifDecoderDestroy(decoder);
        return result;
    }

    avifResult img = avifDecoderNextImage(decoder);
    if (img != AVIF_RESULT_OK) {
        std::cerr << "avif: next image failed: " << avifResultToString(img) << "\n";
        avifDecoderDestroy(decoder);
        return result;
    }

    result.width = decoder->image->width;
    result.height = decoder->image->height;

    avifRWData icc_raw = AVIF_DATA_EMPTY;
    avifResult icc_r = avifDecoderGetICC(decoder, &icc_raw);
    if (icc_r == AVIF_RESULT_OK && icc_raw.data && icc_raw.size > 0) {
        result.icc_profile.assign(icc_raw.data, icc_raw.data + icc_raw.size);
    }
    avifRWDataFree(&icc_raw);

    avifRGBImage rgb;
    memset(&rgb, 0, sizeof(rgb));
    avifRGBImageSetDefaults(&rgb, decoder->image);
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    rgb.depth = 8;
    avifRGBImageAllocatePixels(&rgb);

    avifResult convert = avifImageRGBToPixels(decoder->image, &rgb);
    if (convert != AVIF_RESULT_OK) {
        std::cerr << "avif: RGB convert failed: " << avifResultToString(convert) << "\n";
        avifRGBImageFreePixels(&rgb);
        avifDecoderDestroy(decoder);
        return result;
    }

    size_t pixel_count = (size_t)rgb.width * rgb.height;
    result.pixels.resize(pixel_count * 4);
    std::memcpy(result.pixels.data(), rgb.pixels, pixel_count * 4);

    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(decoder);

    return result;
}

}
