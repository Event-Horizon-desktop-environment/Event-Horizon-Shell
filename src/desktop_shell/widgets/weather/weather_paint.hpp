#pragma once

#include <cairo/cairo.h>
#include <string_view>

namespace eh::config {
struct ShellConfig;
}

namespace eh::widgets {

[[nodiscard]] double dock_weather_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc,
                                              std::string_view instance_id, double icon_ref_px, double bar_height);

void paint_weather_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id, double x, double y,
                        double slot_w, double slot_h, double icon_ref_px, bool hovered, bool pressed);

}
