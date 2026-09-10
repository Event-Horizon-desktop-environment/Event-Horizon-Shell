#pragma once

#include <cairo/cairo.h>
#include <vector>

struct App;
struct wl_surface;

namespace eh::settings {

void redraw_settings_application(App& app);

namespace widget_picker {

struct WidgetPickerLayout {
  int px = 0;
  int py = 0;
  int pw = 0;
  int ph = 0;
  int closeX = 0;
  int closeY = 0;
  int closeW = 28;
  int closeH = 28;
  int searchX = 0;
  int searchY = 0;
  int searchW = 0;
  int searchH = 44;
  int clearX = 0;
  int clearY = 0;
  int clearW = 26;
  int clearH = 26;
  int gridX = 0;
  int gridY = 0;
  int gridW = 0;
  int gridClipH = 0;
  int gridContentH = 0;
  int gridScrollMax = 0;
  int cardW = 0;
  int cardH = 0;
  int colGap = 12;
  int rowGap = 12;
};

[[nodiscard]] std::vector<int> visible_indices(const App& app);
[[nodiscard]] std::string widget_id_for_visible_index(int vi);

WidgetPickerLayout layout_for(App& app, int nVisible, int viewportW, int viewportH,
                              int settingsContentInsetX);

void paint(App& app, cairo_t* cr, int viewportW, int viewportH, int settingsContentInsetX);

void teardown_layer(App& app);
[[nodiscard]] bool try_create_layer(App& app);
void present_layer_if_needed(App& app);
void close_picker(App& app);

// Search field focus + caret blink (frame-synced via wl_surface.frame).
void focus_search(App& app);
void destroy_caret_frame(App& app);
void queue_caret_frame(App& app);

void viewport_for_pointer(const App& app, wl_surface* ptrSurf, int* vw, int* vh, int* inset);

}
}
