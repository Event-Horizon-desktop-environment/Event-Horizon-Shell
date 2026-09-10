#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <string_view>
#include <vector>

namespace eh::config {
struct ShellConfig;
}

struct DockApp;

namespace eh::widgets {

[[nodiscard]] bool widget_list_contains_battery(const eh::config::ShellConfig& sc,
                                                 const std::vector<std::string>& widgets);

[[nodiscard]] double dock_battery_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc,
                                              std::string_view instance_id, double icon_ref_px, double bar_height);

void paint_battery_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id, double x, double y,
                        double slot_w, double slot_h, double icon_ref_px, bool hovered, bool pressed);

void battery_widget_init();
void battery_widget_poll();
void battery_widget_shutdown();

// Popup API.
constexpr int kBatteryPopupW = 260;

int battery_popup_height();

void dock_battery_popup_paint(double pointerX, double pointerY, cairo_t* cr, const eh::config::ShellConfig& sc);

void dock_battery_popup_handle_click(::DockApp& app, double x, double y, uint32_t serial);

}
