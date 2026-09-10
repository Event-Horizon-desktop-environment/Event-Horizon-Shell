#pragma once

#include <cairo/cairo.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eh::wayland {

class CairoCpuBuffer {
public:
  CairoCpuBuffer() = default;
  CairoCpuBuffer(const CairoCpuBuffer&) = delete;
  CairoCpuBuffer& operator=(const CairoCpuBuffer&) = delete;
  CairoCpuBuffer(CairoCpuBuffer&&) = delete;
  CairoCpuBuffer& operator=(CairoCpuBuffer&&) = delete;
  ~CairoCpuBuffer() { destroy(); }

  bool ensure(int width, int height);
  void destroy();

  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] int stride() const { return stride_; }
  [[nodiscard]] cairo_surface_t* cairo_surface() const { return cairoSurface_; }
  [[nodiscard]] cairo_t* cairo() const { return cr_; }
  [[nodiscard]] const unsigned char* data() const { return pixels_.empty() ? nullptr : pixels_.data(); }
  [[nodiscard]] unsigned char* data() { return pixels_.empty() ? nullptr : pixels_.data(); }

private:
  std::vector<unsigned char> pixels_;
  cairo_surface_t* cairoSurface_ = nullptr;
  cairo_t* cr_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  int stride_ = 0;
};

}
