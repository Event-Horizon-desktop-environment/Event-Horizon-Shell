#pragma once

#include "desktop_shell/taskbar/layout/taskbar_types.hpp"
#include "desktop_shell/taskbar/core/taskbar_settings.hpp"

#include <chrono>
#include <cstdint>

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
#include "desktop_shell/desktop/entries/desktop_entry_types.hpp"
#include "desktop_shell/widgets/app_drawer/overlay/app_drawer_overlay.hpp"
#include "desktop_shell/controlcenter/state/control_center_state.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
namespace sdbus { class IConnection; class IObject; }
namespace eh::shell::launchpad { class Host; }
namespace eh::config { struct ShellConfig; }

namespace eh::shell::taskbar {

struct TaskbarOutputLayer {
  TaskbarApp* taskbar = nullptr;
  wl_output* wlOut = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  ext_background_effect_surface_v1* bgEffect = nullptr;
  int configuredWidth = 0;
  int configuredHeight = 0;
  bool configured = false;
  bool everConfigured = false;
  uint32_t pendingSerial = 0;
  eh::wayland::ShmBuffer shmBuf{};
  eh::wayland::ShmBuffer shmBuf2{};
  eh::wayland::SurfaceExtensions surfExt{};
  eh::wayland::CairoCpuBuffer glRaster{};
  std::unique_ptr<eh::wayland::VulkanLayerSurface> vkLayer{};
};

struct TaskbarApp {
  // Wayland globals
  wl_display* display = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  wl_seat* seat = nullptr;
  zwlr_layer_shell_v1* layerShell = nullptr;
  // Pointer / input
  wl_pointer* pointer = nullptr;
  wl_surface* pointerSurface = nullptr;
  double pointerX = 0.0;
  double pointerY = 0.0;
  size_t pointerTaskbarLayerIdx = 0;
  int hoverSlot = -1;
  int pressedSlot = -1;
  double hoverLiftPx = 0.0;

  // Layers
  std::vector<std::unique_ptr<TaskbarOutputLayer>> layers{};
  int configuredWidth = 0;
  int configuredHeight = 0;
  bool configured = false;
  bool running = true;
  bool enabled = false;

  // Settings
  TaskbarSettings settings{};
  bool pendingOutputRebind = false;

  // Pin identity cache
  std::string pinIdentityFingerprint{};
  std::unordered_map<std::string, std::vector<std::string>> pinIdentityKeys{};

  // Pin drag-to-reorder state
  bool pinDragCandidate = false;
  bool pinDragging = false;
  bool pinDragDirty = false;
  double pinDragStartX = 0.0;
  double pinDragStartY = 0.0;
  std::string pinDragKey{};
  std::string pinDragKeyRaw{};
  int pinDragInsertIdx = -1;
  double pinDragFirstLeft = 0.0;   // saved at init — uniform geometry
  double pinDragStride = 0.0;      // saved at init — iconW + gap
  double pinDragIconW = 0.0;       // saved at init — uniform icon width
  bool pinDragGeometryValid = false; // saved at init — explicit flag (firstLeft can legitimately be 0.0 in panel layout)
  std::vector<std::string> pinDragPinsSnapshot{};
  std::vector<std::string> pinDragPaintOrder{};

  // Independent subsystems.

  // Own Wayland connection (isolated from other components)
  std::unique_ptr<eh::wayland::WaylandConnection> wl;
  bool wlError = false;

  // Nightlight gamma control (owned by the session, shared with the taskbar)
  eh::wayland::GammaService* gammaService_ = nullptr;

  // Toplevel tracking (shared pointer to the session's ToplevelTracker — the
  // taskbar does NOT maintain its own because wspace often invalidates handles
  // on secondary Wayland connections; we use the session's tracking which runs
  // on the main display)
  eh::wayland::ForeignToplevels* toplevels = nullptr;
  eh::wayland::ExtForeignToplevels* extToplevels = nullptr;
  std::unordered_map<std::string, uint64_t> appFirstSeenSerial{};

  // Icon cache
  eh::icons::IconCache icons{};

  // Animation
  eh::shell::AnimationManager anim{};
  std::uint32_t launchBounceAnimId = 0;
  std::string launchBounceAnchorNorm{};
  float launchBounceLiftPx = 0.f;

  // MPRIS
  std::unique_ptr<eh::mpris::DockMpris> mpris{};

  // DeskRegion strip
  CompositorKind compositorKind = CompositorKind::Unknown;
  std::vector<eh::shell::WorkspaceStripEntry> workspaceStrip{};
  timespec workspaceStripLastPoll{};
  eh::widgets::WsStripAnimState wsStripAnim{};

