#include "desktop_shell/dock/paint/dock_raster_backend.hpp"
#include "wl/core/gpu_page_trim.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "wl/surface/vulkan_destruction_queue.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/log/verbose_log.hpp"
#include "configuration/shell_renderer_backend.hpp"
#include "wl/buffer/cairo_cpu_buffer.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/vulkan_wayland.hpp"

#include <cairo/cairo.h>
#include <wayland-client.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <vector>

namespace eh::dock {

void clear_layer_rasters(DockOutputLayer& L) {
   
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_layer_rasters enter: layer.surface=" << static_cast<void*>(L.surface)
            << " layer.wlOut=" << static_cast<void*>(L.wlOut));

  if (L.glRaster.cairo_surface()) {
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_layer_rasters: destroying glRaster for surface=" << static_cast<void*>(L.surface));
  } else {
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_layer_rasters: glRaster already null for surface=" << static_cast<void*>(L.surface));
  }
  L.glRaster.destroy();
  L.shmRaster.destroy();
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_layer_rasters exit: surface=" << static_cast<void*>(L.surface));
}

void clear_popup_gl_raster(DockApp& app) {
   
EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_popup_gl_raster enter: popupSurface=" << static_cast<void*>(app.popupSurface)
            << " popupVkLayer=" << static_cast<void*>(app.popupVkLayer.get()));
  if (app.popupVkLayer) {
            EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_popup_gl_raster: resetting popupVkLayer");
    app.popupVkLayer.reset();
  } else {
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_popup_gl_raster: popupVkLayer already null");
  }
  if (app.popupGlRaster.cairo_surface()) {
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_popup_gl_raster: destroying popupGlRaster");
  } else {
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_popup_gl_raster: popupGlRaster already null");
  }
  app.popupGlRaster.destroy();
  if (app.popupShmRaster.wl()) app.popupShmRaster.destroy();
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_popup_gl_raster exit");
}

bool use_vulkan_backend(const DockApp& app) {
   
  bool result = app.dockRendererBackend == eh::config::ShellRendererBackend::Vulkan && !app.dockVkFailed;
EH_VERBOSE_LOG(std::cerr << "[dock-raster] use_vulkan_backend: rendererBackend="
            << (app.dockRendererBackend == eh::config::ShellRendererBackend::Vulkan ? "Vulkan" : "other")
            << " dockVkFailed=" << (app.dockVkFailed ? "true" : "false")
            << " result=" << (result ? "true" : "false"));
  return result;
}

namespace detail {
void clear_vk_gpu_only(DockApp& app) {
   
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only enter: dockLayers.size=" << app.dockLayers.size()
            << " popupVkLayer=" << static_cast<void*>(app.popupVkLayer.get())
            << " dockVk=" << static_cast<void*>(app.dockVk.get())
            << " deferredVkLayers.size=" << app.deferredVkLayers.size());

  for (auto& up : app.dockLayers) {
    if (!up) {
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only: skipping null dockLayer");
      continue;
    }
    if (up->vkLayer) {
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only: moving vkLayer to deferredVkLayers for surface=" << static_cast<void*>(up->surface));
      app.deferredVkLayers.push_back(std::move(up->vkLayer));
    }
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only: destroying glRaster for surface=" << static_cast<void*>(up->surface)
              << " wlOut=" << static_cast<void*>(up->wlOut));
    up->glRaster.destroy();
  }
  if (app.popupVkLayer) {
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only: moving popupVkLayer to deferredVkLayers");
    app.deferredVkLayers.push_back(std::move(app.popupVkLayer));
  } else {
      EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only: popupVkLayer was already null");
  }
  if (app.popupGlRaster.cairo_surface()) {
              EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only: destroying popupGlRaster");
  } else {
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only: popupGlRaster already null");
  }
  app.popupGlRaster.destroy();
  if (app.dockVk) {
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only: deferring dockVk context");
    app.deferredVkDrop = std::move(app.dockVk);
    app.dockVk = nullptr;
  } else {
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only: dockVk was already null");
  }
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] clear_vk_gpu_only exit: deferredVkLayers.size=" << app.deferredVkLayers.size()
            << " deferredVkDrop=" << static_cast<void*>(app.deferredVkDrop.get()));
}
}

