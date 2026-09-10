#pragma once

#include <cairo/cairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "wl/toplevel/ext_foreign_toplevels.hpp"
#include "wl/toplevel/foreign_toplevels.hpp"
#include "wl/surface/vulkan_wayland.hpp"

#include "configuration/shell_renderer_backend.hpp"

#include "desktop_shell/unified/compositor_kind.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "desktop_shell/osd/host/osd_host.hpp"
#include "services/mpris/mpris_player.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "services/tray/manager/tray_item.hpp"

namespace eh::wayland {
class GammaService;
}

namespace eh::shell::launchpad {
class Host;
}

namespace eh::shell::desktop {
struct DesktopApp;
}

struct DockOutputSlot {
  wl_output* output = nullptr;
  zxdg_output_v1* xdg = nullptr;
  uint32_t globalName = 0;

  std::string output_name{};

  int logical_x = 0;
  int logical_y = 0;
  int logical_w = 0;
  int logical_h = 0;

  bool ready = false;

  int32_t current_mode_refresh_mHz = 0;
};

struct WaylandState {
  CompositorKind compositorKind = CompositorKind::Unknown;

  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  wl_compositor* compositor = nullptr;
  zxdg_output_manager_v1* xdgOutputManager = nullptr;
  wl_shm* shm = nullptr;
  wl_seat* seat = nullptr;
  wl_pointer* pointer = nullptr;
  wl_surface* pointerSurface = nullptr;
  wl_keyboard* keyboard = nullptr;
  xkb_context* xkbCtx = nullptr;
  xkb_keymap* xkbKeymap = nullptr;
  xkb_state* xkbState = nullptr;

  xdg_wm_base* xdgBase = nullptr;
  zwlr_layer_shell_v1* layerShell = nullptr;
  wp_viewporter* viewporter = nullptr;
  wp_fractional_scale_manager_v1* fractionalScaleMgr = nullptr;
  zwlr_foreign_toplevel_manager_v1* toplevelManager = nullptr;
  ext_foreign_toplevel_list_v1* extToplevelList = nullptr;
  eh::wayland::ExtForeignToplevels extToplevels{};
  zwlr_gamma_control_manager_v1* gammaControlMgr = nullptr;
  eh::wayland::GammaService* gammaService_ = nullptr;
  ext_idle_notifier_v1* idleNotifier = nullptr;
  ext_session_lock_manager_v1* sessionLockMgr = nullptr;
  ext_background_effect_manager_v1* bgEffectMgr = nullptr;
  zwp_pointer_constraints_v1* pointerConstraints = nullptr;

  struct DeferredBind {
    std::string iface;
    uint32_t name = 0;
    uint32_t version = 0;
  };
  std::vector<DeferredBind> deferredBinds{};

  using Toplevel = eh::wayland::ForeignToplevels::Toplevel;
  eh::wayland::ForeignToplevels toplevels{};

  // System services.

  eh::shell::AnimationManager shellAnim{};
  std::unique_ptr<eh::shell::osd::OsdHost> osdHost{};
  std::unique_ptr<eh::mpris::DockMpris> mpris{};
  eh::icons::IconCache icons{};

  wl_output* dockLayerOutput = nullptr;
  std::vector<std::unique_ptr<DockOutputSlot>> outputSlots{};
  int primaryOutputWidthPx = 0;
  int primaryOutputHeightPx = 0;

  // System tray
  using TrayItem = eh::tray::TrayItem;
  mutable std::mutex trayMutex{};
  std::vector<TrayItem> trayItems{};
  int trayEventFd = -1;
  std::unique_ptr<sdbus::IConnection> trayBus{};
  std::unique_ptr<sdbus::IObject> trayWatcherObj{};
  bool trayWatcherDeferPending = false;
  std::chrono::steady_clock::time_point trayWatcherDeferUntil{};
  std::unordered_map<std::string, bool> trayMissingLogged{};

  // Desktop integration
  eh::shell::launchpad::Host* launchpad = nullptr;
  eh::shell::desktop::DesktopApp* desktopForOutputRebind = nullptr;

  // App tracking
  std::unordered_map<std::string, uint64_t> appFirstSeenSerial{};
  std::string lastLoggedAppSnapshot{};

  // Shared Vulkan resources
  eh::config::ShellRendererBackend dockRendererBackend = eh::config::ShellRendererBackend::Vulkan;
  std::shared_ptr<eh::wayland::VulkanDisplayContext> dockVk;
  std::shared_ptr<eh::wayland::VulkanDisplayContext> deferredVkDrop;
  std::vector<std::unique_ptr<eh::wayland::VulkanLayerSurface>> deferredVkLayers;
  bool dockVkFailed = false;

  // Settings monitoring
  int settingsInotifyFd = -1;

  // Output rebind
  bool pendingDockOutputRebind = false;

  // Benchmark
  bool dockBenchLoggedFirstCommit = false;
};
