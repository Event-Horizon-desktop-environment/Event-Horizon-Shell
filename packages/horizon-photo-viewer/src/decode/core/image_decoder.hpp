#pragma once

#include "decode/core/result.hpp"

#include <memory>

namespace hpv {

class ImageDecoder {
public:
    virtual ~ImageDecoder() = default;
    virtual bool can_decode(const uint8_t* data, size_t size) = 0;
    virtual DecodeResult decode(const uint8_t* data, size_t size,
                                 int target_width = 0, int target_height = 0) = 0;
    virtual const char* name() const = 0;
};

class StbDecoder : public ImageDecoder {
public:
    bool can_decode(const uint8_t* data, size_t size) override;
    DecodeResult decode(const uint8_t* data, size_t size,
                         int target_width = 0, int target_height = 0) override;
    const char* name() const override { return "stb_image"; }
};

}
