#pragma once

#include "decode/core/image_decoder.hpp"

namespace hpv {

class AvifDecoder : public ImageDecoder {
public:
    bool can_decode(const uint8_t* data, size_t size) override;
    DecodeResult decode(const uint8_t* data, size_t size,
                         int target_width = 0, int target_height = 0) override;
    const char* name() const override { return "AVIF"; }
};

}
