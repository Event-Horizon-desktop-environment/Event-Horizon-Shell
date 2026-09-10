#pragma once

#include <cstdint>

struct App;
struct wl_surface;

void settings_close_non_default_app_dropdowns(App& app);

void settings_close_mode_dropdowns(App& app);

void on_pointer_leave(App& app);

void on_pointer_motion(App& app, wl_surface* ptrSurf, double x, double y);

void on_pointer_button(App& app, wl_surface* ptrSurf, uint32_t button, uint32_t state,
                       uint32_t pointer_btn_serial = 0);
