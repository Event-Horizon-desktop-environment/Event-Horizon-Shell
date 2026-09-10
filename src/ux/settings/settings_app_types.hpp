#pragma once

#include <cairo/cairo.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "configuration/shell_config.hpp"
#include "m3/controls/containers/button.hpp"
#include "ux/settings/utils/scroll/settings_scroll_controller.hpp"
#include "ux/settings/utils/dropdown/settings_dropdown.hpp"
#include "m3/controls/input/toggle.hpp"
#include "ux/settings/data/settings_desktop_widgets_data.hpp"
#include "ux/settings/data/mango/settings_mango_data.hpp"
#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"
#include "services/audio/pipewire_service.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "wl/core/protocols.hpp"

namespace eh::wayland { class GammaService; }

#include "ux/settings/data/monitors/settings_monitors.hpp"
#include "services/autostart/autostart_service.hpp"
#include "ux/settings/settings_tab_autostart/settings_tab_autostart.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "wl/buffer/cairo_cpu_buffer.hpp"
#include "wl/input/clipboard.hpp"
#include "wl/surface/overlay_premul.hpp"
#include "wl/surface/vulkan_wayland.hpp"
#include "ux/settings/settings_tab_accounts/settings_tab_accounts.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "wl/core/connection.hpp"
#include "wl/core/seat.hpp"

struct Settings {
  bool dockShowDock = true;
  bool dockAutoHide = false;
  int dockRadius = 18;
  int dockIconSize = 44;
  int dockIconSpacing = 15;
  int dockBottomGap = 6;
  int dockExclusiveZoneGap = 0;
  bool dockBorderEnabled = false;
  int dockBorderSize = 1;
  int dockBorderHue = 200;
  int dockBorderOpacity = 100;
  int dockOpacity = 72;
  // `[shell] renderer` — "vulkan" or "cairo". Round-tripped through
  // load_settings()/settings_to_shell_config() so saves never clobber it.
  std::string renderer = "vulkan";
  std::string iconTheme = "";
  std::vector<std::string> pinnedApps{};
  std::vector<std::string> drawerPinnedApps{};
  std::vector<std::string> startMenuPinnedApps{};
  std::vector<std::string> leftWidgets{};
  std::vector<std::string> centerWidgets{};
  std::vector<std::string> rightWidgets{};

  bool nightlightEnabled = false;
  int nightlightDayTemp = 6500;
  int nightlightNightTemp = 4000;
  int nightlightSchedule = 0;        // 0=manual, 1=sunset, 2=scheduled
  int nightlightScheduleStart = 1200; // 20:00 in minutes from midnight
  int nightlightScheduleEnd = 360;    // 06:00 in minutes from midnight

  std::unordered_set<std::string> widgetSlotsDisabled{};
  std::unordered_set<std::string> dockWidgetSlotsDisabled{};
  std::unordered_set<std::string> panelWidgetSlotsDisabled{};
  std::unordered_set<std::string> taskbarWidgetSlotsDisabled{};
  std::unordered_set<std::string> desktopWidgetSlotsDisabled{};
  bool dockWidgetsEnabled = true;
  bool dockGroupApps = true;
  bool dockTooltipsEnabled = true;

  bool dockPinnedAppsTrayPill = false;
  bool dockRunningAppsTrayPill = false;

  double dockScale = 1.0;
  double uiScale = 1.0;

  bool dockBarFollowsIcons = true;
  int dockManualBarHeightPx = 72;
  int slotPillOpacity = 100;
  bool dockLiquidGlass = true;
  bool dockColoredGlass = false;
  bool taskbarEnabled = false;
  int taskbarWidthMode = 0;
  int taskbarHeight = 48;
  int taskbarRadius = 12;
  int taskbarOpacity = 80;
  int taskbarIconSize = 32;
  int taskbarIconSpacing = 8;
  int taskbarFloatingAmount = 8;
  int taskbarEdgeGap = 0;
  int taskbarExclusiveZoneGap = 0;
  double taskbarScale = 1.0;
  bool taskbarPositionTop = false;
  std::vector<std::string> taskbarLeftWidgets{};
  std::vector<std::string> taskbarCenterWidgets{};
  std::vector<std::string> taskbarRightWidgets{};
  std::vector<std::string> taskbarPinnedApps{};
  bool taskbarGroupApps = true;
  int taskbarSlotPillOpacity = 100;
  bool taskbarAutoHide = false;
  bool taskbarTooltipsEnabled = true;
  bool taskbarPinnedAppsTrayPill = false;
  bool taskbarRunningAppsTrayPill = false;
  bool taskbarWidgetsEnabled = true;
  bool taskbarBorder = false;
  int taskbarBorderSize = 1;
  std::string taskbarIconTheme = "";

