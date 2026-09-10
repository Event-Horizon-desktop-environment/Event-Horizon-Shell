#include "ux/Powermenu/power_menu.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/time/text_caret.hpp"
#include "desktop_shell/dock/core/dock_settings.hpp"
#include "desktop_shell/power_confirm/power_confirm.hpp"
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_exec.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "wl/surface/layer_surface.hpp"

#include <wayland-client.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace eh::ui::powermenu {

namespace {

int debug() {
   
  static int v = [] {
    const char* e = std::getenv("EH_APP_DRAWER_DEBUG");
    return e ? std::atoi(e) : 0;
  }();
  return v;
}

void pm_configure(void* data, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h) {
   
  auto& pm = *static_cast<PowerMenu*>(data);
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  pm.configured = true;
  pm.configuredW = static_cast<int>(w);
  pm.configuredH = static_cast<int>(h);
  if (debug() >= 1) std::fprintf(stderr, "[powermenu] configure serial=%u w=%u h=%u\n", serial, w, h);
}

void pm_closed(void* data, zwlr_layer_surface_v1*) {
   
  auto& pm = *static_cast<PowerMenu*>(data);
  pm.open = false;
  if (debug() >= 1) std::fprintf(stderr, "[powermenu] closed\n");
}

static const zwlr_layer_surface_v1_listener kPmListener = {
    .configure = pm_configure,
    .closed = pm_closed,
};

void pm_frame_done(void* data, wl_callback* cb, uint32_t) {
   
  auto& pm = *static_cast<PowerMenu*>(data);
  wl_callback_destroy(cb);
  pm.frameCallback = nullptr;
}

static constexpr wl_callback_listener kPmFrameListener = {
    .done = pm_frame_done,
};

void pm_buf_release(void* data) {
   
  auto& pm = *static_cast<PowerMenu*>(data);
  if (pm.wantRedraw) {
    pm.wantRedraw = false;
    draw(pm, pm.pointerX, pm.pointerY);
  }
}

} // namespace

bool open(PowerMenu& pm, wl_display* display, wl_compositor* compositor, wl_shm* shm,
          zwlr_layer_shell_v1* layerShell, wl_output* output, int outputW, int outputH, int outputScale, int actionIdx) {
   
  if (pm.open) return true;
  if (!compositor || !layerShell || actionIdx < 1 || actionIdx > 3) {
    if (debug() >= 1) std::fprintf(stderr, "[powermenu] open: invalid args compositor=%p layerShell=%p idx=%d\n",
                                   static_cast<void*>(compositor), static_cast<void*>(layerShell), actionIdx);
    return false;
  }

  pm.display = display;
  pm.compositor = compositor;
  pm.shm = shm;
  pm.actionIdx = actionIdx;
  pm.startMs = eh::shell::monotonic_ms();
  pm.configured = false;
  pm.configuredW = 0;
  pm.configuredH = 0;
  pm.outputW = outputW;
  pm.outputH = outputH;
  pm.outputScale = outputScale > 0 ? outputScale : 1;
  pm.frameCallback = nullptr;
  pm.wantRedraw = false;

  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace = eh::shell::kDesktopWidgetsNamespace;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
               ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  cfg.width = 0;
  cfg.height = 0;
  cfg.exclusiveZone = -1;
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;

  if (!eh::wayland::create_layer_surface(compositor, layerShell, output, cfg,
                                          &kPmListener, &pm,
                                          &pm.surface, &pm.layerSurface)) {
    if (debug() >= 1) std::fprintf(stderr, "[powermenu] failed to create layer surface\n");
    return false;
  }

  wl_surface_set_buffer_scale(pm.surface, 1);
  pm.buf.set_release_hook(pm_buf_release, &pm);

  wl_surface_commit(pm.surface);
  wl_display_roundtrip(display);
  pm.open = true;
  if (debug() >= 1) std::fprintf(stderr, "[powermenu] opened actionIdx=%d surface=%p\n", actionIdx, static_cast<void*>(pm.surface));
  return true;
}

void close(PowerMenu& pm) {
   
  if (debug() >= 1) std::fprintf(stderr, "[powermenu] close\n");
  pm.open = false;
  if (pm.frameCallback) {
    wl_callback_destroy(pm.frameCallback);
    pm.frameCallback = nullptr;
  }
  pm.actionIdx = -1;
  pm.startMs = 0;
  pm.configured = false;
  pm.configuredW = 0;
  pm.configuredH = 0;
  pm.outputW = 0;
  pm.outputH = 0;
  pm.outputScale = 1;
  pm.wantRedraw = false;
  pm.buf.set_release_hook(nullptr, nullptr);
  pm.buf.destroy();
  if (pm.layerSurface) {
    zwlr_layer_surface_v1_destroy(pm.layerSurface);
    pm.layerSurface = nullptr;
  }
  if (pm.surface) {
    wl_surface_destroy(pm.surface);
    pm.surface = nullptr;
  }
}

