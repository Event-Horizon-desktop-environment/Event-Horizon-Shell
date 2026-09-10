#pragma once

#include <string_view>
#include <vector>

#include <cairo/cairo.h>

namespace eh::config {
struct ShellConfig;
}
namespace eh::mpris {
struct PlayerSnapshot;
}

namespace eh::widgets {

[[nodiscard]] bool widget_list_contains_media(const eh::config::ShellConfig& sc,
                                              const std::vector<std::string>& widgets);

[[nodiscard]] double dock_media_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc,
                                           std::string_view instance_id, double icon_ref_px, double bar_height,
                                           const eh::mpris::PlayerSnapshot& snap);

[[nodiscard]] bool paint_media_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                                    double x, double y, double slot_w, double slot_h, double icon_ref_px,
                                    const eh::mpris::PlayerSnapshot& snap, bool hovered, bool pressed,
                                    int hoverBtn = -1,
                                    double pointerLocalX = -1, double pointerLocalY = -1,
                                    bool* progress_tick_wanted = nullptr);

}