  bool panelEnabled = false;
  int panelLayoutMode = 0;
  int panelTopGap = 0;
  int panelHeight = 34;
  int panelRadius = 14;
  int panelOpacity = 96;
  std::vector<std::string> panelLeftWidgets{};
  std::vector<std::string> panelCenterWidgets{};
  std::vector<std::string> panelRightWidgets{};
  std::string dockOutputName{};
  std::string desktopWidgetsOutputName{};
  std::string taskbarOutputName{};
  std::string plasmaTheme{};

  bool wallpaperEnabled = false;
  int wallpaperMode = 0;
  std::string wallpaperImage{};
  std::string wallpaperFolder{};

  bool bingEnabled = false;
  bool bingDailyEnabled = false;
  std::string bingDownloadPath{};
  int bingFilter = 0;
  std::string bingBlockedKeywords;

  int wallpaperFolderPickerMode = 0;
  std::string wallpaperVideoPlayerCmd{};

  int workspacesMaxSlots = 5;
  bool workspacesShowApps = true;
  int workspacesMaxIcons = 4;

  bool notificationsDbusEnabled = true;
  bool notificationsDoNotDisturb = false;
  int notificationsDefaultTimeoutMs = 6000;
  bool notificationsToastLayerShellEnabled = true;
  std::string notificationsToastPosition = "top_right";
  int notificationsToastMarginPx = 16;
  int notificationsToastCornerRadiusPx = 12;
  int notificationsToastMaxWidthPx = 420;
  int notificationsScalePct = 100;

  bool matugenThemingEnabled = false;
  bool horizonColorsNative = true;
  bool horizonColorsPaletteOk = false;
  float hcAccentR = 0.90f, hcAccentG = 0.90f, hcAccentB = 0.90f;
  float hcTextR = 0.92f, hcTextG = 0.92f, hcTextB = 0.95f;
  float hcSurfaceR = 0.12f, hcSurfaceG = 0.12f, hcSurfaceB = 0.14f;
  float hcOutlineR = 0.55f, hcOutlineG = 0.60f, hcOutlineB = 0.62f;
  std::string matugenScheme = "scheme-content";
  std::string matugenMode = "dark";
  bool matugenPaletteOk = false;
  float matugenAccentR = 0.90f, matugenAccentG = 0.90f, matugenAccentB = 0.90f;
  float matugenTextR = 0.92f, matugenTextG = 0.92f, matugenTextB = 0.95f;
  float matugenSurfaceR = 0.12f, matugenSurfaceG = 0.12f, matugenSurfaceB = 0.14f;
  float matugenOutlineR = 0.55f, matugenOutlineG = 0.60f, matugenOutlineB = 0.62f;
  eh::config::MatugenExternalTemplateToggles matugenOutputs{};

  // Custom theme (non-dynamic) state.
  bool colorThemeEnabled = false;
  std::string colorThemeName;
  std::string colorThemeSource;

