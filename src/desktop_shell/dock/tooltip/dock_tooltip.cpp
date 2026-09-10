#include "desktop_shell/dock/tooltip/dock_tooltip.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/output/dock_layer_outputs.hpp"
#include "desktop_shell/dock/input/dock_pick.hpp"
#include "desktop_shell/dock/input/dock_position.hpp"
#include "desktop_shell/dock/paint/dock_strip_geometry.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/shared/popup/popup_position.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/core/protocols.hpp"
#include "wl/buffer/shm_buffer.hpp"

#include "m3/core/primitives/box.hpp"
#include "m3/core/label.hpp"

#include <cairo/cairo.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr std::chrono::milliseconds kTooltipDelayMs{500};
constexpr int kTooltipPaddingH = 10;
constexpr int kTooltipPaddingV = 5;
constexpr int kTooltipFontSize = 12;
constexpr int kTooltipRadius = 4;
constexpr int kTooltipGapFromDock = 4;

std::string resolve_text_for_slot(const PickSlot& slot, const DockPickResult& pr) {
  switch (slot.kind) {
    case PickSlot::Kind::App: {
      auto desktopFile = find_desktop_file_for_appid(slot.key);
      if (desktopFile) {
        auto info = read_desktop_entry_info(*desktopFile);
        if (info && !info->name.empty()) return info->name;
      }
      return slot.key;
    }
    case PickSlot::Kind::Tray: {
      const std::string prefix = eh::shell::kSlotKeyTray;
      if (slot.key.compare(0, prefix.size(), prefix) == 0) {
        const std::string want = slot.key.substr(prefix.size());
        for (const auto& ti : pr.traySnap) {
          if ((ti.service + ti.path) == want) {
            if (!ti.title.empty()) return ti.title;
            if (!ti.id.empty()) return ti.id;
            return ti.service;
          }
        }
      }
      return "Tray";
    }
    case PickSlot::Kind::Settings:        return "Settings";
    case PickSlot::Kind::Spotlight:       return "Search";
    case PickSlot::Kind::AppMenu:         return "Applications";
    case PickSlot::Kind::Smenu:           return "Start Menu";
    case PickSlot::Kind::Launchpad:       return "Launchpad";
    case PickSlot::Kind::AppDrawer:       return "All Apps";
    case PickSlot::Kind::Trash:           return "Trash";
    case PickSlot::Kind::Media:           return {};
    case PickSlot::Kind::ControlCenter:   return "Control Center";
    case PickSlot::Kind::Workspaces:      return "Workspaces";
    case PickSlot::Kind::VolumeMixer:     return "Volume";
    default: return {};
  }
}

static void layer_configure(void* data, zwlr_layer_surface_v1* layer, uint32_t serial,
                            uint32_t width, uint32_t height) {
   
  auto* app = static_cast<DockApp*>(data);
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  app->tooltipCfgW = static_cast<int>(width);
  app->tooltipCfgH = static_cast<int>(height);
  app->tooltipConfigured = true;
}

static void layer_closed(void*, zwlr_layer_surface_v1*) {}

static const zwlr_layer_surface_v1_listener kLayerListener = {
  .configure = layer_configure,
  .closed = layer_closed,
};

static void draw(DockApp& app) {
   
  if (!app.tooltipConfigured || !app.tooltipSurface) return;

  cairo_t* cr = app.tooltipShm.cairo();
  if (!cr) return;

  cairo_surface_t* surf = cairo_get_target(cr);
  int w = app.tooltipShm.width();
  int h = app.tooltipShm.height();

  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  const auto sc = eh::config::shell_config_snapshot();
  const double tooltipAlpha = static_cast<double>(eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::Tooltip));
  const auto ehChrome = eh::config::derived_chrome_colors(sc.appearance);

  {
    m3::Box box;
    box.setColor(static_cast<float>(ehChrome.dockFillR * 0.35), static_cast<float>(ehChrome.dockFillG * 0.35),
                 static_cast<float>(ehChrome.dockFillB * 0.35), static_cast<float>(tooltipAlpha));
    box.setRadius(static_cast<float>(kTooltipRadius));
    box.setGeometry(0, 0, static_cast<float>(w), static_cast<float>(h));
    box.setGlassy(true);
    box.paint(cr);
  }

  {
    m3::Label label;
    label.setText(app.tooltipText);
    label.setColor(static_cast<float>(ehChrome.textR), static_cast<float>(ehChrome.textG),
                   static_cast<float>(ehChrome.textB), static_cast<float>(tooltipAlpha));
    label.setFontSize(static_cast<float>(kTooltipFontSize));
    float tw = 0, th = 0;
    label.measureExtents(tw, th);
    label.paintAt(cr, (static_cast<float>(w) - tw) * 0.5f, (static_cast<float>(h) - th) * 0.5f);
  }

  cairo_surface_flush(surf);

  wl_surface_attach(app.tooltipSurface, app.tooltipShm.wl(), 0, 0);
  wl_surface_damage(app.tooltipSurface, 0, 0, w, h);
  wl_surface_commit(app.tooltipSurface);
  if (app.display) wl_display_flush(app.display);
}

