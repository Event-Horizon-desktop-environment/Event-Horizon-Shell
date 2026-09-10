#pragma once

#include <cairo/cairo.h>
#include <cstdint>

struct App;

// In-window modal for configuring the desktop world_clock widget (timezone
// dropdowns + search). Opened from the cog on a world_clock row in the
// Desktop widgets settings tab; replaces the old separate desktop popup that
// was triggered over IPC.

bool world_clock_popup_visible(const App& app);
void world_clock_popup_open(App& app);
void world_clock_popup_close(App& app);

void world_clock_popup_paint(App& app, cairo_t* cr);
bool world_clock_popup_consume_pointer_down(App& app);
void world_clock_popup_consume_pointer_up(App& app);
void world_clock_popup_consume_pointer_motion(App& app);
bool world_clock_popup_consume_scroll(App& app, double dy);
void world_clock_popup_consume_key(App& app, std::uint32_t sym, std::uint32_t state,
                                   const char* utf8, int utf8_len);

// Search caret blink (frame-synced, same as the widget picker).
void world_clock_popup_queue_caret_frame(App& app);
