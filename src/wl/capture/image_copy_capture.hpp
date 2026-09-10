#pragma once

#include "wl/core/protocols.hpp"

struct ext_image_capture_source_v1;

namespace eh::wayland {

class ImageCopyCapture {
public:
  ImageCopyCapture() = default;
  ImageCopyCapture(const ImageCopyCapture&) = delete;
  ImageCopyCapture& operator=(const ImageCopyCapture&) = delete;
  ImageCopyCapture(ImageCopyCapture&& other) noexcept;
  ImageCopyCapture& operator=(ImageCopyCapture&& other) noexcept;
  ~ImageCopyCapture();

  void bind(ext_image_copy_capture_manager_v1* manager) { manager_ = manager; }

  ext_image_copy_capture_manager_v1* get() const { return manager_; }

  explicit operator bool() const { return manager_ != nullptr; }

private:
  ext_image_copy_capture_manager_v1* manager_ = nullptr;
};

} // namespace eh::wayland
