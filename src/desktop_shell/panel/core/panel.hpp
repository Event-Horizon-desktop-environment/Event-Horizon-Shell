#pragma once

// Event Horizon panel — central state. Brand-new, panel-owned.
// Mirrors the split-out architecture of dock/taskbar (own Wayland
// connection, own toplevel tracking, own tray/MPRIS/animation state) but
// sized for a thin top/bottom indicator strip: no pinned-app drag model,
// no app-drawer, no spotlight. Widgets are the existing slot paints
// (clock, workspaces, tray, battery, ...) hosted in a new three-zone bar.

#include "desktop_shell/panel/core/panel_settings.hpp"
#include "desktop_shell/panel/layout/panel_types.hpp"
#include "desktop_shell/panel/paint/panel_paint.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/toplevel/ext_foreign_toplevels.hpp"
#include "wl/toplevel/foreign_toplevels.hpp"
#include "wl/core/connection.hpp"
#include "wl/color/nightlight.hpp"
#include "wl/buffer/cairo_cpu_buffer.hpp"
#include "wl/surface/vulkan_wayland.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "services/mpris/mpris_player.hpp"
#include "desktop_shell/common/workspace/workspace_strip_types.hpp"
#include "desktop_shell/widgets/workspaces/workspaces_paint.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"

struct wl_compositor;
struct wl_keyboard;
struct wl_output;
struct wl_pointer;
struct wl_seat;
struct wl_shm;
struct wl_surface;
struct wl_display;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
namespace sdbus {
class IConnection;
class IObject;
}  // namespace sdbus
namespace eh::config {
struct ShellConfig;
}

namespace eh::shell::panel {

struct PanelScaledIconCache {
  struct Key {
    const void* src = nullptr;
    int w = 0;
    int h = 0;
    bool operator==(const Key& o) const { return src == o.src && w == o.w && h == o.h; }
  };
  struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept {
      std::size_t h = std::hash<const void*>{}(k.src);
      h ^= std::hash<int>{}(k.w + 0x9e3779b9 + (h << 6) + (h >> 2));
      h ^= std::hash<int>{}(k.h + 0x9e3779b9 + (h << 6) + (h >> 2));
      return h;
    }
  };
  std::unordered_map<Key, cairo_surface_t*, KeyHash> map{};
  std::vector<Key> fifo{};
  static constexpr std::size_t kCap = 128;
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;

  ~PanelScaledIconCache() { clear(); }
  void clear() {
    for (auto& [k, s] : map)
      if (s) cairo_surface_destroy(s);
    map.clear();
    fifo.clear();
  }
  cairo_surface_t* getOrScale(cairo_surface_t* src, int tw, int th);
};

struct PanelOutputLayer {
  PanelApp* panel = nullptr;
  wl_output* wlOut = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  ext_background_effect_surface_v1* bgEffect = nullptr;
  int configuredWidth = 0;
  int configuredHeight = 0;
  bool configured = false;
  bool everConfigured = false;
  std::uint32_t pendingSerial = 0;
  eh::wayland::ShmBuffer shmBuf{};
  eh::wayland::ShmBuffer shmBuf2{};
  eh::wayland::SurfaceExtensions surfExt{};
  eh::wayland::CairoCpuBuffer glRaster{};
  std::unique_ptr<eh::wayland::VulkanLayerSurface> vkLayer{};
  double regionBarW = -1.0;
  double regionBarH = -1.0;
};

struct PanelApp {
  wl_display* display = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  wl_seat* seat = nullptr;
  zwlr_layer_shell_v1* layerShell = nullptr;

  wl_pointer* pointer = nullptr;
  wl_surface* pointerSurface = nullptr;
  double pointerX = 0.0;
  double pointerY = 0.0;
  int hoverSlot = -1;
  int pressedSlot = -1;
  std::uint32_t pressButton = 0;
  double hoverLiftPx = 0.0;
  int mediaHoverZone = -2;

  std::vector<std::unique_ptr<PanelOutputLayer>> layers{};
  bool configured = false;
  bool running = true;
  bool enabled = false;

  PanelSettings settings{};
  bool pendingOutputRebind = false;

  std::unique_ptr<eh::wayland::WaylandConnection> wl;
  bool wlError = false;

  eh::wayland::GammaService* gammaService_ = nullptr;

  eh::wayland::ForeignToplevels* toplevels = nullptr;
  eh::wayland::ExtForeignToplevels* extToplevels = nullptr;
  std::unordered_map<std::string, std::uint64_t> appFirstSeenSerial{};

  eh::icons::IconCache icons{};
  PanelScaledIconCache scaledIcons{};
  std::vector<PanelPaintSlot> cachedLeft{};
  std::vector<PanelPaintSlot> cachedCenter{};
  std::vector<PanelPaintSlot> cachedRight{};
  bool slotsValid = false;
  std::uint64_t slotFpConfigGen = 0;
  std::string slotFpLeftSig{};
  std::string slotFpCenterSig{};
  std::string slotFpRightSig{};

