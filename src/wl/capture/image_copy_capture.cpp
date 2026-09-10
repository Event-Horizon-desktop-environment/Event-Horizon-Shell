#include "wl/capture/image_copy_capture.hpp"

#include <utility>

namespace eh::wayland {

ImageCopyCapture::ImageCopyCapture(ImageCopyCapture&& other) noexcept
    : manager_(std::exchange(other.manager_, nullptr)) {}

ImageCopyCapture& ImageCopyCapture::operator=(ImageCopyCapture&& other) noexcept {
  if (this != &other) {
    manager_ = std::exchange(other.manager_, nullptr);
  }
  return *this;
}

ImageCopyCapture::~ImageCopyCapture() {
  // The manager is a global proxy; WaylandConnection owns its lifetime.
  manager_ = nullptr;
}

} // namespace eh::wayland
