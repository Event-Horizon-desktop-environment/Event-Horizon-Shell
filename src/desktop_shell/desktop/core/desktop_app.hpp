#pragma once

#include "desktop_shell/desktop/icons/desktop_icons.hpp"
#include "desktop_shell/desktop/icons/desktop_icon_menu.hpp"
#include "desktop_shell/desktop/core/desktop_layer.hpp"
#include "desktop_shell/desktop/entries/desktop_open_with.hpp"
#include "desktop_shell/desktop/core/desktop_preferences.hpp"
#include "desktop_shell/desktop/entries/desktop_entry_types.hpp"
#include "desktop_shell/desktop/widgets/shared/desktop_widget_host.hpp"
#include "desktop_shell/desktop/core/session.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "wl/core/connection.hpp"

#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct wl_pointer;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace eh::shell::desktop {

enum class MountDeferredKind : std::uint8_t { None, MountDialog, FstabMount, Unmount };

struct DesktopApp {
  std::unique_ptr<eh::wayland::WaylandConnection> wl;
  wl_display* display = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  wl_seat* seat = nullptr;
  zwlr_layer_shell_v1* layerShell = nullptr;
  wl_pointer* pointer = nullptr;
  wl_keyboard* keyboard = nullptr;
  bool wlError = false;

  eh::icons::IconCache iconCache{};

  wl_surface* pointerSurface = nullptr;
  double pointerX = 0;
  double pointerY = 0;

  std::string outputName{};

  // Inlined Session fields
  std::vector<std::unique_ptr<DesktopLayer>> layers;
  size_t pointerLayerIdx = 0;

  bool marqueeDragging = false;
  bool marqueeVisible = false;
  size_t marqueeLayerIdx = 0;
  double marqueeX0 = 0;
  double marqueeY0 = 0;
  double marqueeX1 = 0;
  double marqueeY1 = 0;
  double marqueeFx0 = 0;
  double marqueeFy0 = 0;
  double marqueeFx1 = 0;
  double marqueeFy1 = 0;
  bool marqueeRepaintWhenFree = false;

  std::vector<DesktopIconItem> icons;
  std::vector<int> iconLayoutW;
  std::vector<int> iconLayoutH;
  bool iconScanDone = false;

  DesktopIconArrangement iconArrangement = DesktopIconArrangement::AlignToGrid;
  DesktopIconSort iconSortMode = DesktopIconSort::Name;
  DesktopIconSizeKind iconSizeKind = DesktopIconSizeKind::Medium;

  bool desktopIconsVisible = true;
  bool desktopPrefsLoaded = false;
  std::unordered_map<std::string, DesktopIconPrefs> iconGridByPath;

  int iconGrabIdx = -1;

  std::vector<int> iconDragGroup;
  std::vector<double> iconDragGroupOffX;
  std::vector<double> iconDragGroupOffY;
  double iconGrabDx = 0;
  double iconGrabDy = 0;
  double iconPressX = 0;
  double iconPressY = 0;
  int iconPressIdx = -1;
  bool iconDragging = false;
  size_t iconGrabLayerIdx = 0;

  bool desktopMenuOpen = false;
  DesktopMenuPanel desktopMenuCascade = DesktopMenuPanel::Main;
  int desktopMenuCascadeAnchorRow = 0;
  double desktopMenuCascadeX = 0;
  double desktopMenuCascadeY = 0;
  double desktopMenuMainWidth = 0;
  double desktopMenuCascadeWidth = 0;
  size_t desktopMenuLayerIdx = 0;
  double desktopMenuX = 0;
  double desktopMenuY = 0;
  int desktopMenuHoverMainRow = -1;
  int desktopMenuHoverSubRow = -1;
  int desktopMenuArmMainRow = -1;
  int desktopMenuArmSubRow = -1;

  std::vector<std::string> desktopUndoPasteDestPaths;

  std::vector<IconCtxMenuRow> iconCtxMenuRows;
  std::optional<DesktopEntryInfo> iconCtxDesktopEntry;

