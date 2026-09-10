#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <time.h>

#include "desktop_shell/shared/system/wayland_state.hpp"

#include "desktop_shell/spotlight/search/spotlight_search.hpp"
#include "desktop_shell/dock/core/dock_settings.hpp"

#include "wl/surface/layer_surface.hpp"
#include "wl/buffer/cairo_cpu_buffer.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/surface_extensions.hpp"

#include "desktop_shell/controlcenter/state/control_center_state.hpp"
#include "desktop_shell/common/fs/file_util.hpp"
#include "desktop_shell/common/asset/asset_loader.hpp"
#include "desktop_shell/shared/layout/strip_geometry.hpp"
#include "desktop_shell/common/workspace/workspace_strip_types.hpp"
#include "desktop_shell/widgets/workspaces/workspaces_paint.hpp"
#include "wl/color/nightlight.hpp"
#include "desktop_shell/desktop/entries/desktop_entry_types.hpp"

struct DockApp;
namespace eh::app { struct PollMuxLoop; }

struct DockOutputLayer {
  DockApp* dock = nullptr;
  wl_output* wlOut = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  ext_background_effect_surface_v1* bgEffect = nullptr;
  int configuredWidth = 0;
  int configuredHeight = 0;
  bool configured = false;
  eh::wayland::SurfaceExtensions surfExt{};
  eh::wayland::CairoCpuBuffer glRaster{};

  eh::wayland::ShmBuffer shmRaster{};
  std::unique_ptr<eh::wayland::VulkanLayerSurface> vkLayer{};

  // Partial-damage bookkeeping: `pendingDamage` accumulates buffer-space
  // regions that still need a present; it is cleared only after a successful
  // present so failed/skipped frames never lose track of stale content.
  eh::wayland::DamageRegion pendingDamage{};
  bool damageSeeded = false;
  int lastPresentW = -1;
  int lastPresentH = -1;
  const void* lastBackend = nullptr;
  std::vector<eh::wayland::DamageRect> lastMediaRects{};
};

struct DockApp : WaylandState {
  std::string spotlightQuery{};
  std::vector<SpotlightHit> spotlightHits{};
  int spotlightSel = -1;

  std::string appMenuQuery{};
  std::string appMenuPrevQuery{};
  std::vector<size_t> appMenuPrevCatIndices{};
  std::vector<SpotlightHit> appMenuPinHits{};
  std::vector<SpotlightHit> appMenuHits{};
  int appMenuSel = -1;
  double appMenuScrollPx = 0;
  double appMenuScrollPxCurrent = 0;
  double appMenuScrollPxAnimFrom = 0;
  uint64_t appMenuScrollAnimStartNs = 0;
  cairo_surface_t* appMenuListCache = nullptr;
  int appMenuListCacheW = 0;
  int appMenuListCacheH = 0;
  int appMenuListCacheViewMode = -1;
  uint64_t appMenuListContentGen = 0;
  uint64_t appMenuListCacheContentGen = 0;
  uint64_t popupLastDrawMs = 0;

  bool appMenuSearchFocused = true;

  bool appMenuSmenuMode = false;

  std::vector<std::string> appMenuCategories{};
  std::vector<double> appMenuCategoryWidths{};
  int appMenuSelectedCategory = -1;
  int appMenuCategoryHoverIdx = -1;

  bool appMenuPowerConfirmOpen = false;
  int appMenuPowerConfirmIdx = -1;
  uint64_t appMenuPowerConfirmStartMs = 0;

  int appMenuHoverRow = -1;

  int appMenuDrawerPinHoverIdx = -1;

  int appMenuPowerHoverIdx = -1;

  bool appMenuRowCtxOpen = false;
  int appMenuRowCtxAnchorRow = -1;
  double appMenuRowCtxMenuX = 0;
  double appMenuRowCtxMenuY = 0;
  int appMenuRowCtxHoverItem = -1;
  bool appMenuRowCtxPinnedDock = false;
  bool appMenuRowCtxPinnedStart = false;
  bool appMenuRowCtxPinnedDrawer = false;
  int appMenuRowCtxItemCount = 3;

  bool appMenuPinCtxOpen = false;
  int appMenuPinCtxAnchorIdx = -1;
  double appMenuPinCtxMenuX = 0;
  double appMenuPinCtxMenuY = 0;
  int appMenuPinCtxHoverItem = -1;
  bool appMenuPinCtxPinnedDock = false;

  double appMenuOpenAnimT = 1.0;
  timespec appMenuAnimStartMono{};
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layerSurface = nullptr;

