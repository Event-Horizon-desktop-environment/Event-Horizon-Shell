#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hpv {

struct DecodeResult {
    std::vector<uint8_t> pixels;  // RGBA non-premultiplied
    int width = 0;
    int height = 0;
    int exif_orientation = 1;
    std::string format_name;
    std::vector<uint8_t> icc_profile;
};

#ifdef HAVE_LCMS2
void apply_color_management(DecodeResult& result);
void apply_color_management(DecodeResult& result, const std::vector<uint8_t>& display_profile);
#endif

}