void abort_vk_backend(DockApp& app) {
   
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] abort_vk_backend enter");

  detail::clear_vk_gpu_only(app);
  app.dockVkFailed = true;
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] abort_vk_backend exit (dockVkFailed set)");
}

void drain_deferred_vk_drop(DockApp& app) {
   
EH_VERBOSE_LOG(std::cerr << "[dock-raster] drain_deferred_vk_drop enter: deferredVkLayers.size=" << app.deferredVkLayers.size()
            << " deferredVkDrop=" << static_cast<void*>(app.deferredVkDrop.get()));

  auto layers = std::move(app.deferredVkLayers);
  auto drop = std::move(app.deferredVkDrop);
  app.deferredVkLayers = decltype(app.deferredVkLayers)();
  app.deferredVkDrop = nullptr;

  if (layers.empty() && !drop) {
EH_VERBOSE_LOG(std::cerr << "[dock-raster] drain_deferred_vk_drop: nothing to drain, returning");
    return;
  }

  EH_VERBOSE_LOG(std::cerr << "[dock-raster] drain_deferred_vk_drop: posting " << layers.size() << " layer(s)"
            << " and drop=" << static_cast<void*>(drop.get()) << " to destruction queue");

  auto& q = eh::vk::VulkanDestructionQueue::instance();

  struct DrainState {
    std::vector<std::unique_ptr<eh::wayland::VulkanLayerSurface>> layers;
    decltype(app.deferredVkDrop) drop;
  };
  auto state = std::make_shared<DrainState>();
  state->layers = std::move(layers);
  state->drop = std::move(drop);
  q.post([state]() {
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] drain_deferred_vk_drop lambda: destroying " << state->layers.size() << " layer(s)"
              << " and drop=" << static_cast<void*>(state->drop.get()));

    state->layers.clear();
    state->drop.reset();
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] drain_deferred_vk_drop lambda: done");
  });
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] drain_deferred_vk_drop exit");
}

