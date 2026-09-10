#pragma once

#include <cairo/cairo.h>

#include <cstdint>
#include <string>

struct App;

// Opens a modal keyring password prompt.
// Returns the entered password via app.keyringPasswordResult.
// On first use (create mode): two fields (password + confirm).
// On unlock mode: one field.
void keyring_prompt_open(App& app, bool createMode);
void keyring_prompt_close(App& app);
bool keyring_prompt_visible(const App& app);

void keyring_prompt_paint(App& app, cairo_t* cr);
bool keyring_prompt_consume_pointer_down(App& app);
void keyring_prompt_consume_pointer_up(App& app);
void keyring_prompt_consume_pointer_motion(App& app);
void keyring_prompt_handle_key(App& app, uint32_t sym, uint32_t state,
                                const char* utf8, int utf8_len);
