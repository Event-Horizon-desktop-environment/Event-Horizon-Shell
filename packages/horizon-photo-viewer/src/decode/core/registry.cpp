#include "decode/core/registry.hpp"

namespace hpv {

void DecoderRegistry::register_decoder(std::unique_ptr<ImageDecoder> decoder) {
    decoders_.push_back(std::move(decoder));
}

DecodeResult DecoderRegistry::decode(const uint8_t* data, size_t size,
                                      int target_width, int target_height) {
    for (auto& decoder : decoders_) {
        if (decoder->can_decode(data, size)) {
            auto result = decoder->decode(data, size, target_width, target_height);
            if (!result.pixels.empty()) return result;
        }
    }
    return DecodeResult{};
}

void DecoderRegistry::prefetch(const uint8_t* data, size_t size,
                                int target_width, int target_height,
                                std::function<void(DecodeResult)> callback) {
    auto result = decode(data, size, target_width, target_height);
    if (callback) callback(std::move(result));
}

}
