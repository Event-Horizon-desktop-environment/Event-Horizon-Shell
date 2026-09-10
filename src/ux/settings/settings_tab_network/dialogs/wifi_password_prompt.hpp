#pragma once

#include <cairo/cairo.h>
#include <cstdint>

struct App;

void wifi_password_prompt_open(App& app, const std::string& ssid, std::uint8_t strength);
void wifi_password_prompt_close(App& app);
bool wifi_password_prompt_visible(const App& app);

void wifi_password_prompt_paint(App& app, cairo_t* cr);
bool wifi_password_prompt_consume_pointer_down(App& app);
void wifi_password_prompt_consume_pointer_up(App& app);
void wifi_password_prompt_consume_pointer_motion(App& app);
void wifi_password_prompt_handle_key(App& app, uint32_t sym, uint32_t state,
                                      const char* utf8, int utf8_len);
// Retry a pending WiFi connection after keyring unlock
void wifi_password_prompt_retry_pending(App& app);
