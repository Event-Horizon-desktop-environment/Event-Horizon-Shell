#pragma once

#include <cairo/cairo.h>

struct App;
struct Settings;

// Appearance child tab IDs.
enum {
  kAppearanceGeneral  = 0,
  kAppearanceTemplates = 1,
};

void paint_appearance_tab(App& app, cairo_t* cr, int contentX, int contentW,
                          double cardX, double cardW, double glassOv,
                          double dockMatA, double paintPointerYOffset);
bool settings_appearance_consume_pointer_down(App& app, int contentX, int contentW);
bool apply_appearance_overlay_slider(App& app, int which, double px);
bool apply_appearance_color_slider(App& app, int which, double px);

void appearance_app_geom(int contentX, int contentW, int idx,
                         int& trX, int& trY, int& trW,
                         int& cardX, int& cardY, int& cardW,
                         int contentTop);

int appearance_overlay_card_top();

int matugen_scheme_ui_index(const Settings& s);
int matugen_mode_ui_index(const Settings& s);
void matugen_scheme_combo_geom(int contentX, int contentW, int matugenRowTop,
                               int* cx, int* cy, int* cw, int* ch);
void matugen_mode_combo_geom(int contentX, int contentW, int matugenRowTop,
                             int* cx, int* cy, int* cw, int* ch);
void matugen_scheme_dd_sync(App& app, int contentX, int contentW);
void matugen_mode_dd_sync(App& app, int contentX, int contentW);
void settings_clamp_appearance_scroll_px(App& app);

extern const char* const kMatugenSchemeLabels[];
extern const char* const kMatugenSchemeValues[];
extern const int kMatugenSchemeCount;
extern const char* const kMatugenModeLabels[];
extern const char* const kMatugenModeValues[];
extern const int kMatugenModeCount;

// Child tab dispatch helpers.
bool appearance_is_templates_child_tab(const App& app);
