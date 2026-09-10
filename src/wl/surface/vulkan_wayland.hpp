#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "wl/buffer/damage_region.hpp"

#ifndef NDEBUG
#include <atomic>
#endif

struct wl_display;
struct wl_surface;

namespace eh::wayland {

constexpr int kMaxFramesInFlight = 2;

struct VulkanDisplayContextImpl;

class VulkanDisplayContext {
 public:
  VulkanDisplayContext();
  ~VulkanDisplayContext();
  VulkanDisplayContext(const VulkanDisplayContext&) = delete;
  VulkanDisplayContext& operator=(const VulkanDisplayContext&) = delete;
  VulkanDisplayContext(VulkanDisplayContext&&) = delete;
  VulkanDisplayContext& operator=(VulkanDisplayContext&&) = delete;

  bool init(wl_display* display);
  void shutdown();

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] struct wl_display* wayland_display() const noexcept;

  VulkanDisplayContextImpl* impl() noexcept { return impl_.get(); }
  [[nodiscard]] const VulkanDisplayContextImpl* impl() const noexcept { return impl_.get(); }

 private:
  std::unique_ptr<VulkanDisplayContextImpl> impl_;
};

struct VulkanLayerSurfaceImpl;

class VulkanLayerSurface {
 public:
  VulkanLayerSurface();
  ~VulkanLayerSurface();
  VulkanLayerSurface(const VulkanLayerSurface&) = delete;
  VulkanLayerSurface& operator=(const VulkanLayerSurface&) = delete;

  bool create(VulkanDisplayContext& ctx, wl_display* display, wl_surface* surface, int width, int height);
  void resize(int width, int height);
  void destroy();
  void detach() noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] int debug_create_call_count() const noexcept { return create_call_count_; }

#ifndef NDEBUG
  struct FrameDrawStats {
    int present_count = 0;
    int64_t bytes_uploaded = 0;
  };
  static FrameDrawStats debug_snapshot_and_reset() noexcept;
#endif

  bool present_cpu_bgra(VulkanDisplayContext& ctx, const unsigned char* data, int width, int height, int stride,
                        bool* transient_recoverable = nullptr);

  // Damage-aware present: only the regions in `frame_damage` are uploaded
  // (unioned with each swapchain image's history so every image stays coherent
  // regardless of acquire order). `effective_damage_out` gets the region to
  // pass to wl_surface_damage_buffer; it may come back empty if nothing changed.
  bool present_cpu_bgra(VulkanDisplayContext& ctx, const unsigned char* data, int width, int height, int stride,
                        bool* transient_recoverable, const DamageRegion& frame_damage,
                        DamageRegion* effective_damage_out);

 private:
  bool create_swapchain(VulkanDisplayContext& ctx, int width, int height);
  bool ensure_staging(VulkanDisplayContext& ctx, size_t bytes, int frame_index);

  std::unique_ptr<VulkanLayerSurfaceImpl> impl_;
  mutable std::recursive_mutex mutex_;
  int create_call_count_ = 0;
};

}