void draw(PowerMenu& pm, double pointerX, double pointerY) {
   
  if (!pm.open || !pm.configured || !pm.surface || !pm.compositor) {
    if (debug() >= 2) std::fprintf(stderr, "[powermenu] draw skip open=%d configured=%d surface=%p comp=%p\n",
                                   pm.open, pm.configured, static_cast<void*>(pm.surface), static_cast<void*>(pm.compositor));
    return;
  }

  // Use logical output dimensions (physical / scale), falling back to configured
  const int bufW = pm.configuredW > 0 ? pm.configuredW : 1;
  const int bufH = pm.configuredH > 0 ? pm.configuredH : 1;
  const double w = (pm.outputW > 0) ? (static_cast<double>(pm.outputW) / pm.outputScale)
                                    : static_cast<double>(bufW);
  const double h = (pm.outputH > 0) ? (static_cast<double>(pm.outputH) / pm.outputScale)
                                    : static_cast<double>(bufH);

  if (!pm.buf.ensure(pm.shm, "event-horizon-powermenu", bufW, bufH)) {
    if (debug() >= 1) std::fprintf(stderr, "[powermenu] draw: buf.ensure(%dx%d) failed\n", bufW, bufH);
    return;
  }
  if (pm.buf.busy()) {
    pm.wantRedraw = true;
    if (debug() >= 2) std::fprintf(stderr, "[powermenu] draw: buf busy\n");
    return;
  }

  cairo_t* cr = pm.buf.cairo();
  if (!cr) {
    if (debug() >= 1) std::fprintf(stderr, "[powermenu] draw: null cairo\n");
    return;
  }

  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  // Dim the background
  const eh::config::ShellConfig sc = eh::config::shell_config_snapshot();
  if (pm.actionIdx >= 1 && pm.actionIdx <= 3) {
    const auto chrome = eh::config::derived_chrome_colors(sc.appearance);
    cairo_set_source_rgba(cr, chrome.drawerDimR, chrome.drawerDimG, chrome.drawerDimB, 0.55);
    cairo_paint(cr);
  }

  const uint64_t now = eh::shell::monotonic_ms();
  const uint64_t elapsed = (pm.startMs != 0) ? (now - pm.startMs) : 0;
  const int remainingSec = (elapsed >= 60000) ? 0 : static_cast<int>(60 - elapsed / 1000);

  if (pm.actionIdx >= 1 && pm.actionIdx <= 3) {
    if (debug() >= 2) std::fprintf(stderr, "[powermenu] draw: paint idx=%d w=%.0f h=%.0f remaining=%d\n",
                                   pm.actionIdx, w, h, remainingSec);
    eh::power_confirm::paint(cr, static_cast<double>(w), static_cast<double>(h),
                             pm.actionIdx, pointerX, pointerY, remainingSec, sc);
  }

  cairo_restore(cr);
  cairo_surface_flush(pm.buf.cairo_surface());

  wl_surface_attach(pm.surface, pm.buf.wl(), 0, 0);
  wl_surface_damage_buffer(pm.surface, 0, 0, w, h);
  pm.buf.mark_busy();

  // Request frame callback for smooth countdown animation
  if (!pm.frameCallback) {
    pm.frameCallback = wl_surface_frame(pm.surface);
    wl_callback_add_listener(pm.frameCallback, &kPmFrameListener, &pm);
  }

  wl_surface_commit(pm.surface);
  if (pm.display) wl_display_flush(pm.display);
  if (debug() >= 2) std::fprintf(stderr, "[powermenu] draw: committed\n");
}

ClickResult handle_click(PowerMenu& pm, double pointerX, double pointerY) {
   
  if (!pm.open || pm.actionIdx < 1 || pm.actionIdx > 3) return ClickResult::None;

  const eh::config::ShellConfig sc = eh::config::shell_config_snapshot();
  const double us = dock_ui_scale(sc.dock);
  const double w = (pm.outputW > 0) ? (static_cast<double>(pm.outputW) / pm.outputScale)
                                    : (pm.configuredW > 0 ? static_cast<double>(pm.configuredW) : 1.0);
  const double h = (pm.outputH > 0) ? (static_cast<double>(pm.outputH) / pm.outputScale)
                                    : (pm.configuredH > 0 ? static_cast<double>(pm.configuredH) : 1.0);

  const auto pick = eh::power_confirm::pick(
      static_cast<double>(w), static_cast<double>(h),
      pm.actionIdx, pointerX, pointerY, us);

  if (debug() >= 1) {
    const char* names[] = {"None", "Cancel", "Confirm", "Close"};
    std::fprintf(stderr, "[powermenu] click pick=%s xy=%.0f,%.0f\n",
                names[static_cast<int>(pick) + 1], pointerX, pointerY);
  }

  switch (pick) {
    case eh::power_confirm::Pick::Confirm: return ClickResult::Confirm;
    case eh::power_confirm::Pick::Cancel: return ClickResult::Cancel;
    case eh::power_confirm::Pick::Close: return ClickResult::Close;
    default: return ClickResult::None;
  }
}

bool tick(PowerMenu& pm) {
   
  if (!pm.open || pm.actionIdx < 1 || pm.actionIdx > 3) return false;
  const uint64_t now = eh::shell::monotonic_ms();
  const uint64_t elapsed = (pm.startMs != 0) ? (now - pm.startMs) : 0;
  return elapsed >= 60000;
}

} // namespace eh::ui::powermenu
