#pragma once

#include <cstdint>

namespace hpv::sc {

struct AppState;

void handle_click(AppState& app, int x, int y);
void handle_release(AppState& app);
void handle_motion(AppState& app, int x, int y);
void handle_scroll(AppState& app, int x, int y, double dy);
void trigger_capture(AppState& app);

}
