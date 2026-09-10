#include "wl/buffer/cairo_cpu_buffer.hpp"

namespace eh::wayland {

void CairoCpuBuffer::destroy() {
   
  if (cr_) cairo_destroy(cr_);
  cr_ = nullptr;
  if (cairoSurface_) cairo_surface_destroy(cairoSurface_);
  cairoSurface_ = nullptr;
  std::vector<unsigned char>().swap(pixels_);
  width_ = 0;
  height_ = 0;
  stride_ = 0;
}

bool CairoCpuBuffer::ensure(int width, int height) {
   
  if (width <= 0 || height <= 0) return false;
  if (cairoSurface_ && width_ == width && height_ == height) return true;

  destroy();

  width_ = width;
  height_ = height;
  stride_ = static_cast<int>(static_cast<size_t>(width) * 4u);
  const size_t n = static_cast<size_t>(stride_) * static_cast<size_t>(height);
  pixels_.resize(n);

  cairoSurface_ = cairo_image_surface_create_for_data(
      pixels_.data(), CAIRO_FORMAT_ARGB32, width, height, stride_);
  if (!cairoSurface_ || cairo_surface_status(cairoSurface_) != CAIRO_STATUS_SUCCESS) {
    destroy();
    return false;
  }
  cr_ = cairo_create(cairoSurface_);
  if (!cr_) {
    destroy();
    return false;
  }
  return true;
}

}
