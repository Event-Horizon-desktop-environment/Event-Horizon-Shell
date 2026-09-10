#pragma once

#include <cairo/cairo.h>

#include <cstddef>
#include <string>
#include <vector>

struct App;

double slider_norm_from_x(double px, int trX, int trW) noexcept;
void settings_draw_trimmed_text_line(cairo_t* cr, const std::string& text, double x, double baselineY,
                                     size_t approxMaxChars, double fadeAlpha, float fontSize, int fontWeight);
void settings_content_column_geom(const App& app, int* contentX, int* contentW);
int settings_content_viewport_h(const App& app);
int settings_mode_dd_pointer_row(double px, double py, int listLeft, int listTop, int listW, int rowH,
                                 int rowCount);
void settings_paint_combo_list_popup(App& app, cairo_t* cr, int x, int y, int w, int rowH, int rowCount,
                                     const char* const* labels, int selectedIdx, int hoverRow, double glassOv,
                                     int scrollPx = 0, int viewPortH = 0, bool opaquePanel = false);
int settings_measure_max_text_advance_px_sans12(const std::vector<std::string>& labels);
