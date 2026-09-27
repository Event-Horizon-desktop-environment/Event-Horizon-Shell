#pragma once

#include "desktop_shell/taskbar/layout/taskbar_types.hpp"
#include "desktop_shell/taskbar/core/taskbar_settings.hpp"
#include "desktop_shell/taskbar/paint/taskbar_paint.hpp"

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
#include "desktop_shell/spotlight/search/spotlight_search.hpp"

#include <cstdint>
#include <functional>
#include <atomic>
#include <array>
#include <thread>
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
namespace eh::config { struct ShellConfig; }

namespace eh::shell::taskbar {

struct TaskbarScaledIconCache {
  struct Key {
    const void* src = nullptr;
    int w = 0;
    int h = 0;
    bool operator==(const Key& o) const { return src == o.src && w == o.w && h == o.h; }
  };
  struct KeyHash {
    size_t operator()(const Key& k) const noexcept {
      size_t h = std::hash<const void*>{}(k.src);
      h ^= std::hash<int>{}(k.w + 0x9e3779b9 + (h << 6) + (h >> 2));
      h ^= std::hash<int>{}(k.h + 0x9e3779b9 + (h << 6) + (h >> 2));
      return h;
    }
  };
  std::unordered_map<Key, cairo_surface_t*, KeyHash> map{};
  std::vector<Key> fifo{};
  static constexpr size_t kCap = 256;
  uint64_t hits = 0;
  uint64_t misses = 0;

  ~TaskbarScaledIconCache() { clear(); }
  void clear() {
    for (auto& [k, s] : map) if (s) cairo_surface_destroy(s);
    map.clear();
    fifo.clear();
  }
  cairo_surface_t* getOrScale(cairo_surface_t* src, int tw, int th);
};

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
  double regionBarW = -1.0;
  double regionBarH = -1.0;
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
  // Output layer those slot indices belong to (output=all mode).
  int hoverLayerIdx = -1;
  int pressedLayerIdx = -1;
  double hoverLiftPx = 0.0;
  int mediaHoverZone = -2;

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
  // Per-pin hit rects (x, w) in paint order at drag init. Used for
  // variable-width insertion math when labels widen pinned slots.
  std::vector<std::pair<double, double>> pinDragRects{};
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
  TaskbarScaledIconCache scaledIcons{};
  std::vector<eh::shell::taskbar::TaskbarPaintSlot> cachedLeft{};
  std::vector<eh::shell::taskbar::TaskbarPaintSlot> cachedCenter{};
  std::vector<eh::shell::taskbar::TaskbarPaintSlot> cachedRight{};
  std::vector<eh::shell::taskbar::TaskbarPaintSlot> cachedAll{};
  bool slotsValid = false;
  std::vector<std::string> slotFpLeftW{};
  std::vector<std::string> slotFpCenterW{};
  std::vector<std::string> slotFpRightW{};
  std::vector<std::string> slotFpPinned{};
  std::vector<std::string> slotFpRunningKeys{};
  std::vector<std::uint64_t> slotFpRunningSerials{};
  std::vector<char> slotFpRunningActive{};
  std::vector<std::string> slotFpTrayIds{};
  bool slotFpPinDragging = false;
  std::string slotFpPinDragKey{};
  bool slotFpShowLabels = false;  // reserved: slot composition no longer varies with labels
  std::uint64_t slotFpConfigGen = 0;
  std::string cachedFloatingPinLookup{};
  bool cachedFloatingPinRunning = false;
  bool cachedFloatingPinActivated = false;

  // Animation
  eh::shell::AnimationManager anim{};

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
  std::atomic<bool> trayStartLaunched{false};
  std::thread trayStartThread{};

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
  int popupHoverItem = -1;
  std::string popupService{};
  std::string popupPath{};
  std::string popupMenuPath{};

  // Auto-hide.
  int triggerHeight = 8;
  bool reveal = false;
  double animOffsetPx = 0.0;
  double animTargetPx = 0.0;
  std::uint32_t slideAnimId = 0;

  // Launch feedback — the same damped-sine icon bounce the dock plays when a
  // slot is activated (cold_start=false: shorter/shallower) or launched from
  // scratch (cold_start=true).
  std::uint32_t launchBounceAnimId = 0;
  float launchBounceLiftPx = 0.f;
  std::string launchBounceAnchorNorm{};

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

  // Per-frame effective label flag, resolved in taskbar_draw() (collapse-
  // when-full may switch labels off for an overflowing bar). Paint and
  // measure read this, never settings.showLabels directly.
  bool effShowLabels = false;
  // Shrink-to-fit label budget for this frame (0 = full width). Set by the
  // paint pass when the labeled strip overflows; measure uses full width.
  double effLabelMaxW = 0.0;
  // Overflow chevron state per output layer, filled by the paint pass when
  // labeled buttons don't fit even at minimum width. The chevron opens these.
  std::vector<std::vector<std::pair<uint64_t, std::string>>> layerOverflow{};
  // Persistent label-width cache (title|fontsize -> px). Titles are measured
  // with one reused scratch context instead of per-call surfaces.
  mutable std::unordered_map<std::string, double> labelWidthCache{};
  cairo_surface_t* labelMeasureSurf = nullptr;
  cairo_t* labelMeasureCr = nullptr;
  // Color Hot-track cache: icon key -> dominant RGB + valid flag.
  mutable std::unordered_map<std::string, std::array<float, 4>> hotTrackCache{};
  // Hot-track cost counters (consumed by the taskbar-perf summary).
  uint64_t hotTrackComputes = 0;
  double hotTrackComputeMs = 0.0;