  // Tray
  mutable std::mutex trayMutex{};
  std::vector<TaskbarTrayItem> trayItems{};
  int trayEventFd = -1;
  std::unique_ptr<sdbus::IConnection> trayBus{};
  std::unordered_map<std::string, bool> trayMissingLogged{};

  // Logo / icon surfaces
  cairo_surface_t* settingsLogo = nullptr;
  cairo_surface_t* distroSpotlightLogo = nullptr;
  cairo_surface_t* trashEmptyLogo = nullptr;
  cairo_surface_t* trashFullLogo = nullptr;
  bool trashFull = false;

  // Keyboard
  wl_keyboard* keyboard = nullptr;
  xkb_context* xkbCtx = nullptr;
  xkb_keymap* xkbKeymap = nullptr;
  xkb_state* xkbState = nullptr;

  // App drawer
  eh::appdrawer::AppDrawerState appDrawerState{};
  bool appDrawerPowerConfirmOpen = false;
  int appDrawerPowerConfirmIdx = -1;
  uint64_t appDrawerPowerConfirmStartMs = 0;

  // Popup state (owned by taskbar).
  wl_surface* popupSurface = nullptr;
  zwlr_layer_surface_v1* popupLayer = nullptr;
  int popupConfiguredW = 0;
  int popupConfiguredH = 0;
  bool popupConfigured = false;
  uint32_t popupPendingSerial = 0;
  eh::wayland::ShmBuffer popupBuf{};
  wl_callback* popupFrameCb = nullptr;
  uint64_t popupLastDrawMs = 0;
  bool popupMotionDirty = false;

  TaskbarPopupKind popupKind = TaskbarPopupKind::None;
  std::vector<TaskbarPopupItem> popupItems{};
  std::string popupService{};
  std::string popupPath{};
  std::string popupMenuPath{};

  // Auto-hide.
  int triggerHeight = 8;
  bool reveal = false;
  double animOffsetPx = 0.0;
  double animTargetPx = 0.0;
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

  // App context menu state (uses serials instead of raw handles to avoid
  // dangling-pointer issues when sharing the dock's ForeignToplevels)
  std::string popupAppKey{};
  uint64_t popupAppChosenSerial = 0;
  std::vector<std::pair<uint64_t, std::string>> popupAppWindows{};
  std::vector<DesktopAction> popupAppDesktopActions{};
  std::string popupAppDesktopExec{};

  // Calendar state
  std::tm calSelectedDate{};
  std::tm calDisplayDate{};

  // Weather
  std::string weatherInstanceId{};

  // Media player popup state
  double mediaPopupCacheSeekY = 0;
  double mediaPopupCacheCtrlY = 0;

  // Control center
  eh::shell::controlcenter::ControlCenterState ccState{};
  std::string ccWidgetId{};

  // Frame callback (vsync alignment).
  wl_callback* frameCallback = nullptr;
  bool frameRedrawPending = false;
  uint64_t frameCallbackRequestedMs = 0; // watchdog: guards against a frame callback that never fires

  // Fast-start (EH_TASKBAR_FAST_START).
  std::shared_ptr<eh::wayland::VulkanDisplayContext> taskbarVk;
  bool vkFailed = false;

  bool taskbarDrawFastStartDidFullPaint = false;
  bool taskbarFastStartDidPlaceholder = false;
  bool deferTaskbarRedraw = false;

  // Popup dimensions (set by taskbar_popup_create, read by template paint)
  int popupW = 0;
  int popupH = 0;
};

bool taskbar_init_on_display(TaskbarApp& app);
void taskbar_init_deferred_startup(TaskbarApp& app);
void taskbar_add_layer(TaskbarApp& app, wl_output* output);
void taskbar_draw(TaskbarApp& app);
void taskbar_schedule_frame(TaskbarApp& app);
void taskbar_handle_tray(TaskbarApp& app);
void taskbar_maybe_reload_settings(TaskbarApp& app, const eh::config::ShellConfig& sc);
void taskbar_cleanup(TaskbarApp& app);
void taskbar_apply_autohide_state(TaskbarApp& app, bool autoHide);
void taskbar_anim_start_slide(TaskbarApp& app);
void taskbar_tooltip_tick(TaskbarApp& app);
void taskbar_tooltip_cancel(TaskbarApp& app);
void taskbar_toggle_menu(TaskbarApp& app);

// Nightlight toggle hook: the split-out `horizon-taskbar` child has no gamma
// control — the supervisor owns the single gamma client.
// `taskbar_request_nightlight_toggle()` fires the hook installed by
// run_taskbar_standalone (publishes `command.request` `nightlight.toggle`);
// a bare in-process run just no-ops.
using TaskbarNightlightToggleFn = std::function<void()>;
void taskbar_set_nightlight_toggle_fn(TaskbarNightlightToggleFn fn);
void taskbar_request_nightlight_toggle();



}
