#pragma once

#include <cairo/cairo.h>
#include <string>
#include <string_view>
#include <vector>

namespace eh::config {
struct ShellConfig;
}

namespace eh::widgets {

[[nodiscard]] bool widget_list_contains_clock(const eh::config::ShellConfig& sc,
                                              const std::vector<std::string>& widgets);

[[nodiscard]] bool clock_tick_signature_changed(int& cached_signature, const eh::config::ShellConfig& sc,
                                                std::string_view instance_id);

[[nodiscard]] double dock_clock_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc,
                                           std::string_view instance_id, double icon_ref_px, double bar_height);

void paint_clock_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id, double x, double y,
                      double slot_w, double slot_h, double icon_ref_px, bool hovered, bool pressed);

}
