#pragma once

#include "desktop_shell/dock/core/dock_app.h"

#include <wayland-client.h>

struct wl_registry_listener;

extern const wl_registry_listener g_registry_listener;

void dock_attach_output_layer_fractional_scale(DockApp& app, DockOutputLayer& L);
void dock_bind_xdg_all_slots(DockApp& app);
void dock_bind_deferred_globals(DockApp& app);