  float themePrimaryR = 0.769f, themePrimaryG = 0.659f, themePrimaryB = 0.941f;
  float themeOnPrimaryR = 1.0f, themeOnPrimaryG = 1.0f, themeOnPrimaryB = 1.0f;
  float themeSecondaryR = 0.239f, themeSecondaryG = 0.125f, themeSecondaryB = 0.439f;
  float themeOnSecondaryR = 1.0f, themeOnSecondaryG = 1.0f, themeOnSecondaryB = 1.0f;
  float themeTertiaryR = 0.910f, themeTertiaryG = 0.522f, themeTertiaryB = 0.290f;
  float themeOnTertiaryR = 1.0f, themeOnTertiaryG = 1.0f, themeOnTertiaryB = 1.0f;
  float themeErrorR = 0.910f, themeErrorG = 0.416f, themeErrorB = 0.353f;
  float themeOnErrorR = 1.0f, themeOnErrorG = 1.0f, themeOnErrorB = 1.0f;
  float themeSurfaceR = 0.102f, themeSurfaceG = 0.075f, themeSurfaceB = 0.188f;
  float themeOnSurfaceR = 1.0f, themeOnSurfaceG = 1.0f, themeOnSurfaceB = 1.0f;
  float themeSurfaceVariantR = 0.133f, themeSurfaceVariantG = 0.102f, themeSurfaceVariantB = 0.227f;
  float themeOnSurfaceVariantR = 1.0f, themeOnSurfaceVariantG = 1.0f, themeOnSurfaceVariantB = 1.0f;
  float themeOutlineR = 0.478f, themeOutlineG = 0.416f, themeOutlineB = 0.588f;
  float themeShadowR = 0.031f, themeShadowG = 0.020f, themeShadowB = 0.063f;
  float themeHoverR = 0.212f, themeHoverG = 0.165f, themeHoverB = 0.337f;
  float themeOnHoverR = 1.0f, themeOnHoverG = 1.0f, themeOnHoverB = 1.0f;

  int colorBrightnessPct = 100;
  int colorContrastPct = 100;
  int colorVibrancePct = 100;
  int colorGammaPct = 100;

  bool overlayOpacityAdvanced = false;
  int overlayOpacityMasterPct = 100;
  int overlayOpacityCcPct = 100;
  int overlayOpacityCcInnerPct = 100;
  int overlayOpacityAppDrawerPct = 100;
  int overlayOpacityLaunchpadPct = 100;
  int overlayOpacitySettingsPct = 100;
  int overlayOpacitySettingsSidebarPct = 100;
  int overlayOpacityDockMenuPct = 100;
  int overlayOpacityDesktopMenuPct = 100;
  int overlayOpacityTrayMenuPct = 100;
  int overlayOpacityCalendarPct = 100;
  int overlayOpacityWeatherPct = 100;
  int overlayOpacityTooltipPct = 100;
  int overlayOpacityWidgetCardPct = 100;
  int overlayOpacityNotificationsPct = 100;

  int launchpadGridColumns = 8;
  int launchpadGridRows = 5;
  int launchpadCellGapPx = 0;
  int launchpadIconFillPct = 58;
  int launchpadLayoutScalePct = 100;
  int launchpadViewMode = 1;
  int launchpadFolderSizePct = 100;
  int launchpadFolderGapPx = 16;
  int launchpadDpiScalePct = 100;

  int overviewAxis = 0;             // 0=Vertical, 1=Horizontal
  int overviewCaptureMode = 0;      // 0=Snapshot, 1=Screencopy (both capture workspaces; Snapshot keeps static captures)
  bool overviewLiveUpdates = false; // Stream live per-window frames while the overview is open
  bool overviewMultiMonitor = false; // Show the overview on all monitors
  int overviewCardScalePct = 50;    // 20-80
  int overviewCardGapPx = 24;       // 8-80
  int overviewScrollDelayMs = 200;  // 50-500
  int overviewCloseBtnSizePx = 34;  // 20-60
  int overviewSearchWidthPx = 420;  // 200-800

  bool desktopEnabled = true;
  eh::config::DefaultAppsSettings defaultApps{};
  std::vector<DesktopWidgetConfig> desktopWidgets{};

  // Power settings
  bool powerDisplaySleep = true;
  int powerDisplaySleepTimeout = 10;
  bool powerIdleSuspend = true;
  int powerIdleSuspendTimeout = 30;
  int powerPowerButtonAction = 0;
  int powerLidCloseAction = 1;
  bool powerShowBatteryPercentage = true;
  std::string powerTunedProfile = "desktop";
  int powerCpuGovernor = 0;       // 0=performance, 1=powersave
  int powerEpp = 2;               // 0=perf, 1=bal_perf, 2=default, 3=bal_power, 4=power