  wl_surface* appMenuHostSurface = nullptr;
  zwlr_layer_surface_v1* appMenuHostLayerSurface = nullptr;
  bool appMenuHostConfigured = false;
  bool appMenuHostMapped = false;
  int appMenuHostBottomMargin = -1;
  int appMenuHostLeftMargin = -1;
  int appMenuHostW = -1;
  int appMenuHostH = -1;
  eh::wayland::ShmBuffer appMenuHostBuf{};

  wl_output* appMenuHostWlOutput = nullptr;

  uint32_t appMenuGrabSerial = 0;
  wl_callback* popupCaretFrameCb = nullptr;
  uint64_t popupCaretBlinkHalf = static_cast<uint64_t>(-1);
  bool popupMotionDirty = false;

  std::vector<std::unique_ptr<DockOutputLayer>> dockLayers{};

  size_t pointerDockLayerIdx = 0;

  wl_callback* frameCallback = nullptr;

  std::uint32_t dockSlideAnimId = 0;
  std::uint32_t dockHoverLiftAnimId = 0;

  int configuredWidth = 0;
  int configuredHeight = 0;
  bool configured = false;
  bool running = true;
  bool sizeDirty = true;

  int dockLastIntrinsicStripWidth = -1;

  bool trayStripNeedsLayerAck = false;
  bool pendingRedraw = false;

  bool mediaMarqueeWantsFrame = false;

  // Partial-damage frame classification: frames scheduled purely by the media
  // marquee may repaint only the marquee region (Weston-style damage
  // coalescing); everything else falls back to a full repaint.
  enum class DockFrameScope : std::uint8_t { Full = 0, MediaMarqueeOnly = 1 };
  DockFrameScope nextFrameScope = DockFrameScope::Full;
  bool externalFrameRequest = false;
  timespec lastFullPaintMono{};
  int64_t partialDamageFrames = 0;
  int64_t fullDamageFrames = 0;

  bool deferDockRedraw = false;

  std::chrono::steady_clock::time_point lastDockDrawTime{};

  // Back-pointer to the event loop so dock_after_display_dispatch can
  // skip the primary display read when rate-limiting (CPU-spin fix).
  eh::app::PollMuxLoop* eventLoopMux = nullptr;

  bool dockFastStartDidPlaceholder = false;
  bool dockFastStartDidFullPaint = false;
  bool dockDeferWidgetsOnNextDraw = false;

  int layerSurfaceCreateDepth = 0;

  void (*launch_settings_override)() = nullptr;

  int pollTimerFd = -1;

  // One-shot timer that wakes the dock when the media progress border has
  // moved enough to be worth repainting. Replaces the old vsync-rate frame
  // chain during playback (idle CPU: ~2.3% -> ~0.1%).
  int mediaAnimTimerFd = -1;

  int dockHeight = 72;
  int triggerHeight = 8;
  int bottomGap = 6;

  bool autoHide = false;
  bool reveal = false;
  double animOffsetPx = 0.0;
  double animTargetPx = 0.0;

  DockSettings settings{};
  std::optional<timespec> settingsMtime{};

  enum class PopupKind : uint8_t {
    None = 0,
    Tray = 1,
    App = 2,

    AppMenu = 3,

    Spotlight = 4,

    ControlCenter = 5,

    Trash = 6,

    Calendar = 7,

    Weather = 8,

    VolumeMixer = 9,

    PowerConfirm = 10,

    Vpn = 11,
    Battery = 12,
    MediaPlayer = 13,
    Bluetooth = 14,
  };
  struct PopupMenuItem {
    std::string label{};
    int32_t id = -1;
    bool enabled = true;
  };
  bool popupOpen = false;
  PopupKind popupKind = PopupKind::None;
  std::string popupService{};
  std::string popupPath{};
  std::string popupMenuPath{};
  std::string popupAppKey{};
  zwlr_foreign_toplevel_handle_v1* popupAppChosenHandle = nullptr;
  std::vector<std::pair<zwlr_foreign_toplevel_handle_v1*, std::string>> popupAppWindows{};
  std::vector<DesktopAction> popupAppDesktopActions{};
  std::string popupAppDesktopExec{};
  std::vector<PopupMenuItem> popupItems{};
  wl_surface* popupSurface = nullptr;
  zwlr_layer_surface_v1* popupLayerSurface = nullptr;
  eh::wayland::CairoCpuBuffer popupGlRaster{};
  eh::wayland::ShmBuffer popupShmRaster{};
  std::unique_ptr<eh::wayland::VulkanLayerSurface> popupVkLayer{};

