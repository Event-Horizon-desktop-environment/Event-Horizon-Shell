#pragma once

#include <vector>

struct DockApp;
struct DockOutputLayer;
struct wl_output;
struct wl_surface;

[[nodiscard]] DockOutputLayer* dock_layer_from_surface(const DockApp& app, wl_surface* s);

[[nodiscard]] std::vector<wl_output*> dock_collect_layer_target_outputs(DockApp& app);
[[nodiscard]] wl_output* dock_pick_layer_output(DockApp& app);
