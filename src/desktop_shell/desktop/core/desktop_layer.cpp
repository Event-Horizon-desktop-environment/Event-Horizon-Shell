#include "desktop_shell/desktop/core/desktop_layer.hpp"
#include "desktop_shell/desktop/icons/desktop_icons.hpp"
#include "desktop_shell/desktop/icons/desktop_icon_menu.hpp"
#include "desktop_shell/desktop/icons/desktop_menu.hpp"
#include "desktop_shell/desktop/core/desktop_app.hpp"
#include <cmath>

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/output/dock_layer_outputs.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/monitor/output_assign.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/core/protocols.hpp"

#include <cairo/cairo.h>

#include "configuration/shell_config.hpp"

#include "viewporter-client-protocol.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace eh::shell::desktop {

bool layer_enabled() {
   
  const char* e = std::getenv("EH_DISABLE_DESKTOP_LAYER");
  return !(e && e[0] == '1');
}

DesktopLayer* layer_from_surface(const DesktopApp& app, wl_surface* s) {
   
  if (!s) return nullptr;
  for (const auto& up : app.layers)
    if (up && (up->surface == s || up->widgetSurface == s || up->menuSurface == s)) return up.get();
  return nullptr;
}

bool pointer_on_desktop_layer(const DesktopApp& app) {
   
  return layer_from_surface(app, app.pointerSurface) != nullptr;
}

bool pointer_enter_desktop(DesktopApp& app, wl_surface* surface) {
   
  if (!surface) return false;
  if (DesktopLayer* dtl = layer_from_surface(app, surface)) {
    for (size_t i = 0; i < app.layers.size(); ++i) {
      if (app.layers[i].get() == dtl) {
        app.pointerLayerIdx = i;
        break;
      }
    }
    return true;
  }
  return false;
}

static void desktop_buffer_release(void* user) {
   
  auto* L = static_cast<DesktopLayer*>(user);
  if (!L || !L->desktopApp) return;
  DesktopApp& app = *L->desktopApp;
  if (!app.marqueeRepaintWhenFree) return;

  paint_layer(app, *L);
  wl_display_flush(app.display);
}

static void desktop_widget_buffer_release(void* user) {
    
  auto* L = static_cast<DesktopLayer*>(user);
  if (!L || !L->desktopApp) return;
  DesktopApp& app = *L->desktopApp;
  if (!app.marqueeRepaintWhenFree) return;

  paint_layer(app, *L);
  wl_display_flush(app.display);
}

static void desktop_layer_configure(void* data, zwlr_layer_surface_v1* surf, uint32_t serial, uint32_t width, uint32_t height) {
   
  auto* L = static_cast<DesktopLayer*>(data);
  if (!L || !L->desktopApp || !L->layer || surf != L->layer) return;
  zwlr_layer_surface_v1_ack_configure(surf, serial);
  if (width > 0) L->configuredWidth = static_cast<int>(width);
  if (height > 0) L->configuredHeight = static_cast<int>(height);
  L->configured = L->configuredWidth > 0 && L->configuredHeight > 0;
  if (L->configured) paint_layer(*L->desktopApp, *L);
}

static void desktop_layer_closed(void* data, zwlr_layer_surface_v1*  ) {
   
  auto* L = static_cast<DesktopLayer*>(data);
  if (L && L->desktopApp) {
    std::cerr << "[desktop] background layer closed by compositor — disabling desktop layers\n";
    clear_layers(*L->desktopApp);
  }
}

static void desktop_widget_layer_configure(void* data, zwlr_layer_surface_v1* surf, uint32_t serial, uint32_t width, uint32_t height) {
   
  auto* L = static_cast<DesktopLayer*>(data);
  if (!L || !L->desktopApp || !L->widgetLayer || surf != L->widgetLayer) return;
  zwlr_layer_surface_v1_ack_configure(surf, serial);
  if (width > 0) L->widgetConfiguredWidth = static_cast<int>(width);
  if (height > 0) L->widgetConfiguredHeight = static_cast<int>(height);
  L->widgetConfigured = L->widgetConfiguredWidth > 0 && L->widgetConfiguredHeight > 0;
  if (L->widgetConfigured) paint_layer(*L->desktopApp, *L);
}

