#pragma once

#if defined(EH_HAVE_POLKIT_AGENT)

#include <cstdint>
#include <memory>

struct wl_compositor;
struct wl_display;
struct wl_output;
struct wl_shm;
struct wl_surface;
struct zwlr_layer_shell_v1;
struct wp_viewporter;
struct wp_fractional_scale_manager_v1;

namespace eh::polkit {

class PolkitAuthDialog {
public:
  PolkitAuthDialog(wl_display* display, wl_compositor* compositor, wl_shm* shm,
                   zwlr_layer_shell_v1* layer_shell, wp_viewporter* viewporter,
                   wp_fractional_scale_manager_v1* fractional_scale_mgr);
  ~PolkitAuthDialog();

  void show(wl_output* primary_output = nullptr);
  void hide();
  void repaint();

  wl_surface* surface() const;

  void pointer_motion(double sx, double sy);
  void pointer_button(uint32_t button, uint32_t state);
  // Key events arrive already decoded by the owning connection's seat (sym +
  // UTF-8), so the dialog needs no xkb state of its own.
  bool consume_keyboard_key(uint32_t state, uint32_t sym, const char* utf8, int utf8_len);

  // Exposed for trampoline functions in the same TU
  struct Impl;
  Impl* impl() { return impl_.get(); }
  const Impl* impl() const { return impl_.get(); }

private:
  std::unique_ptr<Impl> impl_;
};

}

#endif
