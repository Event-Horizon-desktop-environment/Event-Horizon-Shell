#pragma once

#include <cairo/cairo.h>

#include "ux/settings/settings_tab_hyprland/widgets/hyprland_anim_popup.hpp"
#include "ux/settings/settings_tab_hyprland/widgets/hyprland_bezier.hpp"

struct App;

static constexpr int kHyprlandContentTop = 122; // kContentTop + kDockChildTabH + 12

void paint_hyprland_tab(App& app, cairo_t* cr, int contentX, int contentW,
                        double cardX, double cardW, double glassOv,
                        double dockMatA, double paintPointerYOffset, int activeSubTab);

bool hyprland_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW);
bool hyprland_m3_handle_pointer_up(App& app, float px, float py, int activeSubTab);
bool hyprland_m3_handle_pointer_move(App& app, float px, float py, int activeSubTab);
void hyprland_m3_handle_pointer_leave(int activeSubTab);
bool hyprland_m3_has_active_slider(const App& app, int activeSubTab);

int hyprland_hit_child_tab(float px, float py, int contentX, int contentW, int tabScrollPx = 0);

void settings_clamp_hyprland_scroll_px(App& app);

bool hyprland_handle_tab_bar_scroll(App& app, float px, float py, double delta_px,
                                    int contentX, int contentW);
void hyprland_clamp_tab_scroll(App& app, int contentW);
void hyprland_center_active_tab(App& app, int contentW);

void hyprland_commit_cfg(App& app);