  // Time settings
  bool timeUse24h = false;
  bool timeShowSeconds = false;
  bool timeShowDate = true;
  int timeDateFormat = 0;         // 0=weekday+day, 1=full date, 2=ISO, 3=custom
  std::string timeCustomFormat{};
  std::string timeTimezone{};     // empty = system default
  std::string timeFontSize = "default";

  // Keyboard & Language settings
  std::string keyboardLayout = "us";
  std::vector<std::string> keyboardLayouts = {"us"};
  int keyboardSwitchShortcut = 0; // 0=Alt+Shift, 1=Ctrl+Shift, 2=Super+Space, etc.
  bool keyboardShowLayout = false;
  bool keyboardNumlock = true;
  bool keyboardInputMethodEnabled = false;
  int keyboardCapsLockBehavior = 0; // 0=default, 1=ctrl, 2=swap_esc, 3=disabled
  int keyboardComposeKey = 0;       // 0=none, 1=ralt, 2=rctrl, 3=menu, 4=rwin
  bool keyboardMiddleClickPaste = true;
  int keyboardRepeatRate = 25;      // chars per second
  int keyboardRepeatDelay = 600;    // ms before repeat

  std::string avatarPath{};

  // Audio settings
  std::string audioDefaultSinkName;
  std::string audioDefaultSourceName;
  int audioDefaultSinkVolumePct = 100;
  bool audioDefaultSinkMuted = false;
  int audioDefaultSourceVolumePct = 100;
  bool audioDefaultSourceMuted = false;
  int audioEngineClockRateHz = 48000;
  int audioEngineForceRateHz = 0;
  std::vector<int> audioEngineAllowedRatesHz;
  int audioCompatPcmFormat = 0;
};

struct SidebarItem {
  int id = -1;
  std::string label;
  std::vector<std::string> subLabels;
  bool expanded = false;
};

struct App {
  App();

  eh::wayland::WaylandConnection wl{};
  eh::wayland::WaylandSeat seat{};
  eh::wayland::ClipboardService clipboard{};
  eh::wayland::GammaService* gammaService_ = nullptr;

  wl_surface* surface = nullptr;
  eh::wayland::SurfaceExtensions surfaceExt{};
  xdg_surface* xdgSurface = nullptr;
  xdg_toplevel* toplevel = nullptr;

  int width = 900;
  int height = 980;
  bool running = true;
  bool pendingRedraw = false;

  double pointerX = 0;
  double pointerY = 0;
  bool pointerLeftDown = false;

  Settings settings{};

  // IPC bus publish hook, set by the settings run loop. Components use it to
  // poke other shell processes (e.g. asking the desktop to open a popup).
  std::function<bool(const std::string&, const std::string&)> ipcPublish;

  eh::icons::IconCache icons{};
  std::array<eh::wayland::ShmBuffer, 2> buf{};
  eh::wayland::CairoCpuBuffer glRaster{};
  std::unique_ptr<eh::wayland::VulkanLayerSurface> vkLayer{};
  std::shared_ptr<eh::wayland::VulkanDisplayContext> vkDisplay{};

  bool settingsVkFailed = false;
  bool vkRasterReentrantGuard = false;
  bool settingsDeferRedraw = false;
  bool pendingToggle = false;
  bool maximized = false;
  std::chrono::steady_clock::time_point embedOpenTime;
  cairo_surface_t* logo = nullptr;
  cairo_surface_t* btnMin = nullptr;
  cairo_surface_t* btnMax = nullptr;
  cairo_surface_t* btnClose = nullptr;
  int btnHoverIdx = -1;

  // Unified scroll.
  // All scroll animation/clamping/paint-offset lives in this one controller,
  // so the drawn position and the hit-test position can never diverge.
  eh::settings::ScrollController settingsScroll{};
  bool settingsScrollNeedsRedraw = false;
  int settingsScrollLastActiveTab = -1;

  eh::config::ChromePaintColors drawChrome{};
  bool drawChromeMatugen = false;

  eh::config::ShellConfig settings_paint_shell_snapshot{};
  bool settings_paint_shell_snapshot_valid = false;

  bool embedded = false;
  int activeTab = 0;
  int activeSubTab = -1;