static void desktop_widget_layer_closed(void* data, zwlr_layer_surface_v1*) {
   
  auto* L = static_cast<DesktopLayer*>(data);
  if (L && L->desktopApp) {
    std::cerr << "[desktop] widget layer closed by compositor\n";
    clear_layers(*L->desktopApp);
  }
}

static const zwlr_layer_surface_v1_listener g_desktop_layer_listener = {
    .configure = desktop_layer_configure,
    .closed = desktop_layer_closed,
};

static const zwlr_layer_surface_v1_listener g_desktop_widget_layer_listener = {
    .configure = desktop_widget_layer_configure,
    .closed = desktop_widget_layer_closed,
};

static void desktop_menu_buffer_release(void* user) {
    
  auto* L = static_cast<DesktopLayer*>(user);
  if (!L || !L->desktopApp) return;
  DesktopApp& app = *L->desktopApp;
  if (!app.marqueeRepaintWhenFree) return;

  paint_layer(app, *L);
  wl_display_flush(app.display);
}

static void desktop_menu_layer_configure(void* data, zwlr_layer_surface_v1* surf, uint32_t serial, uint32_t width, uint32_t height) {
   
  auto* L = static_cast<DesktopLayer*>(data);
  if (!L || !L->desktopApp || !L->menuLayer || surf != L->menuLayer) return;
  zwlr_layer_surface_v1_ack_configure(surf, serial);
  if (width > 0) L->menuConfiguredWidth = static_cast<int>(width);
  if (height > 0) L->menuConfiguredHeight = static_cast<int>(height);
  L->menuConfigured = L->menuConfiguredWidth > 0 && L->menuConfiguredHeight > 0;
  if (L->menuConfigured) paint_layer(*L->desktopApp, *L);
}

static void desktop_menu_layer_closed(void* data, zwlr_layer_surface_v1*) {
   
  auto* L = static_cast<DesktopLayer*>(data);
  if (L && L->desktopApp) {
    std::cerr << "[desktop] menu layer closed by compositor\n";
    clear_layers(*L->desktopApp);
  }
}

static const zwlr_layer_surface_v1_listener g_desktop_menu_layer_listener = {
    .configure = desktop_menu_layer_configure,
    .closed = desktop_menu_layer_closed,
};

static void paint_widget_layer(DesktopApp& app, DesktopLayer& L) {
   
  if (!L.widgetSurface || !L.widgetLayer || !L.widgetConfigured) return;
  if (L.widgetConfiguredWidth <= 0 || L.widgetConfiguredHeight <= 0) return;
  if (L.widgetShmBuf.busy()) return;
  const int w = L.widgetConfiguredWidth;
  const int h = L.widgetConfiguredHeight;
  if (!L.widgetShmBuf.ensure(app.shm, eh::shell::kDesktopWidgetsNamespace, w, h)) return;
  L.widgetShmBuf.set_release_hook(desktop_widget_buffer_release, &L);
  cairo_t* cr = L.widgetShmBuf.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  {
    std::string outName;
    if (L.wlOut && app.wl) {
      auto bounds = app.wl->logical_output_bounds();
      for (const auto& b : bounds)
        if (b.output == L.wlOut) { outName = b.name; break; }
    }
    app.widgetHost.paint_all(cr, w, h, outName);
    app.widgetHost.paint_resize_indicators(cr, app.widgetResizeIdx, outName);
  }

  cairo_restore(cr);
  cairo_surface_flush(L.widgetShmBuf.cairo_surface());
  if (L.widgetViewport) wp_viewport_set_destination(L.widgetViewport, w, h);
  if (L.widgetBgEffect && w > 0 && h > 0) {
    wl_region* rgn = wl_compositor_create_region(L.desktopApp->compositor);
    wl_region_add(rgn, 0, 0, static_cast<int32_t>(w), static_cast<int32_t>(h));
    ext_background_effect_surface_v1_set_blur_region(L.widgetBgEffect, rgn);
    wl_region_destroy(rgn);
  }
  wl_surface_attach(L.widgetSurface, L.widgetShmBuf.wl(), 0, 0);
  wl_surface_damage_buffer(L.widgetSurface, 0, 0, w, h);
  L.widgetShmBuf.mark_busy();
  wl_surface_commit(L.widgetSurface);
}

