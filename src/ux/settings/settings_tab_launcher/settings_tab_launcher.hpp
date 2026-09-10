#pragma once

struct App;
struct Settings;

void launcher_app_geom(int contentX, int contentW, int idx,
                       int& trX, int& trY, int& trW,
                       int& cardX, int& cardY, int& cardW);
void launcher_view_mode_combo_geom(int contentX, int contentW, int* cx, int* cy, int* cw, int* ch);
void paint_launcher_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, double paintPointerYOffset);

void paint_launcher_tab_m3(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool launcher_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW);
bool launcher_m3_handle_pointer_up(App& app, float px, float py);
bool launcher_m3_handle_pointer_move(App& app, float px, float py);
void launcher_m3_handle_pointer_leave();
bool launcher_m3_has_active_slider(const App& app);
void settings_clamp_launcher_scroll_px(App& app);

extern const char* const kViewModeLabels[];
extern const int kViewModeCount;
