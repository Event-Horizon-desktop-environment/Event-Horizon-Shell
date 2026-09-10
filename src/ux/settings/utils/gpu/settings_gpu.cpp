#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <wayland-client.h>

#include "configuration/shell_config.hpp"
#include "configuration/shell_renderer_backend.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "ux/settings/utils/gpu/settings_gpu.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "wl/buffer/cairo_cpu_buffer.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/surface/vulkan_wayland.hpp"
#include "wl/core/connection.hpp"

namespace {
std::weak_ptr<eh::wayland::VulkanDisplayContext> g_settings_shell_vk_weak;
}

void settings_gpu_embed_shell_vk(std::shared_ptr<eh::wayland::VulkanDisplayContext> vk) {
  debug_log("vulkan", "settings_gpu_embed_shell_vk: vk=%p", (void*)vk.get());
  g_settings_shell_vk_weak = std::move(vk);
}

void settings_gpu_reset_shell_vk() {
  debug_log("vulkan", "settings_gpu_reset_shell_vk");
  g_settings_shell_vk_weak.reset();
}

static bool settings_renderer_env_is_cairo() {
  const char* e = std::getenv("EH_SHELL_RENDERER");
  if (!e || !*e) return false;
  std::string low(e);
  for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return low == "cairo";
}

void settings_clear_all_gpu_surfaces(App& app) {
  debug_log("vulkan", "settings_clear_all_gpu_surfaces: app=%p", (void*)&app);
  app.vkLayer.reset();
  app.glRaster.destroy();
  app.vkDisplay.reset();
}

static void settings_abort_vk(App& app) {
  debug_log("vulkan", "settings_abort_vk: app=%p", (void*)&app);
  app.settingsVkFailed = true;
  settings_clear_all_gpu_surfaces(app);
}

void settings_sync_renderer_backend_state(App& app, const eh::config::ShellConfig& sc) {
  const bool want_vk = settings_want_vk(app, sc);
  debug_log("vulkan", "settings_sync_renderer_backend_state: want_vk=%d settingsVkFailed=%d renderer=%d",
    want_vk, app.settingsVkFailed, static_cast<int>(sc.renderer));
  if (!want_vk) {
    app.vkLayer.reset();
    app.vkDisplay.reset();
    app.glRaster.destroy();
    if (app.surface) app.surfaceExt.ensure_tearing_control(app.wl, app.surface);
    // Only reset the VK failure flag when the user explicitly chose Cairo.
    // If VK failed before, keep the flag so we fall back to SHM/Cairo permanently
    // instead of retrying VK on every draw and never committing a buffer.
    if (sc.renderer != eh::config::ShellRendererBackend::Vulkan) {
      app.settingsVkFailed = false;
    }
  }
}

bool settings_want_vk(const App& app, const eh::config::ShellConfig& sc) {
#if defined(EH_NO_VULKAN_LOADER) && EH_NO_VULKAN_LOADER
  (void)app;
  (void)sc;
  return false;
#else
  if (settings_renderer_env_is_cairo()) return false;
  if (app.settingsVkFailed) return false;
  return sc.renderer == eh::config::ShellRendererBackend::Vulkan;
#endif
}

