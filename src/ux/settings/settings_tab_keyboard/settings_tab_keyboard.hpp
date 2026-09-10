#pragma once
#include <cairo/cairo.h>
struct App;
void paint_keyboard_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_keyboard_consume_pointer_down(App& app, int contentX, int contentW);

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
static constexpr int kLayoutCardH = 52 + 5 * 68 + 24 + 12;
static constexpr int kBehaviorCardH = 52 + 3 * 68 + 24 + 12;
static constexpr int kRepeatCardH = 52 + 2 * 68 + 24;
static constexpr int kCardGap = 12;
static constexpr int kBehaviorCardTop = kCardTop + kLayoutCardH + kCardGap;
static constexpr int kRepeatCardTop = kBehaviorCardTop + kBehaviorCardH + kCardGap;
static constexpr int kComboW = 300;
static constexpr int kComboH = 28;
}
