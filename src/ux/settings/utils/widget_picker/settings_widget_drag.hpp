#pragma once

#include <cairo/cairo.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct App;

// Widget picker IDs.
inline constexpr const char* kDockWidgetPickIds[] = {
    "pinned_apps",     "running_apps", "tray",           "settings_button",
    "app_menu",        "app_drawer",   "smenu",          "launchpad",      "distro_spotlight", "clock", "weather", "media", "workspaces", "world_clock",
    "control_center",  "notifications", "spacer", "trash", "volume_mixer", "vpn", "battery", "bluetooth",
};
inline constexpr int kDockWidgetPickCount =
    static_cast<int>(sizeof(kDockWidgetPickIds) / sizeof(kDockWidgetPickIds[0]));

// Dock widget-list layout constants.
inline constexpr int kWidgetSectionPadT           = 8;
inline constexpr int kWidgetSectionTitleBlock    = 20;
inline constexpr int kWidgetListTopGap           = 8;
inline constexpr int kWidgetRowH                 = 56;
inline constexpr int kWidgetRowGap                = 8;
inline constexpr int kWidgetListToAddGap          = 18;
inline constexpr int kWidgetAddButtonH           = 40;
inline constexpr int kWidgetSectionPadB           = 8;
inline constexpr int kWidgetSectionBackdropSlackH = 16;
inline constexpr int kWidgetSectionGapBetween    = 8;
inline constexpr double kWidgetRowToggleW        = 52.0;
inline constexpr double kWidgetRowToggleH        = 26.0;
inline constexpr double kWidgetRowRemoveHit      = 32.0;
inline constexpr double kWidgetRowToggleRemoveGap = 8.0;
inline constexpr double kWidgetRowRightPad       = 16.0;
inline constexpr double kWidgetRowDragHandleW    = 48.0;

// Dock child-tab layout.
inline constexpr int kDockChildTabH          = 34;
inline constexpr int kDockWidgetToggleCardH  = 68;

// Dock visibility / appearance card layout helpers.
inline constexpr int kDockVisToggleRows     = 6;
inline constexpr int kDockVisRowPitch       = 60;
inline constexpr int kBorderToggleBandH     = 48;
inline constexpr int kDockAppearSliderRows  = 9;
inline constexpr int kDockCardHeaderReserve = 52;

// Widget-row geometry for hit-testing and drag insertion.
struct WidgetRowGeom {
  size_t index = 0;
  double x = 0;
  double y = 0;
  double w = 0;
  double h = 0;
  double toggleX = 0;
  double toggleY = 0;
  double toggleW = 0;
  double toggleH = 0;
  double removeX = 0;
  double removeY = 0;
  double removeW = 0;
  double removeH = 0;
};

// Display name for a given widget id token.
std::string widget_display_title(const std::string& id);

// Section geometry helpers.
int widget_section_block_height(int nwidgets);
int widget_section_y0(const App& app, int sectionIdx);
int widget_section_y0_for_tab(const App& app, int sectionIdx, int tabIdx);
double settings_dock_widgets_card_fill_height_px(const App& app);
void widget_section_row_inner_geometry(const App& app, double* rowX, double* rowW);
void widget_section_add_button_rect(const App& app, int sectionIdx, double* bx, double* by,
                                    double* bw, double* bh);
double settings_logical_content_y_tab01(const App& app);

// View-model access.
std::vector<std::string>* widgets_for_section(App& app, int section);
const std::vector<std::string>* widgets_for_section_for_tab(const App& app, int section,
                                                            int tabIdx);
const std::vector<std::string>* widgets_for_section_const(const App& app, int section);

// Layout and hit-testing.
void layout_section_widget_rows(const App& app, int sectionIdx,
                                const std::vector<std::string>& widgets,
                                std::optional<size_t> skipIndex,
                                std::vector<WidgetRowGeom>& out);
bool hit_widget_row_toggle_hit(const App& app, int section, size_t& outIdx);
bool hit_widget_row_remove_hit(const App& app, int section, size_t& outIdx);
bool hit_widget_row_drag_hit(const App& app, int section, size_t& outIdx);

// Drag state management.
void widget_drag_update_insert(App& app);
void widget_drag_apply(App& app);

// Drawing helpers.
void settings_draw_drag_handle_row(App& app, cairo_t* cr, double cx, double cyMid,
                                   double alphaMul);
void settings_draw_widget_accent_toggle(App& app, cairo_t* cr, double swX, double swY,
                                        bool hovered, double glassAlpha, bool knobRight);
void settings_draw_row_remove_button(App& app, cairo_t* cr, double x, double y, double sz,
                                     bool hover, double glassAlpha);