namespace {

// Frame-activity tracking for the idle GPU-page trimmer: only frames that
// repaint a meaningful share of the buffer count as activity, so sparse
// clock/progress ticks don't pin NVIDIA userspace pages forever.
void note_present_activity(int buf_w, int buf_h, const eh::wayland::DamageRegion& d) {
  const uint64_t total = static_cast<uint64_t>(buf_w) * static_cast<uint64_t>(buf_h);
  if (total == 0) return;
  uint64_t dmg = 0;
  if (d.full()) {
    dmg = total;
  } else {
    for (const auto& r : d.spans()) {
      dmg += static_cast<uint64_t>(r.w) * static_cast<uint64_t>(r.h);
    }
  }
  if (d.empty()) dmg = total;  // nothing damaged yet but we still presented
  eh::gpu::touch_frame_activity_if_significant(static_cast<uint32_t>(total),
                                               static_cast<uint32_t>(std::min<uint64_t>(dmg, UINT32_MAX)));
}

struct VkRasterBackend final : IDockRasterBackend {
  bool ensure(DockApp& app, DockOutputLayer& layer, int buf_w, int buf_h) override {
     
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure enter: surface=" << static_cast<void*>(layer.surface)
              << " display=" << static_cast<void*>(app.display)
              << " buf=" << buf_w << "x" << buf_h
              << " wlOut=" << static_cast<void*>(layer.wlOut)
              << " configured=" << (layer.configured ? "true" : "false")
              << " configuredW=" << layer.configuredWidth
              << " configuredH=" << layer.configuredHeight);
    if (!app.display) {
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure FAIL: app.display is null");
      return false;
    }
    if (!layer.surface) {
  EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure FAIL: layer.surface is null");
      return false;
    }
    if (!app.dockVk) {
EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: creating new VulkanDisplayContext");
      app.dockVk = std::make_shared<eh::wayland::VulkanDisplayContext>();
    } else {
  EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: dockVk already exists, valid=" << (app.dockVk->valid() ? "true" : "false"));
    }
    if (!app.dockVk->valid()) {
              EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: dockVk not valid, calling init(display=" << static_cast<void*>(app.display) << ")");
      if (!app.dockVk->init(app.display)) {
              EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure FAIL: VulkanDisplayContext::init returned false");
        abort_vk_backend(app);
              EH_VERBOSE_LOG(std::cerr << "[dock-vk] Vulkan display init failed");
        return false;
      }
      EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: dockVk init succeeded");
    } else {
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: dockVk already valid, skipping init");
    }
  {

      EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: before valid check: vkLayer=" << static_cast<void*>(layer.vkLayer.get())
              << " valid=" << (layer.vkLayer ? (layer.vkLayer->valid() ? "true" : "false") : "N/A (null)")
              << " create_call_count=" << (layer.vkLayer ? layer.vkLayer->debug_create_call_count() : -1));
  }
  if (!layer.vkLayer || !layer.vkLayer->valid()) {
      EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: vkLayer not valid (ptr=" << static_cast<void*>(layer.vkLayer.get())
              << "), need to create");
    if (!layer.vkLayer) {
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: allocating new VulkanLayerSurface");
      layer.vkLayer = std::make_unique<eh::wayland::VulkanLayerSurface>();
    }
        EH_VERBOSE_LOG(std::cerr << "[dock-vk] calling create: surface=" << static_cast<void*>(layer.surface)
              << " display=" << static_cast<void*>(app.display) << " " << buf_w << "x" << buf_h);
    if (!layer.vkLayer->create(*app.dockVk, app.display, layer.surface, buf_w, buf_h)) {
        EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure FAIL: vkLayer->create returned false");
      EH_VERBOSE_LOG(std::cerr << "[dock-vk] Vulkan surface/swapchain failed");
      abort_vk_backend(app);
      return false;
      }
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: vkLayer->create succeeded");
      if (app.compositor && layer.surface) {
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: setting opaque region to null for surface=" << static_cast<void*>(layer.surface));
        wl_surface_set_opaque_region(layer.surface, nullptr);
      } else {
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: skipping opaque region (compositor=" << static_cast<void*>(app.compositor)
                  << " surface=" << static_cast<void*>(layer.surface) << ")");
      }
    } else {
              EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: vkLayer already valid, calling resize(" << buf_w << "x" << buf_h << ")");
      layer.vkLayer->resize(buf_w, buf_h);
  EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: resize complete");
    }
              EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: calling layer.glRaster.ensure(" << buf_w << "x" << buf_h << ")");
    if (!layer.glRaster.ensure(buf_w, buf_h)) {
      EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure FAIL: glRaster.ensure returned false");
      EH_VERBOSE_LOG(std::cerr << "[dock-vk] CairoCpuBuffer::ensure failed");
      abort_vk_backend(app);
      return false;
    }
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure: glRaster.ensure succeeded, data=" << static_cast<void*>(layer.glRaster.data())
              << " stride=" << layer.glRaster.stride());
    {
      static std::atomic<bool> s_logged_active{};
      if (!s_logged_active.exchange(true)) {
      EH_VERBOSE_LOG(std::cerr << "[dock-vk] dock strip using Vulkan WSI (BGRA upload → transfer → vkQueuePresentKHR). "
                     "EH_DOCK_VK_DEBUG=1 logs GPU name at device init.");
      }
    }
        EH_VERBOSE_LOG(std::cerr << "[dock-vk] ensure returning true");
    return true;
  }
  cairo_t* cairo_ctx(DockOutputLayer& layer) const override {
    cairo_t* ctx = layer.glRaster.cairo();
      EH_VERBOSE_LOG(std::cerr << "[dock-raster] cairo_ctx: surface=" << static_cast<void*>(layer.surface) << " ctx=" << static_cast<void*>(ctx));
    return ctx;
  }
  cairo_surface_t* cairo_surface(DockOutputLayer& layer) const override {
    cairo_surface_t* s = layer.glRaster.cairo_surface();
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] cairo_surface: surface=" << static_cast<void*>(layer.surface) << " cairo_surface=" << static_cast<void*>(s));
    return s;
  }
  bool present(DockApp& app, DockOutputLayer& layer, int buf_w, int buf_h,
               const eh::wayland::DamageRegion& frame_damage) override {

    bool transient = false;
      EH_VERBOSE_LOG(std::cerr << "[dock-vk] present enter: surface=" << static_cast<void*>(layer.surface)
              << " buf=" << buf_w << "x" << buf_h
               << " vkLayer=" << static_cast<void*>(layer.vkLayer.get())
               << " data=" << static_cast<void*>(layer.glRaster.data())
               << " stride=" << layer.glRaster.stride()
               << " damage_full=" << (frame_damage.full() ? "true" : "false")
               << " damage_spans=" << frame_damage.span_count());
    if (!layer.vkLayer) {
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] present FAIL: layer.vkLayer is null");
      abort_vk_backend(app);
      return false;
    }
    if (!app.dockVk) {
      EH_VERBOSE_LOG(std::cerr << "[dock-vk] present FAIL: app.dockVk is null");
      abort_vk_backend(app);
      return false;
    }
      EH_VERBOSE_LOG(std::cerr << "[dock-vk] present: calling present_cpu_bgra");
    eh::wayland::DamageRegion effective;
    bool ok = layer.vkLayer->present_cpu_bgra(*app.dockVk, layer.glRaster.data(), buf_w, buf_h, layer.glRaster.stride(),
                                              &transient, frame_damage, &effective);
        EH_VERBOSE_LOG(std::cerr << "[dock-vk] present: present_cpu_bgra returned ok=" << (ok ? "true" : "false")
              << " transient=" << (transient ? "true" : "false"));
    if (!ok) {
      if (transient) {
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] present: transient failure (OUT_OF_DATE), returning false (surface NOT torn down)");

        return false;
      }
  EH_VERBOSE_LOG(std::cerr << "[dock-vk] present: non-transient failure, aborting Vulkan backend");
      abort_vk_backend(app);
      return false;
    }
    if (layer.surface) {
  EH_VERBOSE_LOG(std::cerr << "[dock-vk] present: committing wl_surface " << static_cast<void*>(layer.surface));

      if (effective.full()) {
        wl_surface_damage_buffer(layer.surface, 0, 0, INT32_MAX, INT32_MAX);
      } else {
        for (const auto& sp : effective.spans()) {
          wl_surface_damage_buffer(layer.surface, sp.x, sp.y, sp.w, sp.h);
        }
      }
      wl_surface_commit(layer.surface);
      note_present_activity(buf_w, buf_h, frame_damage);
    } else {
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] present: layer.surface is null, skipping wl_surface_commit");
    }
              EH_VERBOSE_LOG(std::cerr << "[dock-vk] present returning true");
    return true;
  }
  bool buffer_busy(const DockOutputLayer&  ) const override {
      EH_VERBOSE_LOG(std::cerr << "[dock-raster] buffer_busy called (always returns false)");
    return false;
  }
  bool uses_gpu_present() const noexcept override {
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] uses_gpu_present called (returns true)");
    return true;
  };
};

}

