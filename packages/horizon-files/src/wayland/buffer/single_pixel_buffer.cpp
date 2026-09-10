#include "wayland/buffer/single_pixel_buffer.hpp"

#include <utility>
#include <wayland-client-protocol.h>

namespace eh::wayland {

SinglePixelBuffer::SinglePixelBuffer(SinglePixelBuffer&& other) noexcept
    : manager_(std::exchange(other.manager_, nullptr)) {}

SinglePixelBuffer& SinglePixelBuffer::operator=(SinglePixelBuffer&& other) noexcept {
  if (this != &other) {
    manager_ = std::exchange(other.manager_, nullptr);
  }
  return *this;
}

SinglePixelBuffer::~SinglePixelBuffer() {
  // Manager is a global; we don't own its lifetime.
  manager_ = nullptr;
}

wl_buffer* SinglePixelBuffer::create_buffer(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
  if (!manager_) return nullptr;
  return wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(manager_, r, g, b, a);
}

} // namespace eh::wayland
