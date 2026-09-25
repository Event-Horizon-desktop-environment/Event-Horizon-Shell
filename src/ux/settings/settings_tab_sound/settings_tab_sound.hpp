#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <string>
#include <vector>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"

struct App;

namespace eh::audio {
struct AudioCardProfiles;
}

// The Sound sidebar tab (id 8) is an "Audio Center" with its own child tabs,
// modelled on the wired/launcher child-tab-bar pattern.
enum SoundChildTab {
  kSoundChildDevices = 0,
  kSoundChildBluetooth = 1,
  kSoundChildApps = 2,
  kSoundChildEngine = 3,
};
inline constexpr int kSoundChildCount = 4;
// Scrollable content starts below the child-tab bar.
inline constexpr int kSoundChildContentTop = kContentTop + kDockChildTabH + 12;

struct SoundSectionGeom {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int content_y0 = 0;
};

struct SoundTabGeom {
  SoundSectionGeom header{};
  SoundSectionGeom devices{};
  SoundSectionGeom cards{};
  SoundSectionGeom engine{};
  SoundSectionGeom playback{};
  SoundSectionGeom recording{};

  int n_cards = 0;
  int bottom_y = 0;
};

struct SoundBtPopupGeom {
  int x;
  int w;
};

void paint_sound_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, double paintPointerYOffset);
bool settings_sound_consume_pointer_down(App& app, int contentX, int contentW);

// Card list for the active child tab: Bluetooth cards on the Bluetooth page,
// all non-Bluetooth cards on the Devices page. Cached; refreshed in the
// background by settings_sound_cards_cached()/..._nonbt_cards_cached().
const std::vector<eh::audio::AudioCardProfiles>& sound_tab_cards_cached(App& app);
int sound_tab_card_count_clamped(App& app);
// Geometry for a single Sound child tab page (header card + the sections that
// belong to that child only). Scroll max, painting, dropdowns and hit-testing
// all go through this so every page is laid out independently.
SoundTabGeom sound_compute_child_geom(int content_x, int content_w, int content_top, int child,
                                      int n_play, int n_rec, int n_cards);
bool sound_dd_combo_geom(const SoundTabGeom& g, int sound_active_dd, int n_play, int n_rec,
                         int* cx, int* cy, int* cw, int* ch);
int sound_dd_popup_list_doc_top_y(int combo_doc_y, int combo_h, int nrows, int scroll_px, int window_h);
SoundBtPopupGeom sound_bt_dropdown_popup_geom(const App& app, int tcx, int tcw, int dcx, int combo_w,
                                              const std::vector<std::string>& labels);
void sound_stream_slider_geom_content(int sec_content_y0, int sec_x, int sec_w, int stream_ix,
                                      int* trX, int* trY, int* trW);
void settings_clamp_sound_scroll_px(App& app, int n_play, int n_rec, int n_cards);

inline constexpr int kSoundMaxCards = 12;
inline constexpr int kSoundCardDdBase = 600;
inline constexpr int kSoundEngineRates[] = {44100, 48000, 88200, 96000, 176400, 192000};
inline constexpr int kSoundEngineQuantums[] = {128, 256, 512, 1024, 2048, 4096, 8192};
inline constexpr int kSoundCompatPcmChoiceCount = 4;
inline constexpr const char* kSoundCompatPcmLabels[] = {"16-bit PCM (S16 LE)", "24-bit PCM (S24 LE)", "32-bit PCM (S32 LE)",
                                                         "32-bit float (F32 LE)"};
inline constexpr int kSoundEngineRateCount = static_cast<int>(sizeof(kSoundEngineRates) / sizeof(kSoundEngineRates[0]));
inline constexpr int kSoundEngineQuantumCount =
    static_cast<int>(sizeof(kSoundEngineQuantums) / sizeof(kSoundEngineQuantums[0]));
