#pragma once

#include <string>

struct DockApp;

void dock_start_launch_bounce(DockApp& app, const std::string& app_key_raw, bool cold_start);

double dock_launch_bounce_extra_lift_y(DockApp& app, bool is_app_slot, const std::string& slot_key);
