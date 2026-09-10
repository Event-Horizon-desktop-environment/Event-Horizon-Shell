#pragma once

#include <cairo.h>
#include <wayland-client.h>

#include <cstddef>

namespace hpv::sc {

class ShmBuffer {
public:
  using ReleaseHook = void (*)(void* user);

  ShmBuffer() = default;
  ShmBuffer(const ShmBuffer&) = delete;
  ShmBuffer& operator=(const ShmBuffer&) = delete;
  ShmBuffer(ShmBuffer&&) = delete;
  ShmBuffer& operator=(ShmBuffer&&) = delete;
  ~ShmBuffer();

  void set_release_hook(ReleaseHook hook, void* user);
  bool ensure(wl_shm* shm, const char* tag, int width, int height);
  void destroy();

  [[nodiscard]] bool busy() const { return busy_; }
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] int stride() const { return stride_; }
  [[nodiscard]] size_t size() const { return size_; }
  [[nodiscard]] wl_buffer* wl() const { return buffer_; }
  [[nodiscard]] cairo_surface_t* cairo_surface() const { return cairoSurface_; }
  [[nodiscard]] cairo_t* cairo() const { return cr_; }
  [[nodiscard]] void* data() const { return data_; }
  void mark_busy() { busy_ = true; }

private:
  static void wl_release(void* data, wl_buffer* buffer);
  static constexpr wl_buffer_listener kListener_ = {.release = wl_release};

  void* data_ = nullptr;
  size_t size_ = 0;
  int width_ = 0;
  int height_ = 0;
  int stride_ = 0;
  bool busy_ = false;
  wl_buffer* buffer_ = nullptr;
  cairo_surface_t* cairoSurface_ = nullptr;
  cairo_t* cr_ = nullptr;
  ReleaseHook hook_ = nullptr;
  void* hookUser_ = nullptr;
};

}