VkRasterBackend g_vk_raster;

namespace {

struct ShmRasterBackend final : IDockRasterBackend {
  bool ensure(DockApp& app, DockOutputLayer& layer, int buf_w, int buf_h) override {
    if (!app.shm) return false;
    return layer.shmRaster.ensure(app.shm, "eh-dock-shm", buf_w, buf_h);
  }
  cairo_t* cairo_ctx(DockOutputLayer& layer) const override {
    return layer.shmRaster.cairo();
  }
  cairo_surface_t* cairo_surface(DockOutputLayer& layer) const override {
    return layer.shmRaster.cairo_surface();
  }
  bool present(DockApp&, DockOutputLayer& layer, int buf_w, int buf_h,
               const eh::wayland::DamageRegion&) override {
    if (!layer.surface || !layer.shmRaster.wl()) return false;
    if (layer.shmRaster.busy()) return false;
    cairo_surface_flush(layer.shmRaster.cairo_surface());
    wl_surface_attach(layer.surface, layer.shmRaster.wl(), 0, 0);
    wl_surface_damage_buffer(layer.surface, 0, 0, buf_w, buf_h);
    layer.shmRaster.mark_busy();
    wl_surface_commit(layer.surface);
    {
      eh::wayland::DamageRegion full;
      full.mark_full();
      note_present_activity(buf_w, buf_h, full);
    }
    return true;
  }
  bool buffer_busy(const DockOutputLayer& layer) const override {
    return layer.shmRaster.busy();
  }
  bool uses_gpu_present() const noexcept override { return false; }
};

ShmRasterBackend g_shm_raster;

}

