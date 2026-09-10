#pragma once

#include <cstdint>
#include <string>

namespace eh::shell::desktop {

struct DesktopApp;
enum class DesktopIconArrangement : uint8_t { AutoArrange, AlignToGrid, Stacked, StackedRight };
struct DesktopIconItem;
enum class DesktopIconSort : uint8_t { Name, Type };
enum class DesktopIconSizeKind : uint8_t { Small, Medium, Large };

struct IconLayoutMetrics {
  double icon_sz;
  double cell_w;
  double cell_h;
};

struct DesktopIconPrefs {
  int col;
  int row;
  int layer;
};

IconLayoutMetrics desktop_icon_metrics_for(DesktopIconSizeKind k);
void desktop_prefs_ensure_loaded(DesktopApp& app);
void desktop_prefs_save(DesktopApp& app);
void desktop_prefs_set_arrangement(DesktopApp& app, DesktopIconArrangement a);
void desktop_prefs_set_sort_mode(DesktopApp& app, DesktopIconSort s);
void desktop_prefs_set_icon_size(DesktopApp& app, DesktopIconSizeKind k);
void desktop_prefs_set_icons_visible(DesktopApp& app, bool visible);

}