  std::unordered_set<std::string> sidebarExpanded{};
  int sidebarHoverIdx = -1;
  int sidebarScrollPx = 0;

  bool widgetPickerOpen = false;
  std::string widgetPickerSection{};
  bool widgetPickerForPanel = false;
  bool widgetPickerForTaskbar = false;
  bool widgetPickerForDesktop = false;
  std::string widgetPickerFilter{};
  int widgetPickerHoverSlot = -1;
  int widgetPickerScrollPx = 0;
  bool widgetPickerSearchActive = false;
  std::uint64_t widgetPickerCaretBlinkHalf = static_cast<std::uint64_t>(-1);
  wl_callback* widgetPickerCaretFrameCb = nullptr;
  wl_surface* widgetPickerSurface = nullptr;
  xdg_surface* widgetPickerXdgSurface = nullptr;
  xdg_toplevel* widgetPickerToplevel = nullptr;
  eh::wayland::SurfaceExtensions widgetPickerSurfExt{};

  int widgetPickerWinW = 0;
  int widgetPickerWinH = 0;
  std::array<eh::wayland::ShmBuffer, 2> widgetPickerBuf{};

  // World clock settings popup, an in-window modal in the Desktop widgets tab.
  bool worldClockPopupOpen = false;
  bool worldClockPopupJustOpened = false;
  std::vector<std::string> worldClockCities;
  std::vector<std::string> worldClockTimezones;
  std::vector<std::string> worldClockTzList;
  std::vector<std::string> worldClockTzFiltered;
  std::string worldClockSearch;
  bool worldClockSearchFocus = false;
  int worldClockOpenDropdown = -1;
  int worldClockDropdownScroll = 0;
  int worldClockDropdownHover = -1;
  int worldClockHoverItem = -1;
  int worldClockArmItem = -1;
  double worldClockX = 0, worldClockY = 0, worldClockW = 0, worldClockH = 0;
  std::uint64_t worldClockCaretBlinkHalf = static_cast<std::uint64_t>(-1);
  wl_callback* worldClockCaretFrameCb = nullptr;

  bool iconThemePickerOpen = false;
  std::vector<eh::icons::ThemeInfo> iconThemes{};

  bool wallpaperFolderPickerOpen = false;

  bool defaultAppPickerOpen = false;
  int defaultAppPickerCategory = 0;
  int defaultAppPickerScrollPx = 0;
  std::vector<eh::app_drawer::DesktopEntry> defaultAppPickerEntries{};

  std::vector<std::string> defaultAppPickerComboLabels{};

  int defaultAppPickerPopupHoverIdx = -1;

  int defaultAppsDropdownHoverRow = -1;

  std::array<std::array<int, 4>, 9> defaultAppsPillRect{};
  bool defaultAppsPillRectValid = false;

  wl_surface* defaultAppPickerXdgWlSurface = nullptr;
  xdg_surface* defaultAppPickerXdgChildSurface = nullptr;
  xdg_popup* defaultAppPickerXdgPopup = nullptr;
  wl_surface* defaultAppPickerLayerWlSurface = nullptr;
  zwlr_layer_surface_v1* defaultAppPickerLayer = nullptr;
  eh::wayland::SurfaceExtensions defaultAppPickerSurfExt{};
  int defaultAppPickerLayerW = 0;
  int defaultAppPickerLayerH = 0;
  eh::wayland::ShmBuffer defaultAppPickerBuf[2]{};

  int defaultAppPickerCachedAx = 0;
  int defaultAppPickerCachedAy = 0;
  int defaultAppPickerCachedAw = 0;
  int defaultAppPickerCachedAh = 0;
  int defaultAppPickerCachedLw = 0;
  int defaultAppPickerCachedVh = 0;
  int defaultAppPickerCachedGap = 0;
  bool defaultAppPickerXdgGeomValid = false;

  int defaultAppPickerPopupRelToParentX = 0;
  int defaultAppPickerPopupRelToParentY = 0;

