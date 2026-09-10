#pragma once

#include "decode/core/result.hpp"
#include "decode/core/image_decoder.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace hpv {

class DecoderRegistry {
public:
    void register_decoder(std::unique_ptr<ImageDecoder> decoder);

    DecodeResult decode(const uint8_t* data, size_t size,
                         int target_width = 0, int target_height = 0);

    void prefetch(const uint8_t* data, size_t size,
                   int target_width, int target_height,
                   std::function<void(DecodeResult)> callback);

private:
    std::vector<std::unique_ptr<ImageDecoder>> decoders_;
};

}
