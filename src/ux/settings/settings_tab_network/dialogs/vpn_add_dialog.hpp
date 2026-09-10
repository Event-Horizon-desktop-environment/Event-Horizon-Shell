#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <string>

struct App;

void vpn_add_dialog_open(App& app);
void vpn_add_dialog_close(App& app);
bool vpn_add_dialog_visible(const App& app);

void vpn_add_dialog_paint(App& app, cairo_t* cr);
bool vpn_add_dialog_consume_pointer_down(App& app);
void vpn_add_dialog_consume_pointer_up(App& app);
void vpn_add_dialog_consume_pointer_motion(App& app);
void vpn_add_dialog_handle_key(App& app, uint32_t sym, uint32_t state,
                                const char* utf8, int utf8_len);
