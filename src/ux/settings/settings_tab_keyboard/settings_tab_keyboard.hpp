#pragma once
#include <cairo/cairo.h>
#include <string>
#include <vector>
struct App;
void paint_keyboard_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_keyboard_consume_pointer_down(App& app, int contentX, int contentW);

// Friendly display name for an xkb layout code ("us(dvorak)" -> "US (Dvorak)").
const char* keyboard_layout_display_name(const std::string& code);

// Push persisted keyboard settings into the active compositor (best effort).
void keyboard_push_to_compositor(App& app);

// Known layouts not yet enabled (for the add-layout dropdown).
std::vector<std::string> keyboard_available_layouts(const App& app);

// Installed system locales (localectl, cached) and the current system locale.
std::vector<std::string> keyboard_system_locales();
std::string keyboard_system_locale();
int keyboard_locale_count();

// Human line for the login-screen keymap, e.g. "us,de".
std::string keyboard_x11_keymap_line();

// Apply the staged locale / current keyboard list system-wide (pkexec).
// On success msg describes the result; returns false with msg on failure.
bool keyboard_apply_locale(const std::string& locale, std::string& msg);
bool keyboard_apply_x11_keymap(const App& app, std::string& msg);

extern const char* kSwitchShortcutLabels[];
extern const int kSwitchShortcutCount;
extern const char* kCapsLockLabels[];
extern const int kCapsLockCount;
extern const char* kComposeKeyLabels[];
extern const int kComposeKeyCount;

// Layout constants exposed for pointer motion handler and draw dispatch
namespace keyboard_tab {
static constexpr int kCardTop = 76;
static constexpr int kBodyTop = kCardTop + 52;
static constexpr int kLayoutCardH = 52 + 3 * 68 + 24 + 12;
static constexpr int kBehaviorCardH = 52 + 3 * 68 + 24 + 12;
static constexpr int kRepeatCardH = 52 + 2 * 68 + 24;
static constexpr int kCardGap = 12;
static constexpr int kSourcesRowH = 44;
static constexpr int kLocaleRowH = 30;
static constexpr int kComboW = 300;
static constexpr int kComboH = 28;
static constexpr int kRepeatValueW = 76; // persistent value label reserve

// Dynamic geometry: the sources card grows with the enabled-layout count,
// the language card with the installed-locale count.
int sources_card_top();
int sources_card_h(const App& app);
int behavior_card_top(const App& app);
int repeat_card_top(const App& app);
int language_card_top(const App& app);
int language_card_h();
int content_bottom(const App& app);

// Repeat-slider track shared by paint, pointer-down and pointer-motion so
// drag values always agree with tap values.
void repeat_slider_track_geom(int contentX, int contentW, int* trX, int* trW);
}