static void paint_menu_layer(DesktopApp& app, DesktopLayer& L) {
    
  if (!L.menuSurface || !L.menuLayer || !L.menuConfigured) return;
  if (L.menuConfiguredWidth <= 0 || L.menuConfiguredHeight <= 0) return;
  if (!app.desktopMenuOpen && !app.iconCtxMenuOpen && !app.mountDialogOpen &&
      !app.worldClockSettingsOpen) {
    if (L.menuSurfaceHidden) return;
    if (L.menuShmBuf.busy()) return;
    // Hide by detaching rather than painting a cleared full-screen buffer:
    // this avoids allocating the menu buffer at all until a menu is first
    // opened, and frees it again after every close.
    wl_surface_attach(L.menuSurface, nullptr, 0, 0);
    wl_surface_commit(L.menuSurface);
    L.menuShmBuf.destroy();
    L.menuSurfaceHidden = true;
    return;
  }
  L.menuSurfaceHidden = false;
  if (L.menuShmBuf.busy()) {
    eh_desktop_log("paint_menu_layer: BUSY skip");
    return;
  }
  const int w = L.menuConfiguredWidth;
  const int h = L.menuConfiguredHeight;
  if (!L.menuShmBuf.ensure(app.shm, eh::shell::kDesktopMenusNamespace, w, h)) {
    eh_desktop_log("paint_menu_layer: ENSURE FAILED w=%d h=%d buf_sz=%zu", w, h, L.menuShmBuf.size());
    return;
  }
  L.menuShmBuf.set_release_hook(desktop_menu_buffer_release, &L);
  cairo_t* cr = L.menuShmBuf.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  size_t layer_idx = static_cast<size_t>(-1);
  for (size_t i = 0; i < app.layers.size(); ++i) {
    if (app.layers[i].get() == &L) {
      layer_idx = i;
      break;
    }
  }
  if (layer_idx != static_cast<size_t>(-1)) {
    desktop_menu_paint(app, layer_idx, cr);
    desktop_icon_menu_paint(app, layer_idx, cr);
  }

  cairo_restore(cr);
  cairo_surface_flush(L.menuShmBuf.cairo_surface());
  if (L.menuViewport) wp_viewport_set_destination(L.menuViewport, w, h);
  if (L.menuBgEffect && w > 0 && h > 0) {
    wl_region* rgn = wl_compositor_create_region(L.desktopApp->compositor);
    wl_region_add(rgn, 0, 0, static_cast<int32_t>(w), static_cast<int32_t>(h));
    ext_background_effect_surface_v1_set_blur_region(L.menuBgEffect, rgn);
    wl_region_destroy(rgn);
  }
  wl_surface_attach(L.menuSurface, L.menuShmBuf.wl(), 0, 0);
  wl_surface_damage_buffer(L.menuSurface, 0, 0, w, h);
  L.menuShmBuf.mark_busy();
  wl_surface_commit(L.menuSurface);
}