  bool iconCtxMenuOpen = false;
  size_t iconCtxMenuLayerIdx = 0;
  double iconCtxMenuX = 0;
  double iconCtxMenuY = 0;
  int iconCtxMenuHoverMainRow = -1;
  int iconCtxMenuHoverCompressRow = -1;
  int iconCtxMenuArmMainRow = -1;
  int iconCtxMenuArmCompressRow = -1;
  bool iconCtxCompressCascadeOpen = false;
  int iconCtxCompressAnchorRow = -1;
  double iconCtxCompressCascadeX = 0;
  double iconCtxCompressCascadeY = 0;
  double iconCtxCompressCascadeW = 0;
  double iconCtxMenuW = 0;
  int iconCtxTargetIdx = -1;

  std::vector<int> iconCtxSelection;
  std::vector<int> iconMarqueeSelection;

  int iconHoverIdx = -1;
  std::optional<std::chrono::steady_clock::time_point> iconLastClickTime;
  int iconLastClickIdx = -1;

  std::optional<std::chrono::steady_clock::time_point> desktopDeferredRescanUntil;
  std::optional<std::chrono::steady_clock::time_point> desktopDeferredRescanLastTick;

  bool mountDialogOpen = false;
  size_t mountDialogLayerIdx = 0;
  double mountDialogX = 0, mountDialogY = 0;
  double mountDialogW = 0, mountDialogH = 0;
  int mountDialogHoverItem = -1;
  int mountDialogArmItem = -1;
  int mountDialogMountBase = 0;
  bool mountDialogBoot = false;
  bool mountDialogDropdownOpen = false;
  std::string mountDialogObjectPath;
  std::string mountDialogLabel;
  std::string mountDialogDevice;
  std::string mountDialogFstype;
  std::string mountDialogUuid;

  MountDeferredKind mountDialogDeferredKind = MountDeferredKind::None;
  std::string mountDialogDeferredObjectPath;
  std::string mountDialogDeferredDevice;
  std::string mountDialogDeferredLabel;
  std::string mountDialogDeferredFstype;
  std::string mountDialogDeferredUuid;
  int mountDialogDeferredMountBase = 0;
  bool mountDialogDeferredBoot = false;

  // Async mount/unmount operation (runs on background thread to avoid freezing the DE)
  std::shared_future<void> mountAsyncOp;

  // World clock settings popup (opened via the settings app's cog)
  bool worldClockSettingsOpen = false;
  size_t worldClockSettingsLayerIdx = 0;
  double worldClockSettingsX = 0, worldClockSettingsY = 0;
  double worldClockSettingsW = 0, worldClockSettingsH = 0;
  int worldClockSettingsHoverItem = -1;
  int worldClockSettingsArmItem = -1;
  int worldClockSettingsOpenDropdown = -1;
  int worldClockSettingsDropdownScroll = 0;
  int worldClockSettingsDropdownHover = -1;
  std::vector<std::string> worldClockSettingsCities;
  std::vector<std::string> worldClockSettingsTimezones;
  std::vector<std::string> worldClockSettingsTzList;
  std::string worldClockSettingsSearch;
  bool worldClockSettingsSearchFocus = false;
  std::vector<std::string> worldClockSettingsTzFiltered;

  // Keyboard state for the world-clock settings search (xkbcommon)
  xkb_context* xkbContext = nullptr;
  xkb_keymap* xkbKeymap = nullptr;
  xkb_state* xkbState = nullptr;

  bool leftButtonDown = false;

  int widgetDragIdx = -1;
  double widgetDragOffX = 0;
  double widgetDragOffY = 0;

  int widgetResizeIdx = -1;
  double widgetResizeStartX = 0;
  double widgetResizeStartScale = 1.0;

  DesktopWidgetHost widgetHost{};

  OpenWithState openWith{};
};

bool desktop_init_on_display(DesktopApp& app);
void desktop_cleanup(DesktopApp& app);

bool desktop_create_layers(DesktopApp& app);
void desktop_clear_layers(DesktopApp& app);

void desktop_pointer_enter(DesktopApp& app, wl_surface* surface, double sx, double sy);
void desktop_pointer_leave(DesktopApp& app);
void desktop_pointer_motion(DesktopApp& app, double sx, double sy);
void desktop_pointer_button(DesktopApp& app, uint32_t button, uint32_t state);

void eh_desktop_log(const char* fmt, ...);

}

