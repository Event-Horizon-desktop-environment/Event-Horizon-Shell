#pragma once

#include "desktop_shell/dock/core/dock_app.h"

void maybe_log_layout(DockApp& app, const char* reason);

zwlr_layer_surface_v1* dock_popup_parent_layer_surface(DockApp& app);
wl_surface* dock_popup_parent_wl_surface(DockApp& app);
void dock_main_layers_set_keyboard_interactivity(DockApp& app, uint32_t mode);