  bool panelLayoutDropdownOpen = false;
  bool taskbarWidthModeDropdownOpen = false;
  int taskbarWidthModeDropdownHoverRow = -1;
  bool wallpaperModeDropdownOpen = false;
  bool launcherViewModeDropdownOpen = false;
  int launcherViewModeDropdownHoverRow = -1;
  int panelLayoutDropdownHoverRow = -1;
  int wallpaperModeDropdownHoverRow = -1;

  // Appearance-tab combos live inside these self-contained components. All
  // open/hover/anchor/popup geometry is owned by the component; tabs just
  // feed in labels, the selected index and the anchor each frame.
  eh::settings::SettingsDropdown matugenSchemeDd{};
  eh::settings::SettingsDropdown matugenModeDd{};
  // Dock tab: shell renderer picker (vulkan / cairo).
  eh::settings::SettingsDropdown rendererDd{};

  int settingsDockScrollPx = 0;
  int settingsPanelScrollPx = 0;
  int settingsTaskbarScrollPx = 0;

  int settingsWallpaperScrollPx = 0;

  eh::settings_monitors::MonitorsTabState monitorsTab{};
  int settingsMonitorsScrollPx = 0;

  int settingsSoundScrollPx = 0;

  int settingsAppearanceScrollPx = 0;
  int settingsIconsScrollPx = 0;
  int settingsThemesScrollPx = 0;
  int settingsColorThemesScrollPx = 0;
  int settingsNetworkScrollPx = 0;
  int settingsNetworkHoverRow = -1;

  int settingsMangoScrollPx = 0;
  int mangoContentBottom = 0;
  int mangoSliderDrag = -1;
  eh::settings_mango::MangoConfig mangoConfig{};
  std::vector<std::string> mangoConfigLines{};

  int settingsHyprlandScrollPx = 0;
  int hyprlandContentBottom = 0;
  int hyprlandChildTab = 0;
  int hyprlandTabScrollPx = 0;

  int settingsLauncherScrollPx = 0;
  int launcherContentBottom = 0;
  int launcherChildTab = 0;  // 0=Launcher, 1=Workspaces, 2=Overview
  int wiredChildTab = 0;  // 0=Status, 1=IPv4, 2=IPv6, 3=Ethernet, 4=Security, 5=Advanced
  bool hyprlandLayoutDropdownOpen = false;
  int hyprlandLayoutDropdownHoverRow = -1;
  int hyprlandLayoutComboX = 0;
  int hyprlandLayoutComboY = 0;
  int hyprlandAnimEditIdx = -1;
  bool hyprlandAnimCurveDdOpen = false;
  int hyprlandAnimCurveDdHover = -1;
  bool hyprlandAnimStyleDdOpen = false;
  int hyprlandAnimStyleDdHover = -1;
  bool hyprlandAnimSpeedEditActive = false;
  std::string hyprlandAnimSpeedEditBuf{};
  bool hyprlandBezierOpen = false;
  int hyprlandBezierDrag = -1;    // -1=none, 0=drag P1, 1=drag P2
  double hyprlandBezierP1x = 0.25;
  double hyprlandBezierP1y = 0.1;
  double hyprlandBezierP2x = 0.25;
  double hyprlandBezierP2y = 1.0;

  eh::settings_hyprland::HyprlandConfig hyprlandConfig{};

  int soundActiveDd = -1;
  int soundDdHoverRow = -1;

  int soundVolDragCode = -1;

  std::uint32_t soundVolDragPwNodeId = 0;
  std::uint64_t soundVolPwLastApplyMonoMs = 0;

  std::optional<eh::audio::Snapshot> soundPaintSnapPending{};

  int monitorsScaleSliderDragIdx = -1;

  int monitorsHyprExtraSlider = -1;
  bool monitorsTabDidInitialRefresh = false;

  int monitorsSelectedIdx = 0;
  double monitorsCanvasPanX = 0;
  double monitorsCanvasPanY = 0;
  double monitorsCanvasZoom = 1.0;
  int monitorsCanvasDragIdx = -1;
  int monitorsCanvasDragAnchorX = 0;
  int monitorsCanvasDragAnchorY = 0;
  double monitorsCanvasPressLogicalX = 0;
  double monitorsCanvasPressLogicalY = 0;
  bool monitorsCanvasPanArmed = false;
  double monitorsCanvasPanGrabX = 0;
  double monitorsCanvasPanGrabY = 0;

