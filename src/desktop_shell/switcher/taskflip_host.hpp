#pragma once

#include "desktop_shell/switcher/taskflip_model.hpp"
#include "desktop_shell/switcher/taskflip_paint.hpp"
#include "wl/buffer/shm_buffer.hpp"

#include <cstdint>
#include <memory>

struct wl_display;
struct wl_surface;
struct wl_output;
struct zwlr_layer_surface_v1;
struct DockApp;

namespace eh::wayland {
class WaylandConnection;
}

namespace eh::shell::taskflip {

struct HostConfig {
  bool galleryStyle = true;
  bool showCaption = true;
  bool showCount = true;
  bool showIcons = true;
  bool mruOrder = true;
};

class Host {
 public:
  Host(DockApp& dock, HostConfig cfg);
  ~Host();

  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  void open();
  void close();
  void toggle();
  void next();
  void prev();
  void confirm();
  void syncFromToplevels();

  static void onConfigure(void* data, zwlr_layer_surface_v1* layer, uint32_t serial,
                          uint32_t width, uint32_t height);

 private:
  void createSurface(wl_output* output);
  void destroySurface();
  void paint();

  DockApp& dock_;
  HostConfig cfg_;
  Model model_;
  PaintStyle style_{};
  bool open_ = false;
  wl_surface* surface_ = nullptr;
  zwlr_layer_surface_v1* layer_ = nullptr;
  wl_output* output_ = nullptr;
  eh::wayland::ShmBuffer shmBuf_{};
  int width_ = 0;
  int height_ = 0;
  bool configured_ = false;
};

}
