#pragma once

#include <cairo/cairo.h>

struct App;

namespace eh::settings::dashboard_tab {

// Settings sub-tab id (Panels & UI → Dashboard).
inline constexpr int kTabId = 34;

// Card 1 (behaviour): header + enable row + 7 title-only slider rows.
inline constexpr int kCard1Top     = kContentTop + 12;  // 28
inline constexpr int kEnableRowH   = 48;
inline constexpr int kSliderCount  = 7;
inline constexpr int kCard1BodyTop = kCard1Top + kDockCardHeaderReserve;  // 80
inline constexpr int kSliderRowsTop = kCard1BodyTop + kEnableRowH;        // 128
inline constexpr int kCard1H = kDockCardHeaderReserve + kEnableRowH +
                               kSliderCount * kSliderRowH + kSpacingXL;   // 600

// Card 2 (widget order): header reserve then the widget-list section block.
inline constexpr int kCard2Top = kCard1Top + kCard1H + kCardGap;  // 640
inline constexpr int kWidgetsSectionTop = kCard2Top + kDockCardHeaderReserve;  // 692

// Logical content bottom (drives scroll clamping).
int content_bottom(const App& app);

// Slider rows: shared geometry + value application (paint, press, and motion
// all go through these so they can never disagree).
void slider_geom(int contentX, int contentW, int idx, int& trX, int& trY, int& trW);
void apply_slider_x(App& app, int idx, double px);

void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv,
           double paintPointerYOffset);
bool consume_pointer_down(App& app, int contentX, int contentW);

}  // namespace eh::settings::dashboard_tab
