#pragma once

#include <cairo/cairo.h>

struct App;

static constexpr int kWiredChildCount = 6;
static constexpr int kWiredChildStatus   = 0;
static constexpr int kWiredChildIPv4     = 1;
static constexpr int kWiredChildIPv6     = 2;
static constexpr int kWiredChildEthernet = 3;
static constexpr int kWiredChildSecurity = 4;
static constexpr int kWiredChildAdvanced = 5;

static constexpr int kNetworkWiredTop = 122; // kContentTop + kDockChildTabH + 12

extern const char* kWiredChildLabels[6];

void paint_wired_child_tab_bar(cairo_t* cr, int contentX, int contentW,
                                float textR, float textG, float textB,
                                int activeTab);
int wired_hit_child_tab(float px, float py, int contentX, int contentW);