static void destroy(DockApp& app) {
   
  app.tooltipShm.set_release_hook(nullptr, nullptr);
  app.tooltipShm.destroy();
  if (app.tooltipLayer) {
    zwlr_layer_surface_v1_destroy(app.tooltipLayer);
    app.tooltipLayer = nullptr;
  }
  if (app.tooltipSurface) {
    wl_surface_destroy(app.tooltipSurface);
    app.tooltipSurface = nullptr;
  }
  app.tooltipConfigured = false;
  app.tooltipCfgW = 0;
  app.tooltipCfgH = 0;
  app.tooltipText.clear();
  app.tooltipShownSlot = -1;
}

static void create(DockApp& app, const std::string& text, double centerX, int slotIdx) {
   
  if (!app.compositor || !app.layerShell) return;
  if (app.tooltipSurface) return;

  m3::Label measureLabel;
  measureLabel.setText(text);
  measureLabel.setFontSize(static_cast<float>(kTooltipFontSize));
  float tw = 0, th = 0;
  measureLabel.measureExtents(tw, th);

  int pw = static_cast<int>(std::ceil(tw)) + kTooltipPaddingH * 2;
  int ph = static_cast<int>(std::ceil(th)) + kTooltipPaddingV * 2;

  // Find the dock layer the pointer is on (same as popup logic)
  DockOutputLayer* refLayer = dock_popup_margin_reference_layer(const_cast<DockApp&>(app));
  wl_output* output = refLayer ? refLayer->wlOut : nullptr;
  if (!output) {
    for (const auto& slot : app.outputSlots) {
      if (slot && slot->output) { output = slot->output; break; }
    }
  }
  if (!output) return;

  int originX = 0;
  if (refLayer) {
    const int layerW = refLayer->configuredWidth > 0 ? refLayer->configuredWidth : app.configuredWidth;
    for (const auto& u : app.outputSlots) {
      if (u && u->output == refLayer->wlOut && u->logical_w > 0) {
        originX = std::max(0, static_cast<int>(std::lround((static_cast<double>(u->logical_w) - static_cast<double>(layerW)) * 0.5)));
        break;
      }
    }
  }
  const int marginLeft = originX + std::max(0, static_cast<int>(centerX) - pw / 2);
  const int marginBottom = kTooltipGapFromDock;

  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace = eh::shell::kPopupNamespace;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
  cfg.width = static_cast<uint32_t>(pw);
  cfg.height = static_cast<uint32_t>(ph);
  cfg.exclusiveZone = 0;
  cfg.marginLeft = marginLeft;
  cfg.marginBottom = marginBottom;
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  wl_surface* surf = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, output, cfg,
                                          &kLayerListener, &app, &surf, &layer)) {
    return;
  }

  app.tooltipSurface = surf;
  app.tooltipLayer = layer;
  app.tooltipCfgW = 0;
  app.tooltipCfgH = 0;
  app.tooltipConfigured = false;
  app.tooltipText = text;
  app.tooltipShownSlot = slotIdx;

  wl_surface_commit(surf);
  if (app.display) wl_display_roundtrip(app.display);

  if (!app.tooltipConfigured || app.tooltipCfgW <= 0 || app.tooltipCfgH <= 0) {
    destroy(app);
    return;
  }

  if (!app.tooltipShm.ensure(app.shm, "dock_tooltip", app.tooltipCfgW, app.tooltipCfgH)) {
    destroy(app);
    return;
  }
  app.tooltipShm.set_release_hook(nullptr, nullptr);

  draw(app);
}

}

void dock_tooltip_start_timer(DockApp& app, int slotIdx) {
   
  if (!app.settings.dockTooltipsEnabled) return;
  app.tooltipHoverSlot = slotIdx;
  app.tooltipHoverStart = std::chrono::steady_clock::now();
  if (app.tooltipShownSlot >= 0 && app.tooltipShownSlot != slotIdx) {
    destroy(app);
  }
}

void dock_tooltip_cancel(DockApp& app) {
  app.tooltipHoverSlot = -1;
  destroy(app);
}

void dock_tooltip_tick(DockApp& app) {
  if (app.tooltipHoverSlot < 0) return;
  if (!app.settings.dockTooltipsEnabled) return;
  if (app.tooltipShownSlot == app.tooltipHoverSlot) return;

  auto elapsed = std::chrono::steady_clock::now() - app.tooltipHoverStart;
  if (elapsed < kTooltipDelayMs) return;

  DockPickResult pr = dock_pick_at(app, app.pointerX, app.pointerY);
  if (pr.idx < 0 || static_cast<size_t>(pr.idx) >= pr.all.size()) return;

  const PickSlot& slot = pr.all[static_cast<size_t>(pr.idx)];
  std::string text = resolve_text_for_slot(slot, pr);
  if (text.empty()) return;

  auto [slotX, slotW] = dock_strip_slot_xw(app, pr, pr.idx);
  double centerX = slotX + slotW / 2.0;

  create(app, text, centerX, pr.idx);
}

void dock_tooltip_cleanup(DockApp& app) {
  app.tooltipHoverSlot = -1;
  destroy(app);
}