  wl_callback* popupAnimCallback = nullptr;
  int popupW = 0;
  int popupH = 0;
  int popupAnchorX = 0;
  int popupAnchorY = 0;
  int popupConfiguredX = 0;
  int popupConfiguredY = 0;
  int popupConfiguredW = 0;
  int popupConfiguredH = 0;
  int popupLastMarginBottom = -1;
  int popupLastMarginLeft = -1;

  int popupHoverItem = -1;

  struct PopupMarginOverride {
    wl_output* output = nullptr;
    int marginLeft = 0;
    int marginBottom = 0;
  };
  PopupMarginOverride popupMarginOverride{};

  eh::shell::dock::control_center::ControlCenterState ccState{};

  double dockRectX = 0.0;
  double dockRectY = 0.0;
  double dockRectW = 0.0;
  double dockRectH = 0.0;

  std::string lastLayoutSnapshot{};
  std::string lastWidgetLayoutSnapshot{};

  double pointerX = 0.0;
  double pointerY = 0.0;

  int dockHoverSlot = -1;
  double dockHoverLiftPx = 0.0;
  double dockHoverLiftTarget = 0.0;
  int dockPressedSlot = -1;

  std::string dockLaunchBounceAnchorNorm{};
  float dockLaunchBounceLiftPx = 0.f;
  std::uint32_t dockLaunchBounceAnimId = 0;

  int dockClockTickSignature = -1;
  int dockWorldClockTickSignature = -1;

  int dockControlCenterTickSignature = -1;

  std::vector<eh::shell::WorkspaceStripEntry> workspaceStrip{};
  timespec workspaceStripLastPoll{};
  eh::widgets::WsStripAnimState wsStripAnim{};

  std::tm calSelectedDate{};
  std::tm calDisplayDate{};

  std::string weatherInstanceId{};

  // Media player popup state
  double mediaPopupCacheSeekY = 0;
  double mediaPopupCacheCtrlY = 0;

  PopupKind popupDismissedThisPress = PopupKind::None;

  bool pinDragCandidate = false;
  bool pinDragging = false;
  bool pinDragDirty = false;
  double pinDragStartX = 0.0;
  double pinDragStartY = 0.0;
  std::string pinDragKey{};
  std::string pinDragKeyRaw{};

  int pinDragInsertIdx = -1;

  std::vector<std::string> pinDragPinsSnapshot{};

  std::vector<std::string> pinDragPaintOrder{};

  bool pinDragLayerOnlyNextDraw = false;

  uint32_t pinDragPerfInsertChanges = 0;
  uint32_t pinDragPerfGhostCoalesce = 0;
  uint32_t pinDragPerfScheduleNoop = 0;
  zwlr_foreign_toplevel_handle_v1* pinClickHandle = nullptr;

  std::string dockPinIdentityCacheFingerprint{};
  std::unordered_map<std::string, std::vector<std::string>> dockPinIdentityKeys{};

  cairo_surface_t* settingsLogo = nullptr;
  cairo_surface_t* distroSpotlightLogo = nullptr;
  cairo_surface_t* trashEmptyLogo = nullptr;
  cairo_surface_t* trashFullLogo = nullptr;
  bool trashFull = false;

  // Tooltip
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
};

struct DockPinnedDragGeometry {
  bool valid = false;

  double firstPinnedLeftSurf = 0;

  double slotStrideSurf = 0;

  double iconSurf = 0;
  int pinnedCount = 0;
};

[[nodiscard]] bool eh_dock_pin_drag_perf_enabled();

[[nodiscard]] int eh_dock_fps_log_mode();
void dock_draw(DockApp& app, bool* committed = nullptr);
void dock_schedule_frame(DockApp& app);
int dock_compute_widget_strip_width(DockApp& app, const std::vector<std::string>& l,
                                    const std::vector<std::string>& c, const std::vector<std::string>& r);
[[nodiscard]] DockPinnedDragGeometry dock_pinned_drag_geometry(DockApp& app);
bool dock_save_dock_settings(const DockSettings& s);

bool dock_settings_equal(const DockSettings& a, const DockSettings& b);

bool eh_dock_settings_debug();
void dock_maybe_reload_settings(DockApp& app, const char* source);
[[nodiscard]] int dock_open_settings_inotify();
void dock_drain_settings_inotify(DockApp& app, int inotifyFd);


int dock_effective_icon_px(const DockSettings& s);
int dock_effective_gap_px(const DockSettings& s);
[[nodiscard]] int dock_effective_bar_height_px(const DockSettings& s);