IDockRasterBackend* try_pick_raster_backend(DockApp& app, DockOutputLayer& layer, int buf_w, int buf_h) {

        EH_VERBOSE_LOG(std::cerr << "[dock-raster] try_pick_raster_backend enter: rendererBackend="
            << (app.dockRendererBackend == eh::config::ShellRendererBackend::Vulkan ? "Vulkan" : "other")
            << " buf=" << buf_w << "x" << buf_h
            << " surface=" << static_cast<void*>(layer.surface)
            << " wlOut=" << static_cast<void*>(layer.wlOut));

  // Honor `[shell] renderer = "cairo"` / EH_SHELL_RENDERER=cairo: draw through
  // wl_shm and never touch Vulkan. This keeps the NVIDIA userspace ICD
  // (gpucomp/glcore/glvkspirv ≈ 90MB RSS shared-clean) unmapped entirely.
  if (app.dockRendererBackend != eh::config::ShellRendererBackend::Vulkan) {
    if (g_shm_raster.ensure(app, layer, buf_w, buf_h)) return &g_shm_raster;
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] cairo backend requested but wl_shm unavailable");
    return nullptr;
  }

  if (!use_vulkan_backend(app)) {
    static std::atomic<bool> s_vulkan_unavail{};
    if (!s_vulkan_unavail.exchange(true)) {
    EH_VERBOSE_LOG(std::cerr << "[dock-vk] `[shell] renderer=\"vulkan\"` ignored: Vulkan unavailable "
                   "(check EH_NO_VULKAN_LOADER or Vulkan installation).");
    }
  EH_VERBOSE_LOG(std::cerr << "[dock-raster] try_pick_raster_backend returning nullptr (vulkan unavailable)");
    return nullptr;
  }

  EH_VERBOSE_LOG(std::cerr << "[dock-raster] try_pick_raster_backend: calling g_vk_raster.ensure");
  if (g_vk_raster.ensure(app, layer, buf_w, buf_h)) {
    EH_VERBOSE_LOG(std::cerr << "[dock-raster] try_pick_raster_backend returning &g_vk_raster");
    return &g_vk_raster;
  }

EH_VERBOSE_LOG(std::cerr << "[dock-raster] try_pick_raster_backend: ensure failed, returning nullptr");
  return nullptr;
}

bool ensure_popup_vk_raster(DockApp& app, int buf_w, int buf_h) {
   
EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster enter: popupSurface=" << static_cast<void*>(app.popupSurface)
            << " display=" << static_cast<void*>(app.display)
            << " buf=" << buf_w << "x" << buf_h);
  if (!app.display) {
            EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster FAIL: app.display is null");
    return false;
  }
  if (!app.popupSurface) {
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster FAIL: app.popupSurface is null");
    return false;
  }
  if (!app.dockVk) {
      EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: creating new VulkanDisplayContext");
    app.dockVk = std::make_shared<eh::wayland::VulkanDisplayContext>();
  } else {
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: dockVk exists, valid=" << (app.dockVk->valid() ? "true" : "false"));
  }
  if (!app.dockVk->valid()) {
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: dockVk not valid, calling init");
    if (!app.dockVk->init(app.display)) {
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster FAIL: VulkanDisplayContext::init returned false");
      abort_vk_backend(app);
      EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] Vulkan display init failed");
      return false;
    }
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: dockVk init succeeded");
  } else {
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: dockVk already valid");
  }
  if (!app.popupVkLayer || !app.popupVkLayer->valid()) {
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: popupVkLayer not valid (ptr=" << static_cast<void*>(app.popupVkLayer.get()) << ")");
    if (!app.popupVkLayer) {
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: allocating new VulkanLayerSurface");
      app.popupVkLayer = std::make_unique<eh::wayland::VulkanLayerSurface>();
    }
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] calling create: surface=" << static_cast<void*>(app.popupSurface)
              << " display=" << static_cast<void*>(app.display) << " " << buf_w << "x" << buf_h);
    if (!app.popupVkLayer->create(*app.dockVk, app.display, app.popupSurface, buf_w, buf_h)) {
EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster FAIL: popupVkLayer->create returned false");
      abort_vk_backend(app);
EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] Vulkan surface/swapchain failed");
      return false;
    }
            EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: popupVkLayer->create succeeded");
    if (app.compositor) {
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: setting opaque region to null for popupSurface=" << static_cast<void*>(app.popupSurface));
      wl_surface_set_opaque_region(app.popupSurface, nullptr);
    } else {
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: compositor is null, skipping opaque region");
    }
  } else {
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: popupVkLayer already valid, calling resize(" << buf_w << "x" << buf_h << ")");
    app.popupVkLayer->resize(buf_w, buf_h);
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: resize complete");
  }
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: calling popupGlRaster.ensure(" << buf_w << "x" << buf_h << ")");
  if (!app.popupGlRaster.ensure(buf_w, buf_h)) {
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster FAIL: popupGlRaster.ensure returned false");
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] CairoCpuBuffer::ensure failed");
    abort_vk_backend(app);
    return false;
  }
      EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster: popupGlRaster.ensure succeeded, data="
            << static_cast<void*>(app.popupGlRaster.data()) << " stride=" << app.popupGlRaster.stride());
      EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] ensure_popup_vk_raster returning true");
  return true;
}

