#include "services/tray/icon/tray_stream_icon.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"
#include "desktop_shell/controlcenter/mixer/mixer_stream_icon_resolve.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>

static std::string dock_lower_copy(std::string s) {
   
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

static std::string dock_exe_basename(const std::string& p) {
   
  if (p.empty()) return {};
  size_t end = p.size();
  while (end > 0 && (p[end - 1] == '/' || p[end - 1] == '\\')) end--;
  const size_t slash = p.find_last_of("/\\", end - 1);
  if (slash == std::string::npos) return p.substr(0, end);
  return p.substr(slash + 1, end - slash - 1);
}

// Match substrings both ways, so tray ids like `chrome_status_123` still pair
// with `google-chrome`.
static bool dock_str_overlap_ci(const std::string& hay, const std::string& needle) {
   
  if (needle.size() < 2 || hay.size() < 2) return false;
  const std::string h = dock_lower_copy(hay);
  const std::string n = dock_lower_copy(needle);
  if (h.find(n) != std::string::npos) return true;
  if (n.size() >= 4 && h.size() >= 4 && n.find(h) != std::string::npos) return true;
  return false;
}

static bool dock_tray_item_matches_stream_ci(const DockApp::TrayItem& ti, const std::string& needle) {
   
  if (needle.size() < 2) return false;
  return dock_str_overlap_ci(ti.id, needle) || dock_str_overlap_ci(ti.title, needle);
}

DockTrayIconSnapshot dock_tray_icon_snapshot_for_stream(const DockApp& app,
                                                        const eh::shell::dock::control_center::ControlCenterMixerStream& s) {
   
  DockTrayIconSnapshot out{};
  const std::string appId = dock_lower_copy(s.app_id);
  const std::string appName = dock_lower_copy(s.app_name);
  const std::string binBase = dock_lower_copy(dock_exe_basename(s.process_binary));
  const bool bin_tray_ok =
      binBase.size() >= 2 && binBase != "bwrap" && binBase != "flatpak-bwrap" && binBase != "fuse-overlayfs" &&
      binBase != "snap-confine" && binBase != "xdg-dbus-proxy";

  if (appId.empty() && appName.empty() && (!bin_tray_ok || binBase.empty())) return out;

  std::lock_guard<std::mutex> lock(app.trayMutex);
  for (const auto& ti : app.trayItems) {
    bool hit = dock_tray_item_matches_stream_ci(ti, appId) || dock_tray_item_matches_stream_ci(ti, appName);
    if (bin_tray_ok) hit = hit || dock_tray_item_matches_stream_ci(ti, binBase);
    if (!hit) continue;
    out.iconName = ti.iconName;
    if (ti.pixSurface) {
      out.pixSurfaceRef = cairo_surface_reference(ti.pixSurface);
      out.pixW = std::max(0, ti.pixW);
      out.pixH = std::max(0, ti.pixH);
      if (out.pixW <= 0 || out.pixH <= 0) {
        out.pixW = cairo_image_surface_get_width(ti.pixSurface);
        out.pixH = cairo_image_surface_get_height(ti.pixSurface);
      }
    }
    return out;
  }
  return out;
}

DockMixerStreamIcon dock_mixer_stream_resolve_icon(DockApp& app, const eh::shell::dock::control_center::ControlCenterMixerStream& s) {
   
  DockMixerStreamIcon r{};
  DockTrayIconSnapshot tray = dock_tray_icon_snapshot_for_stream(app, s);
  r.tray_pix = tray.pixSurfaceRef;
  r.tray_pix_w = tray.pixW;
  r.tray_pix_h = tray.pixH;

  if (!tray.iconName.empty()) r.icon = app.icons.tray_icon(tray.iconName);
  if ((!r.icon || !r.icon->surface) && !r.tray_pix) {
    r.icon = mixer_icon_from_pinned_apps(app.icons, app.settings.pinnedApps, s.process_binary);
  }
  if ((!r.icon || !r.icon->surface) && !r.tray_pix) {
    eh::shell::mixer_icon::StreamIconIds ids{};
    ids.icon_name = s.icon_name;
    ids.app_id = s.app_id;
    ids.process_binary = s.process_binary;
    ids.process_path = s.process_path;
    ids.app_name = s.app_name;
    r.icon = eh::shell::mixer_icon::resolve_mixer_stream_theme_icon(app.icons, ids);
  }
  return r;
}