  // App context menu state (uses serials instead of raw handles to avoid
  // dangling-pointer issues when sharing the dock's ForeignToplevels)
  std::string popupAppKey{};
  uint64_t popupAppChosenSerial = 0;
  std::vector<std::pair<uint64_t, std::string>> popupAppWindows{};
  std::vector<DesktopAction> popupAppDesktopActions{};
  std::string popupAppDesktopExec{};

  // Win7-style hover window previews (thumbnail cards popup).
  std::string thumbGroupKey{};
  std::string thumbAppName{};
  std::string thumbIconId{};
  std::vector<std::pair<uint64_t, std::string>> thumbWindows{};
  int thumbHoverRow = -1;
  bool thumbCloseHover = false;
  bool thumbListMode = false;
  int thumbAnchorX = 0;

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
  uint64_t frameCallbackRequestedMs = 0;
  uint64_t frameDoneCount = 0;
  uint64_t frameWatchdogCount = 0;
  double sectionMs[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  // Fast-start (EH_TASKBAR_FAST_START).
  std::shared_ptr<eh::wayland::VulkanDisplayContext> taskbarVk;
  bool vkFailed = false;

  bool taskbarDrawFastStartDidFullPaint = false;
  bool taskbarFastStartDidPlaceholder = false;
  bool deferTaskbarRedraw = false;

  // Per-layer paint diagnostics (blame-free geometry blame log).
  struct TaskbarLayerDiag {
    int boxW = 0;
    int boxH = 0;
    size_t nSlots = 0;
    double totalW = 0.0;
    double availW = 0.0;
    bool usePanel = false;
    bool useLr = false;
    double hScale = 1.0;
    bool labels = false;
    double effMaxW = 0.0;
    size_t overflowN = 0;
    size_t hitsN = 0;
  };
  std::vector<TaskbarLayerDiag> layerDiag{};
  // Popup dimensions (set by taskbar_popup_create, read by template paint)
  int popupW = 0;
  int popupH = 0;
  // Slot center the current popup was anchored to; a content-driven resize
  // (control-center settle) re-creates the surface at the same anchor.
  int popupAnchorX = 0;

  // Spotlight (distro_spotlight slot) search palette.
  std::string spotlightQuery{};
  std::vector<SpotlightHit> spotlightHits{};
  int spotlightSel = -1;
};

bool taskbar_init_on_display(TaskbarApp& app);
void taskbar_init_deferred_startup(TaskbarApp& app);
void taskbar_add_layer(TaskbarApp& app, wl_output* output);
void taskbar_draw(TaskbarApp& app);
void taskbar_schedule_frame(TaskbarApp& app);
void taskbar_popup_draw(TaskbarApp& app);

// Win7-style hover window previews: state reset + popup paint. Defined in
// taskbar.cpp; the popup-local geometry lives in taskbar_types.hpp.
void taskbar_thumbs_clear(TaskbarApp& app);
void taskbar_thumbs_paint(TaskbarApp& app, cairo_t* cr, int w, int h);
// Close the preview popup if open (used when pin drag starts).
void taskbar_thumbs_dismiss(TaskbarApp& app);
// Per-output hit geometry (output=all gives each monitor its own hits).
const std::vector<TaskbarWidgetHit>& taskbar_layer_hits(const TaskbarApp& app, int layer);
int taskbar_pointer_layer(const TaskbarApp& app);
void taskbar_handle_tray(TaskbarApp& app);
void taskbar_maybe_reload_settings(TaskbarApp& app, const eh::config::ShellConfig& sc);
void taskbar_cleanup(TaskbarApp& app);
void taskbar_apply_autohide_state(TaskbarApp& app, bool autoHide);
void taskbar_anim_start_slide(TaskbarApp& app);
void taskbar_tooltip_tick(TaskbarApp& app);
void taskbar_tooltip_cancel(TaskbarApp& app);
void taskbar_toggle_menu(TaskbarApp& app);

// Launch feedback (dock parity): bounce the launching/activating app slot.
void taskbar_start_launch_bounce(TaskbarApp& app, const std::string& app_key_raw, bool cold_start);
[[nodiscard]] double taskbar_launch_bounce_lift_y(const TaskbarApp& app, const std::string& slot_key);

// Nightlight toggle hook: the split-out `horizon-taskbar` child has no gamma
// control — the supervisor owns the single gamma client.
// `taskbar_request_nightlight_toggle()` fires the hook installed by
// run_taskbar_standalone (publishes `command.request` `nightlight.toggle`);
// a bare in-process run just no-ops.
using TaskbarNightlightToggleFn = std::function<void()>;
void taskbar_set_nightlight_toggle_fn(TaskbarNightlightToggleFn fn);
void taskbar_request_nightlight_toggle();

// Overview toggle hook (middle-click on the workspaces widget): the split-out
// `horizon-taskbar` child publishes `overview.toggle` on the bus so the
// horizon-stage child flips the overview; a bare in-process run just no-ops.
using TaskbarOverviewToggleFn = std::function<void()>;
void taskbar_set_overview_toggle_fn(TaskbarOverviewToggleFn fn);
void taskbar_request_overview_toggle();



}