bool present_popup_vk_raster(DockApp& app, int buf_w, int buf_h, bool* transient_failure) {
   
  bool transient = false;
  EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] present_popup_vk_raster enter: buf=" << buf_w << "x" << buf_h
            << " popupVkLayer=" << static_cast<void*>(app.popupVkLayer.get())
            << " data=" << static_cast<void*>(app.popupGlRaster.data())
            << " stride=" << app.popupGlRaster.stride());
  if (!app.popupVkLayer) {
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] present_popup_vk_raster FAIL: popupVkLayer is null");
    if (transient_failure) *transient_failure = false;
    abort_vk_backend(app);
    return false;
  }
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] present_popup_vk_raster: calling present_cpu_bgra");
  if (!app.dockVk) {
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] present_popup_vk_raster FAIL: app.dockVk is null");
    if (transient_failure) *transient_failure = false;
    abort_vk_backend(app);
    return false;
  }
  bool ok = app.popupVkLayer->present_cpu_bgra(*app.dockVk, app.popupGlRaster.data(), buf_w, buf_h, app.popupGlRaster.stride(),
                                          &transient);
      EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] present_popup_vk_raster: present_cpu_bgra returned ok=" << (ok ? "true" : "false")
            << " transient=" << (transient ? "true" : "false"));
  if (!ok) {
    if (transient_failure) *transient_failure = transient;
    if (transient) {
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] present_popup_vk_raster: transient failure, calling clear_vk_gpu_only");
      detail::clear_vk_gpu_only(app);
      return false;
    }
      EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] present_popup_vk_raster: non-transient failure, calling abort_vk_backend");
    abort_vk_backend(app);
    return false;
  }
  if (transient_failure) *transient_failure = false;
    EH_VERBOSE_LOG(std::cerr << "[dock-popup-vk] present_popup_vk_raster returning true");
  return true;
}

bool ensure_popup_cairo_raster(DockApp& app, int buf_w, int buf_h) {
  if (!app.shm || !app.popupSurface) return false;
  // Drop any Vulkan-era raster if the renderer switched at runtime.
  if (app.popupVkLayer || app.popupGlRaster.cairo_surface()) clear_popup_gl_raster(app);
  if (app.popupShmRaster.wl() && !app.popupShmRaster.busy() &&
      app.popupShmRaster.width() == buf_w && app.popupShmRaster.height() == buf_h) {
    return true;
  }
  if (!app.popupShmRaster.wl()) {
    if (!app.popupShmRaster.ensure(app.shm, "eh-dock-popup-shm", buf_w, buf_h)) return false;
  }
  return true;
}

cairo_t* popup_cairo_ctx(DockApp& app) { return app.popupShmRaster.cairo(); }

bool present_popup_cairo_raster(DockApp& app, int buf_w, int buf_h) {
  if (!app.popupSurface || !app.popupShmRaster.wl()) return false;
  if (app.popupShmRaster.busy()) return false;
  cairo_surface_flush(app.popupShmRaster.cairo_surface());
  wl_surface_attach(app.popupSurface, app.popupShmRaster.wl(), 0, 0);
  wl_surface_damage_buffer(app.popupSurface, 0, 0, buf_w, buf_h);
  app.popupShmRaster.mark_busy();
  return true;
}

}