  int monitorsActiveDd = -1;
  int monitorsDdHoverRow = -1;

  bool monitorsCanvasLayoutReady = false;

  int monitorsCanvasGeomW = 0;
  int monitorsCanvasGeomH = 0;

  static constexpr int kSectionNone = -1;
  int widgetDragSection = kSectionNone;

  int widgetDragTargetSection = kSectionNone;
  int widgetDragFromIndex = -1;
  double widgetDragPressX = 0;
  double widgetDragPressY = 0;
  double widgetDragGrabDx = 0;
  double widgetDragGrabDy = 0;
  bool widgetDragArmed = false;
  bool widgetDragging = false;

  bool settingsWidgetDragRepaintQueued = false;
  size_t widgetDragInsertBefore = 0;

  double settingsSliderDragNormT = -1.0;

  std::uint64_t settingsDragPreviewThrottleLastMs = 0;
  int sliderDrag = -1;
  int wsSliderDrag = -1;

  int launcherSliderDrag = -1;
  int notifSliderDrag = -1;
  bool notifPosDropdownOpen = false;
  int notifPosDropdownHoverRow = -1;
  int timeSliderDrag = -1;
  bool timeFormatDropdownOpen = false;
  int timeFormatDropdownHoverRow = -1;
  bool powerBtnDropdownOpen = false;
  int powerBtnDropdownHoverRow = -1;
  bool lidCloseDropdownOpen = false;
  int lidCloseDropdownHoverRow = -1;
  int settingsKeyboardDdKind = -1; // 0=layout, 1=shortcut, 2=caps, 3=compose
  int settingsKeyboardDdHoverRow = -1;
  int keyboardSliderDrag = -1;
  bool draggingPanelTopGap = false;
  bool draggingPanelHeight = false;
  bool draggingPanelRadius = false;
  bool draggingPanelOpacity = false;
  int appearanceOverlaySliderDrag = -1;
  int appearanceColorSliderDrag = -1;

  int wallpaperUiSubTab = 0;
  int themesSubTab = 0;
  int colorThemesSliderDrag = -1;
  int colorThemeEditorPaletteSliderDrag = -1;
  int colorThemesTab = 0;
  int colorThemesNewNameField = -1;  // -1 = not editing, 0 = editing name
  bool colorThemesMatugenActive = true;
  bool colorThemesEditorOpen = false;
  bool colorThemesDeleteConfirm = false;
  int colorThemesHoverPill = -1;  // 0 = Matugen, 1 = Color Engine, 2 = Custom Theme
  int appearanceChildTab = 0; // 0 = General, 1 = Templates
  bool qtColorSchemeDropdownOpen = false;
  int qtColorSchemeDropdownHoverRow = -1;
  int themesSliderDrag = -1;

  int wallpaperGallerySortMode = 2;
  int wallpaperGalleryColumns = 5;
  int wallpaperGalleryRows = 5;
  int wallpaperGalleryScalePct = 100;
  int wallpaperGalleryThumbRadiusPx = 8;
  int wallpaperUiOpacityPct = 100;
  cairo_surface_t* wallpaperHeroSurf = nullptr;
  std::string wallpaperHeroPath{};
  std::string wallpaperHeroRequest{};

  // Bing Wallpaper state.
  bool bingFilterDropdownOpen = false;
  bool bingYearDropdownOpen = false;
  bool bingMonthDropdownOpen = false;
  bool bingFolderPickerOpen = false;
  bool bingIsCheckingUpdates = false;
  bool bingIsDownloading = false;
  bool bingIsFetchingDaily = false;
  bool bingHasUpdates = false;
  int bingUpdateCount = 0;
  int bingProgressCurrent = 0;
  int bingProgressTotal = 0;
  int bingDownloadedCount = 0;
  int bingFailedCount = 0;
  int bingSkippedCount = 0;
  int bingFoundInArchive = -1;
  int bingSelectedYear = 2026;
  int bingSelectedMonth = 5;
  std::string bingDailyWallpaperPath{};
  std::string bingStatusText{};
  std::string bingStatusType = "idle"; // idle | info | success | error
  std::string bingNewestAvailableDate{};
  bool bingShowBlockedKeywords = false;
  std::string bingLastDownloadedFilename{};

