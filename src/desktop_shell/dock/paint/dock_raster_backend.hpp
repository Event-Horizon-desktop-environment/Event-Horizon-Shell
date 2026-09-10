#pragma once

#include <cairo/cairo.h>

#include "wl/buffer/damage_region.hpp"

struct DockApp;
struct DockOutputLayer;

namespace eh::dock {

void clear_layer_rasters(DockOutputLayer& layer);
void clear_popup_gl_raster(DockApp& app);

class IDockRasterBackend {
public:
  virtual ~IDockRasterBackend() = default;

  [[nodiscard]] virtual bool ensure(DockApp& app, DockOutputLayer& layer, int buf_w, int buf_h) = 0;
  [[nodiscard]] virtual cairo_t* cairo_ctx(DockOutputLayer& layer) const = 0;
  [[nodiscard]] virtual cairo_surface_t* cairo_surface(DockOutputLayer& layer) const = 0;
  // `frame_damage` describes which buffer pixels were repainted this frame.
  // Backends may use it to skip uploads and to drive wl_surface damage.
  [[nodiscard]] virtual bool present(DockApp& app, DockOutputLayer& layer, int buf_w, int buf_h,
                                     const eh::wayland::DamageRegion& frame_damage) = 0;
  [[nodiscard]] virtual bool buffer_busy(const DockOutputLayer& layer) const = 0;
  [[nodiscard]] virtual bool uses_gpu_present() const noexcept = 0;
};

[[nodiscard]] bool use_vulkan_backend(const DockApp& app);

void abort_vk_backend(DockApp& app);

void drain_deferred_vk_drop(DockApp& app);

[[nodiscard]] IDockRasterBackend* try_pick_raster_backend(DockApp& app, DockOutputLayer& layer, int buf_w, int buf_h);

[[nodiscard]] bool ensure_popup_vk_raster(DockApp& app, int buf_w, int buf_h);
[[nodiscard]] bool present_popup_vk_raster(DockApp& app, int buf_w, int buf_h, bool* transient_failure);

// Cairo (wl_shm) popup raster: draws straight into an shm buffer attached to
// app.popupSurface so popups never map the Vulkan ICD in cairo mode.
[[nodiscard]] bool ensure_popup_cairo_raster(DockApp& app, int buf_w, int buf_h);
[[nodiscard]] cairo_t* popup_cairo_ctx(DockApp& app);
[[nodiscard]] bool present_popup_cairo_raster(DockApp& app, int buf_w, int buf_h);

}