void paint_layer(DesktopApp& app, DesktopLayer& L) {
   
  if (!L.surface || !L.layer || !L.configured) return;
  if (L.configuredWidth <= 0 || L.configuredHeight <= 0) return;
  if (L.shmBuf.busy()) {

    app.marqueeRepaintWhenFree = true;
    return;
  }
  app.marqueeRepaintWhenFree = false;
  const int w = L.configuredWidth;
  const int h = L.configuredHeight;
  if (!L.shmBuf.ensure(app.shm, eh::shell::kDesktopShellNamespace, w, h)) return;
  L.shmBuf.set_release_hook(desktop_buffer_release, &L);
  cairo_t* cr = L.shmBuf.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  size_t layer_idx = static_cast<size_t>(-1);
  for (size_t i = 0; i < app.layers.size(); ++i) {
    if (app.layers[i].get() == &L) {
      layer_idx = i;
      break;
    }
  }
  if (layer_idx != static_cast<size_t>(-1)) {
    desktop_icons_kick_initial_scan(app);
    desktop_icons_paint_layer(app, layer_idx, w, h, cr);
  }

  const bool thisLayer =
      app.marqueeLayerIdx < app.layers.size() && app.layers[app.marqueeLayerIdx].get() == &L;
  if (thisLayer && (app.marqueeDragging || app.marqueeVisible)) {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (app.marqueeDragging) {
      x0 = app.marqueeX0;
      y0 = app.marqueeY0;
      x1 = app.marqueeX1;
      y1 = app.marqueeY1;
    } else {
      x0 = app.marqueeFx0;
      y0 = app.marqueeFy0;
      x1 = app.marqueeFx1;
      y1 = app.marqueeFy1;
    }
    const double mx = std::min(x0, x1);
    const double my = std::min(y0, y1);
    const double mw = std::abs(x1 - x0);
    const double mh = std::abs(y1 - y0);
    if (mw >= 2.0 && mh >= 2.0) {
      cairo_set_source_rgba(cr, 0.25, 0.55, 1.0, 0.14);
      cairo_rectangle(cr, mx, my, mw, mh);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, 0.4, 0.65, 1.0, 0.85);
      cairo_set_line_width(cr, 1.0);
      const double dashes[] = {4.0, 4.0};
      cairo_set_dash(cr, dashes, 2, 0.0);
      cairo_rectangle(cr, mx + 0.5, my + 0.5, std::max(0.0, mw - 1.0), std::max(0.0, mh - 1.0));
      cairo_stroke(cr);
    }
  }

  cairo_restore(cr);
  cairo_surface_flush(L.shmBuf.cairo_surface());
  if (L.bgEffect && w > 0 && h > 0) {
    wl_region* rgn = wl_compositor_create_region(L.desktopApp->compositor);
    wl_region_add(rgn, 0, 0, static_cast<int32_t>(w), static_cast<int32_t>(h));
    ext_background_effect_surface_v1_set_blur_region(L.bgEffect, rgn);
    wl_region_destroy(rgn);
  }
  wl_surface_attach(L.surface, L.shmBuf.wl(), 0, 0);
  wl_surface_damage_buffer(L.surface, 0, 0, w, h);
  L.shmBuf.mark_busy();
  wl_surface_commit(L.surface);

  paint_widget_layer(app, L);
  paint_menu_layer(app, L);
}

void paint_all_layers(DesktopApp& app) {
   
  for (auto& up : app.layers) {
    if (up && up->configured) paint_layer(app, *up);
  }
}

void clear_layers(DesktopApp& app) {
   
#ifndef NDEBUG
  const bool had_marquee = app.marqueeDragging || app.marqueeVisible || app.marqueeRepaintWhenFree;
#endif
  for (auto& up : app.layers) {
    if (!up) continue;
    up->menuShmBuf.set_release_hook(nullptr, nullptr);
    if (up->menuViewport) wp_viewport_destroy(up->menuViewport);
    up->menuViewport = nullptr;
    if (up->menuBgEffect) ext_background_effect_surface_v1_destroy(up->menuBgEffect);
    up->menuBgEffect = nullptr;
    if (up->menuLayer) zwlr_layer_surface_v1_destroy(up->menuLayer);
    up->menuLayer = nullptr;
    if (up->menuSurface) wl_surface_destroy(up->menuSurface);
    up->menuSurface = nullptr;
    up->menuShmBuf.destroy();
    up->menuConfigured = false;
    up->widgetShmBuf.set_release_hook(nullptr, nullptr);
    if (up->widgetViewport) wp_viewport_destroy(up->widgetViewport);
    up->widgetViewport = nullptr;
    if (up->widgetBgEffect) ext_background_effect_surface_v1_destroy(up->widgetBgEffect);
    up->widgetBgEffect = nullptr;
    if (up->widgetLayer) zwlr_layer_surface_v1_destroy(up->widgetLayer);
    up->widgetLayer = nullptr;
    if (up->widgetSurface) wl_surface_destroy(up->widgetSurface);
    up->widgetSurface = nullptr;
    up->widgetShmBuf.destroy();
    up->widgetConfigured = false;
    up->shmBuf.set_release_hook(nullptr, nullptr);
    if (up->shellViewport) wp_viewport_destroy(up->shellViewport);
    up->shellViewport = nullptr;
    if (up->bgEffect) ext_background_effect_surface_v1_destroy(up->bgEffect);
    up->bgEffect = nullptr;
    if (up->layer) zwlr_layer_surface_v1_destroy(up->layer);
    up->layer = nullptr;
    if (up->surface) wl_surface_destroy(up->surface);
    up->surface = nullptr;
    up->shmBuf.destroy();
    up->configured = false;
    up->wlOut = nullptr;
  }
  app.layers.clear();
  app.marqueeDragging = false;
  app.marqueeVisible = false;
  app.marqueeRepaintWhenFree = false;
  desktop_icons_clear(app);
#ifndef NDEBUG
  if (had_marquee)
    std::cerr << "[desktop][marquee] gone: desktop layers cleared (surfaces destroyed) destroyed=yes\n";
#endif
}