  std::string wallpaperGalleryFolderSynced{};
  time_t wallpaperGalleryFolderMtime = 0;
  std::chrono::steady_clock::time_point wallpaperGalleryLastStatCheck{};
  bool wallpaperGalleryValid = false;   // set false on folder change / explicit refresh; enables lazy ensure
  std::vector<std::string> wallpaperGalleryPaths{};
  std::unordered_map<std::string, time_t> wallpaperGalleryMtimeCache{};
  int wallpaperGallerySortModeApplied = -1;
  bool wallpaperGalleryPrecached = false;

  // Color themes state.
  struct ColorThemeEntry {
    std::string name;
    std::string source;    // "custom" | "catppuccin" | "google" | "gruvbox"
    std::string variant;
    std::string filePath;  // empty for built-in presets
    bool builtIn = false;
  };
  std::vector<ColorThemeEntry> colorThemeList;
  bool colorThemeListNeedsRefresh = true;
  int wallpaperGalleryPage = 0;
  std::unordered_map<std::string, cairo_surface_t*> wallpaperThumbs{};

  std::list<std::string> wallpaperThumbLru{};

  int wallpaperHoveredSlot = -1;
  std::string wallpaperHoveredPath{};
  float wallpaperHoverScale = 1.0f;
  eh::shell::AnimationManager wallpaperHoverAnim{};

  wl_callback* surfaceFrameCb = nullptr;

  eh::shell::AnimationManager embedAnim{};
  float embedPresentT = 1.f;

  m3::Toggle wifiToggle{};
  float toggleAnimProgress = 0.f;

  int last_committed_draw_w = -1;
  int last_committed_draw_h = -1;

  int settingsDesktopWidgetsScrollPx = 0;
  int desktopWidgetSliderDrag = -1;
  int settingsDesktopScrollPx = 0;
  int settingsTimeScrollPx = 0;
  int settingsKeyboardScrollPx = 0;
  int powerChildTab = 0; // 0=Sleep & Power, 1=Performance
  eh::settings::SettingsDropdown powerGovDd{};
  eh::settings::SettingsDropdown powerEppDd{};
  eh::settings::SettingsDropdown powerTunedDd{};
  int settingsPowerScrollPx = 0;
  int settingsBluetoothScrollPx = 0;
  int settingsBluetoothHoverRow = -1;
  int settingsAccountsScrollPx = 0;
  AccountsField accountsActiveField = AccountsField::None;

  // Autostart / Startup Applications tab
  std::vector<eh::autostart::AutostartUiEntry> autostartEntries;
  int autostartScrollPx = 0;
  int autostartHoverRow = -1;
  bool autostartNeedsRefresh = true;
  bool autostartFormOpen = false;
  bool autostartEditMode = false; // false=Add, true=Edit
  std::string autostartEditStem;
  std::string autostartEditName;
  std::string autostartEditExec;
  std::string autostartEditIcon;
  int autostartEditDelay = 0;
  AutostartField autostartActiveField = AutostartField::None;
  int autostartDeleteConfirmRow = -1;
  bool autostartAppBrowserOpen = false;
  std::vector<eh::autostart::InstalledApp> autostartInstalledApps;
  int autostartAppBrowserScrollPx = 0;
  int autostartAppBrowserHoverRow = -1;
  int autostartDefAppsHoverIdx = -1;
  std::string accountsPwNew;
  std::string accountsPwConfirm;
  std::string accountsCuUsername;
  std::string accountsCuFullName;
  std::string accountsCuPassword;
  std::string accountsCuConfirm;
  std::string accountsHostnameEdit;
  std::string accountsStatusMsg;

  m3::Button testNotifBtn;

  // Tab content cache (for scroll animation).
  cairo_surface_t* tabContentCache = nullptr;
  int tabContentCacheW = 0;
  int tabContentCacheH = 0;
  bool tabContentDirty = true;

  bool blankLoggedForActiveTab = false;
};
