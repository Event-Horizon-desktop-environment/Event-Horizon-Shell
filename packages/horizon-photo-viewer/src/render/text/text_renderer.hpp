#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hpv {

struct RenderedText {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
};

class TextRenderer {
public:
    TextRenderer() = default;
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    TextRenderer(TextRenderer&&) noexcept;
    TextRenderer& operator=(TextRenderer&&) noexcept;

    bool init(const char* font_path, float font_size = 16.0f);
    bool is_valid() const { return font_data_ != nullptr; }

    RenderedText render(const std::string& text, uint32_t rgba_color = 0xFFFFFFFF) const;

private:
    unsigned char* font_data_ = nullptr;
    size_t font_size_ = 0;
    float font_scale_ = 16.0f;
};

}