  eh::shell::AnimationManager anim{};

  std::unique_ptr<eh::mpris::DockMpris> mpris{};

  CompositorKind compositorKind = CompositorKind::Unknown;
  std::vector<eh::shell::WorkspaceStripEntry> workspaceStrip{};
  timespec workspaceStripLastPoll{};
  eh::widgets::WsStripAnimState wsStripAnim{};

  mutable std::mutex trayMutex{};
  std::vector<PanelTrayItem> trayItems{};
  int trayEventFd = -1;
  std::unique_ptr<sdbus::IConnection> trayBus{};
  std::unordered_map<std::string, bool> trayMissingLogged{};
  std::atomic<bool> trayStartLaunched{false};
  std::thread trayStartThread{};

  wl_keyboard* keyboard = nullptr;
  xkb_context* xkbCtx = nullptr;
  xkb_keymap* xkbKeymap = nullptr;
  xkb_state* xkbState = nullptr;

  // Popup state (owned by the panel).
  wl_surface* popupSurface = nullptr;
  zwlr_layer_surface_v1* popupLayer = nullptr;
  int popupConfiguredW = 0;
  int popupConfiguredH = 0;
  bool popupConfigured = false;
  std::uint32_t popupPendingSerial = 0;
  eh::wayland::ShmBuffer popupBuf{};
  wl_callback* popupFrameCb = nullptr;
  std::uint64_t popupLastDrawMs = 0;

  PanelPopupKind popupKind = PanelPopupKind::None;
  std::vector<PanelPopupItem> popupItems{};
  std::string popupService{};
  std::string popupMenuPath{};
  int popupHoverItem = -1;
  std::string popupWidgetId{};
  int popupW = 0;
  int popupH = 0;
  int popupAnchorX = 0;

  std::tm calSelectedDate{};
  std::tm calDisplayDate{};

  eh::shell::controlcenter::ControlCenterState ccState{};
  std::string ccWidgetId{};

  double mediaPopupCacheSeekY = 0;
  double mediaPopupCacheCtrlY = 0;
  std::string weatherInstanceId{};

  wl_callback* frameCallback = nullptr;
  bool frameRedrawPending = false;
  std::uint64_t frameCallbackRequestedMs = 0;

  // Auto-hide slide.
  bool reveal = false;
  double animOffsetPx = 0.0;
  double animTargetPx = 0.0;
  int contextHideLastActiveWs = -1;
  std::uint64_t contextHideRevealUntilMs = 0;
  std::uint32_t slideAnimId = 0;

  // Tooltips.
  int tooltipHoverSlot = -1;
  std::chrono::steady_clock::time_point tooltipHoverStart{};
  int tooltipShownSlot = -1;
  std::string tooltipText{};
  wl_surface* tooltipSurface = nullptr;
  zwlr_layer_surface_v1* tooltipLayer = nullptr;
  eh::wayland::ShmBuffer tooltipShm{};
  int tooltipCfgW = 0;
  int tooltipCfgH = 0;
  bool tooltipConfigured = false;

  // Last painted hit rects (surface coords) + bar geometry, for input picking.
  // Updated by panel_draw(); the pointer handlers hit-test against these so
  // paint stays the single source of truth for slot geometry.
  std::vector<PanelWidgetHit> lastHits{};
  double lastBarX = 0, lastBarY = 0, lastBarW = 0, lastBarH = 0;
  // Popup kind dismissed by the current press (toggle-close): a release on
  // the owning slot must not immediately reopen it.
  PanelPopupKind pressDismissedKind = PanelPopupKind::None;

  std::shared_ptr<eh::wayland::VulkanDisplayContext> panelVk;
  bool vkFailed = false;
};

bool panel_init_on_display(PanelApp& app);
void panel_init_deferred_startup(PanelApp& app);
void panel_add_layer(PanelApp& app, wl_output* output);
void panel_destroy_layers(PanelApp& app);
void panel_draw(PanelApp& app);
void panel_schedule_frame(PanelApp& app);
void panel_update_context_hide(PanelApp& app);
void panel_popup_draw(PanelApp& app);
void panel_popup_close(PanelApp& app);
void panel_handle_tray(PanelApp& app);
void panel_maybe_reload_settings(PanelApp& app, const eh::config::ShellConfig& sc);
void panel_cleanup(PanelApp& app);
void panel_apply_autohide_state(PanelApp& app, bool autoHide);
void panel_tooltip_tick(PanelApp& app);
void panel_tooltip_cancel(PanelApp& app);

// Overview toggle hook (middle-click on the workspaces widget): the split-out
// `horizon-panel` child publishes `overview.toggle` on the bus so the
// horizon-stage child flips the overview; a bare in-process run just no-ops.
using PanelOverviewToggleFn = std::function<void()>;
void panel_set_overview_toggle_fn(PanelOverviewToggleFn fn);
void panel_request_overview_toggle();

}  // namespace eh::shell::panel