bool settings_ensure_vk_raster(App& app, int buf_w, int buf_h) {
  debug_log("vulkan", "settings_ensure_vk_raster: app=%p %dx%d vkFailed=%d", (void*)&app, buf_w, buf_h, app.settingsVkFailed);
#if defined(EH_NO_VULKAN_LOADER) && EH_NO_VULKAN_LOADER
  (void)app;
  (void)buf_w;
  (void)buf_h;
  return false;
#else
  if (app.vkRasterReentrantGuard) {
    debug_log("vulkan", "settings_ensure_vk_raster: reentrant guard active, returning false");
    return false;
  }
  app.vkRasterReentrantGuard = true;
  const auto resetGuard = [&] { app.vkRasterReentrantGuard = false; };

  wl_display* dpy = app.wl.display();
  if (!dpy || !app.surface) {
    debug_log("vulkan", "settings_ensure_vk_raster: no display or surface, returning false");
    resetGuard(); return false;
  }
  if (!app.vkDisplay) {
    if (auto sh = g_settings_shell_vk_weak.lock()) {
      debug_log("vulkan", "settings_ensure_vk_raster: reusing shell VK context");
      app.vkDisplay = std::move(sh);
    }
    if (!app.vkDisplay) {
      debug_log("vulkan", "settings_ensure_vk_raster: creating new VulkanDisplayContext");
      app.vkDisplay = std::make_shared<eh::wayland::VulkanDisplayContext>();
    }
  }
  if (!app.vkDisplay->valid()) {
    debug_log("vulkan", "settings_ensure_vk_raster: vkDisplay not valid, calling init");
    if (!app.vkDisplay->init(dpy)) {
      debug_log("vulkan", "settings_ensure_vk_raster: Vulkan display init failed");
      settings_abort_vk(app);
      std::cerr << "[settings-vk] Vulkan display init failed; using SHM+Cairo\n";
      resetGuard(); return false;
    }
  }
  if (!app.vkLayer || !app.vkLayer->valid()) {
    debug_log("vulkan", "settings_ensure_vk_raster: creating/setting up VkLayer");
    if (!app.vkLayer) app.vkLayer = std::make_unique<eh::wayland::VulkanLayerSurface>();
    app.surfaceExt.destroy_tearing_control();
    if (dpy) wl_display_roundtrip(dpy);
    if (!app.vkLayer->create(*app.vkDisplay, dpy, app.surface, buf_w, buf_h)) {
      debug_log("vulkan", "settings_ensure_vk_raster: Vulkan surface/swapchain failed");
      app.surfaceExt.ensure_tearing_control(app.wl, app.surface);
      settings_abort_vk(app);
      std::cerr << "[settings-vk] Vulkan surface/swapchain failed; using SHM+Cairo\n";
      resetGuard(); return false;
    }
    if (app.wl.compositor()) wl_surface_set_opaque_region(app.surface, nullptr);
  } else {
    debug_log("vulkan", "settings_ensure_vk_raster: resizing existing VkLayer");
    app.vkLayer->resize(buf_w, buf_h);
  }
  if (!app.glRaster.ensure(buf_w, buf_h)) {
    debug_log("vulkan", "settings_ensure_vk_raster: CairoCpuBuffer::ensure failed");
    std::cerr << "[settings-vk] CairoCpuBuffer::ensure failed; using SHM+Cairo\n";
    settings_abort_vk(app);
    resetGuard(); return false;
  }
  debug_log("vulkan", "settings_ensure_vk_raster: OK");
  resetGuard(); return true;
#endif
}

bool settings_present_vk_raster(App& app, int buf_w, int buf_h, bool* transient_failure) {
  debug_log("vulkan", "settings_present_vk_raster: app=%p %dx%d", (void*)&app, buf_w, buf_h);
#if defined(EH_NO_VULKAN_LOADER) && EH_NO_VULKAN_LOADER
  (void)app;
  (void)buf_w;
  (void)buf_h;
  if (transient_failure) *transient_failure = false;
  return false;
#else
  bool transient = false;
  if (!app.vkDisplay || !app.vkLayer ||
      !app.vkLayer->present_cpu_bgra(*app.vkDisplay, app.glRaster.data(), buf_w, buf_h, app.glRaster.stride(), &transient)) {
    if (transient_failure) *transient_failure = transient;
    if (transient) {
      debug_log("vulkan", "settings_present_vk_raster: transient failure (OUT_OF_DATE), clearing surfaces");
      settings_clear_all_gpu_surfaces(app);
      return false;
    }
    debug_log("vulkan", "settings_present_vk_raster: non-transient failure, aborting VK");
    settings_abort_vk(app);
    return false;
  }
  if (transient_failure) *transient_failure = false;
  debug_log("vulkan", "settings_present_vk_raster: OK");
  return true;
#endif
}