bool create_layers(DesktopApp& app) {
   
  if (!layer_enabled() || !app.compositor || !app.layerShell) {
    clear_layers(app);
    return true;
  }
  if (!eh::config::shell_config_snapshot().desktopEnabled) {
    clear_layers(app);
    return true;
  }

  std::vector<eh::wayland::LogicalOutputBounds> outputs;
  if (app.wl) {
    outputs = app.wl->logical_output_bounds();
  }
  if (outputs.empty()) {
    return true;
  }

  std::vector<const eh::wayland::LogicalOutputBounds*> targets;
  // The desktop icons follow the dock's configured display (behavior parity with
  // the pre-split in-process desktop, which read dock_.settings.outputName).
  // Falls back to the desktop's own output assignment when the dock has none.
  std::string desktop_out = eh::shell::trim_output_assign(eh::config::shell_config_snapshot().dock.outputName);
  if (desktop_out.empty()) desktop_out = eh::shell::trim_output_assign(app.outputName);
  if (desktop_out.empty() || eh::shell::output_assign_is_auto(desktop_out)) {
    const eh::wayland::LogicalOutputBounds* best = nullptr;
    for (const auto& ob : outputs) {
      if (!ob.output) continue;
      if (!best || ob.global_x < best->global_x ||
          (ob.global_x == best->global_x && ob.global_y < best->global_y))
        best = &ob;
    }
    if (best && best->output) targets.push_back(best);
  } else if (eh::shell::output_assign_is_all_displays(desktop_out)) {
    for (const auto& ob : outputs)
      if (ob.output) targets.push_back(&ob);
  } else {
    for (const auto& ob : outputs)
      if (ob.output && ob.name == desktop_out) targets.push_back(&ob);
  }

  bool same_targets = app.layers.size() == targets.size();
  if (same_targets) {
    for (size_t i = 0; i < targets.size(); ++i) {
      if (!app.layers[i] || app.layers[i]->wlOut != targets[i]->output) {
        same_targets = false;
        break;
      }
    }
  }
  if (same_targets) return true;

  clear_layers(app);

  if (targets.empty()) return true;

  auto destroy_layer_resources = [](DesktopLayer* up) {
    if (!up) return;
    up->menuShmBuf.set_release_hook(nullptr, nullptr);
    if (up->menuViewport) wp_viewport_destroy(up->menuViewport);
    up->menuViewport = nullptr;
    if (up->menuBgEffect) ext_background_effect_surface_v1_destroy(up->menuBgEffect);
    up->menuBgEffect = nullptr;
    if (up->menuLayer) zwlr_layer_surface_v1_destroy(up->menuLayer);
    up->menuLayer = nullptr;
    if (up->menuSurface) wl_surface_destroy(up->menuSurface);
    up->menuSurface = nullptr;
    up->menuShmBuf.destroy();
    up->widgetShmBuf.set_release_hook(nullptr, nullptr);
    if (up->widgetViewport) wp_viewport_destroy(up->widgetViewport);
    up->widgetViewport = nullptr;
    if (up->widgetBgEffect) ext_background_effect_surface_v1_destroy(up->widgetBgEffect);
    up->widgetBgEffect = nullptr;
    if (up->widgetLayer) zwlr_layer_surface_v1_destroy(up->widgetLayer);
    up->widgetLayer = nullptr;
    if (up->widgetSurface) wl_surface_destroy(up->widgetSurface);
    up->widgetSurface = nullptr;
    up->widgetShmBuf.destroy();
    up->shmBuf.set_release_hook(nullptr, nullptr);
    if (up->shellViewport) wp_viewport_destroy(up->shellViewport);
    up->shellViewport = nullptr;
    if (up->bgEffect) ext_background_effect_surface_v1_destroy(up->bgEffect);
    up->bgEffect = nullptr;
    if (up->layer) zwlr_layer_surface_v1_destroy(up->layer);
    up->layer = nullptr;
    if (up->surface) wl_surface_destroy(up->surface);
    up->surface = nullptr;
    up->shmBuf.destroy();
  };

  std::vector<std::unique_ptr<DesktopLayer>> created;
  for (const auto* ob : targets) {
    if (!ob || !ob->output) continue;
    auto L = std::make_unique<DesktopLayer>();
    L->desktopApp = &app;
    L->wlOut = ob->output;
    eh::wayland::LayerSurfaceConfig cfg{};
    cfg.nameSpace = eh::shell::kDesktopShellNamespace;

    cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
    cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    cfg.width = 0;
    cfg.height = 0;
    cfg.exclusiveZone = 0;
    cfg.marginTop = 0;
    cfg.marginRight = 0;
    cfg.marginBottom = 0;
    cfg.marginLeft = 0;
    cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
    if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, ob->output, cfg, &g_desktop_layer_listener, L.get(), &L->surface,
                                           &L->layer)) {
      std::cerr << "[desktop] failed to create desktop shell layer\n";
      for (auto& up : created) destroy_layer_resources(up.get());
      return false;
    }
    if (L->surface && app.wl) {
      if (auto* mgr = app.wl->background_effect_manager()) {
        L->bgEffect = ext_background_effect_manager_v1_get_background_effect(mgr, L->surface);
      }
      if (auto* vp = app.wl->viewporter()) {
        L->shellViewport = wp_viewporter_get_viewport(vp, L->surface);
      }
    }
    L->shmBuf.set_release_hook(desktop_buffer_release, L.get());

    // Menu layer surface (separate namespace for context menus)
    {
      eh::wayland::LayerSurfaceConfig mcfg{};
      mcfg.nameSpace = eh::shell::kDesktopMenusNamespace;
      mcfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
      mcfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
      mcfg.width = 0;
      mcfg.height = 0;
      mcfg.exclusiveZone = 0;
      mcfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
      if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, ob->output, mcfg, &g_desktop_menu_layer_listener, L.get(), &L->menuSurface,
                                             &L->menuLayer)) {
        std::cerr << "[desktop] failed to create menu layer\n";
        for (auto& up : created) destroy_layer_resources(up.get());
        destroy_layer_resources(L.get());
        return false;
      }
      if (L->menuSurface && app.wl) {
        if (auto* mgr = app.wl->background_effect_manager()) {
          L->menuBgEffect = ext_background_effect_manager_v1_get_background_effect(mgr, L->menuSurface);
        }
        if (auto* vp = app.wl->viewporter()) {
          L->menuViewport = wp_viewporter_get_viewport(vp, L->menuSurface);
        }
      }
    }

    // Widget layer surface (separate namespace for per-widget blur rules)
    {
      eh::wayland::LayerSurfaceConfig wcfg{};
      wcfg.nameSpace = eh::shell::kDesktopWidgetsNamespace;
      wcfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
      wcfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
      wcfg.width = 0;
      wcfg.height = 0;
      wcfg.exclusiveZone = 0;
      wcfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
      if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, ob->output, wcfg, &g_desktop_widget_layer_listener, L.get(), &L->widgetSurface,
                                             &L->widgetLayer)) {
        std::cerr << "[desktop] failed to create widget layer\n";
        for (auto& up : created) destroy_layer_resources(up.get());
        destroy_layer_resources(L.get());
        return false;
      }
      if (L->widgetSurface && app.wl) {
        if (auto* mgr = app.wl->background_effect_manager()) {
          L->widgetBgEffect = ext_background_effect_manager_v1_get_background_effect(mgr, L->widgetSurface);
        }
        if (auto* vp = app.wl->viewporter()) {
          L->widgetViewport = wp_viewporter_get_viewport(vp, L->widgetSurface);
        }
      }
    }
    created.push_back(std::move(L));
  }
  for (auto& up : created) {
    if (up->surface) wl_surface_commit(up->surface);
    if (up->widgetSurface) wl_surface_commit(up->widgetSurface);
    if (up->menuSurface) wl_surface_commit(up->menuSurface);
  }
  if (app.display) wl_display_roundtrip(app.display);
  app.layers = std::move(created);
  return true;
}

}
