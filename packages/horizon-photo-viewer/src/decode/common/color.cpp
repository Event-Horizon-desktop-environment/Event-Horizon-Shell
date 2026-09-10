#include "decode/core/decoder.hpp"
#include <lcms2.h>
#include <vector>
#include <cstdint>
#include <iostream>

namespace hpv {

static bool apply_icc_transform(std::vector<uint8_t>& pixels, int width, int height,
                                 const std::vector<uint8_t>& icc_src,
                                 const std::vector<uint8_t>& icc_dst) {
    if (pixels.empty() || icc_src.empty() || width <= 0 || height <= 0) return false;

    cmsHPROFILE src = cmsOpenProfileFromMem(icc_src.data(), (cmsUInt32Number)icc_src.size());
    if (!src) {
        std::cerr << "lcms: failed to open source ICC profile\n";
        return false;
    }

    cmsHPROFILE dst;
    if (!icc_dst.empty()) {
        dst = cmsOpenProfileFromMem(icc_dst.data(), (cmsUInt32Number)icc_dst.size());
    } else {
        dst = cmsCreate_sRGBProfile();
    }
    if (!dst) {
        cmsCloseProfile(src);
        return false;
    }

    cmsHTRANSFORM xform = cmsCreateTransform(
        src, TYPE_RGBA_8,
        dst, TYPE_RGBA_8,
        INTENT_PERCEPTUAL, 0
    );

    if (!xform) {
        std::cerr << "lcms: failed to create transform\n";
        cmsCloseProfile(dst);
        cmsCloseProfile(src);
        return false;
    }

    cmsDoTransform(xform, pixels.data(), pixels.data(),
                   (cmsUInt32Number)(width * height));

    cmsDeleteTransform(xform);
    cmsCloseProfile(dst);
    cmsCloseProfile(src);
    return true;
}

void apply_color_management(DecodeResult& result) {
    if (result.icc_profile.empty()) return;
    apply_icc_transform(result.pixels, result.width, result.height,
                         result.icc_profile, {});
}

void apply_color_management(DecodeResult& result, const std::vector<uint8_t>& display_profile) {
    if (result.icc_profile.empty()) return;
    apply_icc_transform(result.pixels, result.width, result.height,
                         result.icc_profile, display_profile);
}

}
