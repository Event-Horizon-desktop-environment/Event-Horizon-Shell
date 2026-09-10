#pragma once

#include <cairo/cairo.h>

#include <string>

#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"

struct DockApp;

struct DockTrayIconSnapshot {
  std::string iconName{};
  cairo_surface_t* pixSurfaceRef = nullptr;
  int pixW = 0;
  int pixH = 0;
};

struct DockMixerStreamIcon {
  const eh::icons::IconEntry* icon = nullptr;
  cairo_surface_t* tray_pix = nullptr;
  int tray_pix_w = 0;
  int tray_pix_h = 0;
};

[[nodiscard]] DockTrayIconSnapshot dock_tray_icon_snapshot_for_stream(const DockApp& app,
                                                                       const eh::shell::dock::control_center::ControlCenterMixerStream& s);

[[nodiscard]] DockMixerStreamIcon dock_mixer_stream_resolve_icon(DockApp& app,
                                                                  const eh::shell::dock::control_center::ControlCenterMixerStream& s);
