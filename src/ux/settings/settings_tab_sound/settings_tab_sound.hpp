#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <string>
#include <vector>

struct App;

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
  SoundSectionGeom bluetooth{};
  SoundSectionGeom engine{};
  SoundSectionGeom playback{};
  SoundSectionGeom recording{};

  int n_bt_cards = 0;
  int bottom_y = 0;
};

struct SoundBtPopupGeom {
  int x;
  int w;
};

void paint_sound_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, double paintPointerYOffset);
bool settings_sound_consume_pointer_down(App& app, int contentX, int contentW);

int sound_tab_bt_count_clamped(App& app);
SoundTabGeom sound_compute_tab_geom(int content_x, int content_w, int content_top, int viewport_h,
                                    int n_play, int n_rec, int n_bt_cards);
bool sound_dd_combo_geom(const SoundTabGeom& g, int sound_active_dd, int n_play, int n_rec,
                         int* cx, int* cy, int* cw, int* ch);
int sound_dd_popup_list_doc_top_y(int combo_doc_y, int combo_h, int nrows, int scroll_px, int window_h);
SoundBtPopupGeom sound_bt_dropdown_popup_geom(const App& app, int tcx, int tcw, int dcx, int combo_w,
                                              const std::vector<std::string>& labels);
void sound_stream_slider_geom_content(int sec_content_y0, int sec_x, int sec_w, int stream_ix,
                                      int* trX, int* trY, int* trW);
void settings_clamp_sound_scroll_px(App& app, int n_play, int n_rec, int n_bt_cards);

inline constexpr int kSoundMaxBtCards = 4;
inline constexpr int kSoundBtDdBase = 600;
inline constexpr int kSoundEngineRates[] = {44100, 48000, 88200, 96000, 176400, 192000};
inline constexpr int kSoundCompatPcmChoiceCount = 4;
inline constexpr const char* kSoundCompatPcmLabels[] = {"16-bit PCM (S16 LE)", "24-bit PCM (S24 LE)", "32-bit PCM (S32 LE)",
                                                         "32-bit float (F32 LE)"};
inline constexpr int kSoundEngineRateCount = static_cast<int>(sizeof(kSoundEngineRates) / sizeof(kSoundEngineRates[0]));
