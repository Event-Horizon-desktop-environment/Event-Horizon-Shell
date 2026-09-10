#pragma once

#include "wl/core/protocols.hpp"

#include <cstdint>

struct wl_buffer;

namespace eh::wayland {

class SinglePixelBuffer {
public:
  SinglePixelBuffer() = default;
  SinglePixelBuffer(const SinglePixelBuffer&) = delete;
  SinglePixelBuffer& operator=(const SinglePixelBuffer&) = delete;
  SinglePixelBuffer(SinglePixelBuffer&& other) noexcept;
  SinglePixelBuffer& operator=(SinglePixelBuffer&& other) noexcept;
  ~SinglePixelBuffer();

  void bind(wp_single_pixel_buffer_manager_v1* manager) { manager_ = manager; }

  wl_buffer* create_buffer(uint32_t r, uint32_t g, uint32_t b, uint32_t a);

  explicit operator bool() const { return manager_ != nullptr; }

private:
  wp_single_pixel_buffer_manager_v1* manager_ = nullptr;
};

} // namespace eh::wayland
