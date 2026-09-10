#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_sound/settings_tab_sound.hpp"
#include "ux/settings/settings_serialize.hpp"
#include "ux/settings/utils/monitors/settings_monitors_tab.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/controlcenter/mixer/mixer_stream_icon_resolve.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "services/audio/pipewire_service.hpp"

// extern declarations for functions remaining in settings_app.cpp
extern void draw(App& app);
extern void settings_close_non_default_app_dropdowns(App& app);

#include "ux/settings/utils/sound/settings_sound_cache.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"

// Monitors constants (copied from settings_monitors_ui.inl)
static constexpr int kMonFormRowPitch = 56;
static constexpr int kMonHeaderCardH = 52;
static constexpr int kMonSecGap = 32;
static constexpr int kMonSecTitlePadTop = 20;
static constexpr int kMonSecTitleLineH = 22;
static constexpr int kMonSecAfterTitle = 20;
static constexpr int kMonSecBottomPad = 24;
static constexpr int kMonFormLabelColW = 200;

// Sound-specific layout constants
static constexpr int kSndGridRowH = 82;
static constexpr int kSndGridLabelH = 18;
static constexpr int kSndGridLabelGap = 12;
static constexpr int kSndSliderIconW = 30;
static constexpr int kSndAppIconSize = 36;
static constexpr int kSndAppIconRad = 10;

// File-static monitors helpers (copied from settings_monitors_ui.inl)
static int monitors_section_card_height_rows(int n_rows) {
  return kMonSecTitlePadTop + kMonSecTitleLineH + kMonSecAfterTitle + n_rows * kMonFormRowPitch + kMonSecBottomPad;
}

static int snd_engine_card_height() {
  return kMonSecTitlePadTop + kMonSecTitleLineH + kMonSecAfterTitle + 2 * kSndGridRowH + kMonSecBottomPad;
}

static int monitors_section_content_y0(int card_y) {
  return card_y + kMonSecTitlePadTop + kMonSecTitleLineH + kMonSecAfterTitle;
}

static void monitors_combo_geom_at_content_row(int content_y0, int cardX, int cardW, int rowIx, int* cx, int* cy,
                                               int* cw, int* ch) {
  
  const int y = content_y0 + rowIx * kMonFormRowPitch;
  *cx = cardX + cardW - kCardPad - kSettingsComboW;
  *cy = y;
  *cw = kSettingsComboW;
  *ch = kSettingsComboH;
}

// Engine 2-column grid geometry helpers
static void snd_engine_grid_geom(const SoundSectionGeom& sec, int* colW, int* col1X, int* col2X) {
  const int innerW = sec.w - 2 * kCardPad;
  *colW = (innerW - kSpacingL) / 2;
  *col1X = sec.x + kCardPad;
  *col2X = *col1X + *colW + kSpacingL;
}

static void snd_engine_select_geom(const SoundSectionGeom& sec, int colIx, int rowIx,
                                    int* sx, int* sy, int* sw, int* sh) {
  int colW, col1X, col2X;
  snd_engine_grid_geom(sec, &colW, &col1X, &col2X);
  const int colX = (colIx == 0) ? col1X : col2X;
  const int gridRowY = sec.content_y0 + rowIx * kSndGridRowH;
  *sx = colX;
  *sy = gridRowY + kSndGridLabelH + kSndGridLabelGap;
  *sw = colW;
  *sh = kSettingsComboH;
}

static int snd_engine_label_y(const SoundSectionGeom& sec, int rowIx) {
  return sec.content_y0 + rowIx * kSndGridRowH;
}

// Char-count estimation of text-sized combo geom (no Pango/cairo needed).
static void snd_combo_text_geom(int content_y0, int cardX, int cardW, int rowIx,
                                const char* value_text, int* cx, int* cy, int* cw, int* ch) {
  monitors_combo_geom_at_content_row(content_y0, cardX, cardW, rowIx, cx, cy, cw, ch);
  if (!value_text || !value_text[0]) return;
  const int estPx = static_cast<int>(std::strlen(value_text)) * 8 + 4;
  const int desiredW = estPx + 48;
  const int rightEdge = cardX + cardW - kCardPad;
  const int maxW = std::max(kSettingsComboW, rightEdge - (cardX + kCardPad + kSpacingM));
  *cw = std::clamp(desiredW, kSettingsComboW, maxW);
  *cx = rightEdge - *cw;
}

static void monitors_form_slider_track_geom_content(int content_y0, int cardX, int cardW, int rowIx, int* trX,
                                                    int* trY, int* trW) {
   
  constexpr int kValPx = 56;
  const int right = cardX + cardW - kCardPad;
  const int minX = cardX + kMonFormLabelColW + 12;
  *trW = std::max(96, right - kValPx - minX);
  *trX = right - kValPx - *trW;
  *trY = content_y0 + rowIx * kMonFormRowPitch + 14;
}

// File-static copy of settings_utf8_prev_cp_start (used by settings_draw_text_line_fit_width)
static size_t settings_utf8_prev_cp_start(const std::string& s, size_t byte_end) {
   
  if (byte_end == 0) return 0;
  size_t i = byte_end - 1;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  return i;
}

// File-static copy of settings_draw_text_line_fit_width (copied from settings_app.cpp)
// Uses Pango for both measurement and rendering to avoid width mismatch.
static void settings_draw_text_line_fit_width(cairo_t* cr, const std::string& text, double x, double baselineY,
                                              int max_width_px, double fadeAlpha) {
  
  if (max_width_px <= 10 || text.empty()) return;
  auto measure = [&](const std::string& s) -> int {
    auto* layout = pango_cairo_create_layout(cr);
    auto* desc = pango_font_description_new();
    pango_font_description_set_family(desc, "Inter");
    pango_font_description_set_size(desc, static_cast<int>(12.0f * PANGO_SCALE));
    pango_font_description_set_weight(desc, static_cast<PangoWeight>(400));
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, s.c_str(), -1);
    int pw = 0, ph = 0;
    pango_layout_get_pixel_size(layout, &pw, &ph);
    pango_font_description_free(desc);
    g_object_unref(layout);
    return pw;
  };
  if (measure(text) <= max_width_px) {
    settings_show_text(cr, x, baselineY, text.c_str(), 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, fadeAlpha);
    return;
  }
  static const char kEll[] = "\xe2\x80\xa6";
  const int ell_w = measure(kEll);
  const int budget = max_width_px - ell_w;
  if (budget <= 1) {
    settings_show_text(cr, x, baselineY, kEll, 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, fadeAlpha);
    return;
  }
  size_t end = text.size();
  while (end > 0) {
    std::string trial = text.substr(0, end);
    if (measure(trial) <= budget) {
      int tw = 0, th = 0;
      {
        auto* layout = pango_cairo_create_layout(cr);
        auto* desc = pango_font_description_new();
        pango_font_description_set_family(desc, "Inter");
        pango_font_description_set_size(desc, static_cast<int>(12.0f * PANGO_SCALE));
        pango_font_description_set_weight(desc, static_cast<PangoWeight>(400));
        pango_layout_set_font_description(layout, desc);
        pango_layout_set_text(layout, trial.c_str(), -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        pango_font_description_free(desc);
        g_object_unref(layout);
      }
      settings_show_text(cr, x, baselineY, trial.c_str(), 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, fadeAlpha);
      settings_show_text(cr, x + static_cast<double>(tw), baselineY, kEll, 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, fadeAlpha);
      return;
    }
    end = settings_utf8_prev_cp_start(text, end);
  }
  settings_show_text(cr, x, baselineY, kEll, 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, fadeAlpha);
}

// Sound helpers (from settings_sound_ui.inl)
int sound_tab_bt_count_clamped(App& app) {
  const auto& v = settings_sound_bt_cards_cached(app);
  return static_cast<int>(std::min(v.size(), static_cast<size_t>(kSoundMaxBtCards)));
}

static std::string sound_engine_hz_display(int hz) {
   
  if (hz <= 0) return std::string("\xe2\x80\x94");
  if (hz == 44100) return "44.1 kHz";
  if (hz == 88200) return "88.2 kHz";
  if (hz == 176400) return "176.4 kHz";
  if (hz % 1000 == 0) return std::to_string(hz / 1000) + " kHz";
  return std::to_string(hz) + " Hz";
}

static int sound_engine_allowed_preset_sel(const eh::audio::EngineSettings& es) {
   
  auto norm = es.allowed_rates_hz;
  std::sort(norm.begin(), norm.end());
  static const int hi[] = {44100, 48000, 88200, 96000, 176400, 192000};
  if (norm == std::vector<int>(hi, hi + 6)) return 0;
  static const int co[] = {44100, 48000};
  if (norm == std::vector<int>(co, co + 2)) return 1;
  static const int pr[] = {48000, 96000, 192000};
  if (norm == std::vector<int>(pr, pr + 3)) return 2;
  std::vector<int> single = {es.clock_rate_hz};
  std::sort(single.begin(), single.end());
  if (norm.size() == 1 && norm == single) return 3;
  return -1;
}

static std::string sound_engine_allowed_combo_display(const eh::audio::EngineSettings& es) {
   
  const int m = sound_engine_allowed_preset_sel(es);
  if (m == 0) return "Hi-Fi (44.1-192 kHz)";
  if (m == 1) return "Consumer (44.1 / 48 kHz)";
  if (m == 2) return "Pro (48 / 96 / 192 kHz)";
  if (m == 3) return "Single (default rate only)";
  return "Custom";
}

static std::string sound_compat_pcm_combo_display(const eh::audio::CompatDefaultSinkFormat& cf) {
   
  if (!cf.available) return std::string("(pactl unavailable)");
  if (!cf.sample_spec.empty()) return cf.sample_spec;
  return cf.format_encoding.empty() ? std::string("(unknown)") : cf.format_encoding;
}

static int sound_compat_pcm_dropdown_sel_ix(const eh::audio::CompatDefaultSinkFormat& cf) {
   
  if (!cf.available) return -1;
  std::string enc = cf.format_encoding;
  for (char& c : enc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (enc == "s16le") return 0;
  if (enc == "s24le" || enc == "s24_32le" || enc == "s2432le") return 1;
  if (enc == "s32le") return 2;
  if (enc == "f32le" || enc == "float32le") return 3;
  return -1;
}

SoundTabGeom sound_compute_tab_geom(int content_x, int content_w, int content_top, int /*viewport_h*/,
                                           int n_play, int n_rec, int n_bt_cards) {
   
  SoundTabGeom g{};
  const int w = eh::settings_monitors_tab::monitors_main_column_width_px(content_w);
  const int x = eh::settings_monitors_tab::monitors_main_column_left_px(content_x, content_w, w);
  int y = content_top + 8;

  g.header.x = x;
  g.header.y = y;
  g.header.w = w;
  g.header.h = 72;
  g.header.content_y0 = y + 18;
  y += kMonHeaderCardH + kMonSecGap;

  const int dh = monitors_section_card_height_rows(4);
  g.devices = {x, y, w, dh, monitors_section_content_y0(y)};
  y += dh + kMonSecGap;

  g.n_bt_cards = std::clamp(n_bt_cards, 0, kSoundMaxBtCards);
  if (g.n_bt_cards > 0) {
    const int bh = monitors_section_card_height_rows(g.n_bt_cards);
    g.bluetooth = {x, y, w, bh, monitors_section_content_y0(y)};
    y += bh + kMonSecGap;
  } else {
    g.bluetooth = {x, y, w, 0, monitors_section_content_y0(y)};
  }

  const int eh = snd_engine_card_height();
  g.engine = {x, y, w, eh, monitors_section_content_y0(y)};
  y += eh + kMonSecGap;

  const int pb_rows = (n_play <= 0) ? 1 : (n_play * 2);
  const int pbh = monitors_section_card_height_rows(pb_rows);
  g.playback = {x, y, w, pbh, monitors_section_content_y0(y)};
  y += pbh + kMonSecGap;

  const int rec_rows = (n_rec <= 0) ? 1 : (n_rec * 2);
  const int rech = monitors_section_card_height_rows(rec_rows);
  g.recording = {x, y, w, rech, monitors_section_content_y0(y)};
  y += rech;

  g.bottom_y = y + 24;
  return g;
}

static int settings_sound_scroll_max_px(App& app, int n_play, int n_rec, int n_bt_cards) {
   
  int tcx = 0;
  int tcw = 0;
  settings_content_column_geom(app, &tcx, &tcw);
  const SoundTabGeom g =
      sound_compute_tab_geom(tcx, tcw, kContentTop, settings_content_viewport_h(app), n_play, n_rec, n_bt_cards);
  const int view_h = app.height - kContentTop - kSpacingL;
  return std::max(0, g.bottom_y - kContentTop - view_h);
}

void settings_clamp_sound_scroll_px(App& app, int n_play, int n_rec, int n_bt_cards) {
  app.settingsSoundScrollPx =
      std::clamp(app.settingsSoundScrollPx, 0, settings_sound_scroll_max_px(app, n_play, n_rec, n_bt_cards));
}

int sound_dd_popup_list_doc_top_y(int combo_doc_y, int combo_h, int nrows, int scroll_px, int window_h) {
   
  if (nrows <= 0) return combo_doc_y + combo_h + 2;
  const int default_top = combo_doc_y + combo_h + 2;
  const int popup_h = nrows * kSettingsDdRowH;
  const int viewport_bottom_doc = window_h - kSpacingL + scroll_px;
  if (default_top + popup_h <= viewport_bottom_doc) return default_top;
  const int above_top = combo_doc_y - popup_h - 2;
  if (above_top >= kContentTop) return above_top;
  return default_top;
}

bool sound_dd_combo_geom(const SoundTabGeom& g, int sound_active_dd, int n_play, int n_rec, int* cx, int* cy,
                                int* cw, int* ch) {
   
  if (sound_active_dd < 0) return false;
  if (sound_active_dd == 0) {
    monitors_combo_geom_at_content_row(g.devices.content_y0, g.devices.x, g.devices.w, 0, cx, cy, cw, ch);
    return true;
  }
  if (sound_active_dd == 1) {
    monitors_combo_geom_at_content_row(g.devices.content_y0, g.devices.x, g.devices.w, 2, cx, cy, cw, ch);
    return true;
  }
  if (sound_active_dd >= kSoundBtDdBase && sound_active_dd < kSoundBtDdBase + kSoundMaxBtCards) {
    const int bi = sound_active_dd - kSoundBtDdBase;
    if (bi < 0 || bi >= g.n_bt_cards) return false;
    monitors_combo_geom_at_content_row(g.bluetooth.content_y0, g.bluetooth.x, g.bluetooth.w, bi, cx, cy, cw, ch);
    return true;
  }
  if (sound_active_dd == 2) {
    snd_engine_select_geom(g.engine, 0, 0, cx, cy, cw, ch);
    return true;
  }
  if (sound_active_dd == 3) {
    snd_engine_select_geom(g.engine, 1, 0, cx, cy, cw, ch);
    return true;
  }
  if (sound_active_dd == 4) {
    snd_engine_select_geom(g.engine, 0, 1, cx, cy, cw, ch);
    return true;
  }
  if (sound_active_dd == 5) {
    snd_engine_select_geom(g.engine, 1, 1, cx, cy, cw, ch);
    return true;
  }
  if (sound_active_dd >= 100 && sound_active_dd < 1000) {
    const int i = sound_active_dd - 100;
    if (n_play <= 0 || i < 0 || i >= n_play) return false;
    monitors_combo_geom_at_content_row(g.playback.content_y0, g.playback.x, g.playback.w, i * 2, cx, cy, cw, ch);
    return true;
  }
  if (sound_active_dd >= 1000) {
    const int i = sound_active_dd - 1000;
    if (n_rec <= 0 || i < 0 || i >= n_rec) return false;
    monitors_combo_geom_at_content_row(g.recording.content_y0, g.recording.x, g.recording.w, i * 2, cx, cy, cw, ch);
    return true;
  }
  return false;
}

void sound_stream_slider_geom_content(int sec_content_y0, int sec_x, int sec_w, int stream_ix, int* trX,
                                             int* trY, int* trW) {
   
  monitors_form_slider_track_geom_content(sec_content_y0, sec_x, sec_w, stream_ix * 2 + 1, trX, trY, trW);
}

SoundBtPopupGeom sound_bt_dropdown_popup_geom(const App& app, int tcx, int tcw, int dcx, int combo_w,
                                                     const std::vector<std::string>& labels) {
   
  SoundBtPopupGeom g{dcx, combo_w};
  (void)tcx;
  (void)tcw;
  if (labels.empty()) return g;
  constexpr int kMargin = 12;
  constexpr int kPopupTextHPadding = 28;
  const int text_px = settings_measure_max_text_advance_px_sans12(labels);
  const int desired = text_px + kPopupTextHPadding;
  const int screen_budget = std::max(combo_w, app.width - 2 * kMargin);
  g.w = std::max(combo_w, std::min(desired, screen_budget));
  g.x = dcx;
  if (g.x + g.w > app.width - kMargin) g.x = app.width - kMargin - g.w;
  if (g.x < kMargin) {
    g.x = kMargin;
    g.w = std::max(combo_w, std::min(g.w, app.width - 2 * kMargin));
  }
  return g;
}

// Slider wrapper with dimmed accent color (less harsh than full accent)
static void sound_settings_slider(App& app, cairo_t* cr, int trX, int trY, int trW,
                                   int v, int vmin, int vmax, double paintPointerYOffset,
                                   const char* val_override = nullptr,
                                   bool inactive = false, double drag_track_norm = -1.0) {
  static_cast<void>(inactive);
  m3::Slider sl;
  sl.setRange(static_cast<float>(vmin), static_cast<float>(vmax));
  const bool use_drag = drag_track_norm >= 0.0;
  if (use_drag) {
    const float dv = static_cast<float>(vmax - vmin);
    sl.setValue(static_cast<float>(vmin) + static_cast<float>(drag_track_norm) * dv);
  } else {
    sl.setValue(static_cast<float>(v));
  }
  sl.setStep(0.0f);
  sl.setGeometry(static_cast<float>(trX), static_cast<float>(trY) - 10.0f, static_cast<float>(trW), 36.0f);
  sl.setEnabled(true);
  const double py = app.pointerY + paintPointerYOffset;
  const bool hov = app.pointerX >= trX - 6 && app.pointerX < trX + trW + 6 &&
                   py >= trY - 10 && py < trY + 36;
  sl.setHovered(hov);

  std::array<char, 32> vbuf;
  const char* disp = val_override;
  if (!disp) {
    std::snprintf(vbuf.data(), vbuf.size(), "%d px", v);
    disp = vbuf.data();
  }
  sl.setShowValueLabel(true);
  sl.setValueLabel(disp);
  if (use_drag) sl.setPressed(true);

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  const float dim = 0.50f;
  a_r = s_r + (a_r - s_r) * dim;
  a_g = s_g + (a_g - s_g) * dim;
  a_b = s_b + (a_b - s_b) * dim;
  sl.setAccentColor(a_r, a_g, a_b);
  sl.setSurfaceColor(s_r, s_g, s_b);
  sl.setTextColor(t_r, t_g, t_b);
  sl.paint(cr);
  static_cast<void>(o_r);
  static_cast<void>(o_g);
  static_cast<void>(o_b);
}

// Forward declaration for settings_paint_sound_dropdown_unclipped
static void settings_paint_sound_dropdown_unclipped(App& app, cairo_t* cr, int /*contentX*/, int /*contentW*/,
                                                     double glassOv, const eh::audio::Snapshot& snap);

// paint_sound_tab function (content of settings_sound_tab_body.inl)
void paint_sound_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, double paintPointerYOffset) {
   
    eh::audio::PipeWireService::instance().start();

    static bool audio_defaults_applied = false;
    if (!audio_defaults_applied) {
      audio_defaults_applied = true;
      const auto& snk = app.settings.audioDefaultSinkName;
      const auto& src = app.settings.audioDefaultSourceName;
      if (!snk.empty() || !src.empty()) {
        eh::audio::PipeWireService::SavedDefaults d;
        d.default_sink_name = snk;
        d.default_source_name = src;
        d.default_sink_volume_pct = app.settings.audioDefaultSinkVolumePct;
        d.default_sink_muted = app.settings.audioDefaultSinkMuted;
        d.default_source_volume_pct = app.settings.audioDefaultSourceVolumePct;
        d.default_source_muted = app.settings.audioDefaultSourceMuted;
        d.engine_clock_rate_hz = app.settings.audioEngineClockRateHz;
        d.engine_force_rate_hz = app.settings.audioEngineForceRateHz;
        d.engine_allowed_rates_hz = app.settings.audioEngineAllowedRatesHz;
        d.compat_pcm_format = app.settings.audioCompatPcmFormat;
        eh::audio::PipeWireService::instance().apply_saved_defaults(d);
      }
    }

    eh::audio::Snapshot snap;
    if (app.soundPaintSnapPending) {
      snap = std::move(*app.soundPaintSnapPending);
      app.soundPaintSnapPending.reset();
    } else {
      snap = eh::audio::PipeWireService::instance().snapshot();
    }
    const int n_play = static_cast<int>(snap.output_streams.size());
    const int n_rec = static_cast<int>(snap.input_streams.size());
    const auto& bt_cards_full = settings_sound_bt_cards_cached(app);
    const int n_bt =
        static_cast<int>(std::min(bt_cards_full.size(), static_cast<size_t>(kSoundMaxBtCards)));
    settings_clamp_sound_scroll_px(app, n_play, n_rec, n_bt);

    std::string pw_link_list_blob;
    if (n_play > 0 || n_rec > 0) (void)eh::audio::pw_link_list_capture_stdout(&pw_link_list_blob);

    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    const double pyH = app.pointerY + paintPointerYOffset;

    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const SoundTabGeom sg =
        sound_compute_tab_geom(tcx, tcw, kContentTop, settings_content_viewport_h(app), n_play, n_rec, n_bt);

    auto fill_snd_card = [&](const SoundSectionGeom& sec) {
      {
        m3::Box box;
        float r, g, b;
        if (app.drawChromeMatugen) {
          r = app.drawChrome.panelFillR;
          g = app.drawChrome.panelFillG;
          b = app.drawChrome.panelFillB;
        } else {
          r = static_cast<float>(Theme::BgR);
          g = static_cast<float>(Theme::BgG);
          b = static_cast<float>(Theme::BgB);
        }
        box.setColor(r, g, b, static_cast<float>(0.82 * glassOv));
        box.setRadius(14.0f);
        box.setGeometry(static_cast<float>(sec.x), static_cast<float>(sec.y),
                        static_cast<float>(sec.w), static_cast<float>(sec.h));
        box.setGlassy(true);
        box.paint(cr);
      }
      cairo_round_rect(cr, static_cast<double>(sec.x), static_cast<double>(sec.y), static_cast<double>(sec.w),
                       static_cast<double>(sec.h), 14.0);
      paint_src_glass_hi(app, cr, 0.10 * glassOv);
      cairo_set_line_width(cr, 1.2);
      cairo_stroke(cr);
    };

    int headingPh = 0, namePh = 0, descPh = 0;
    {
      auto* l = pango_cairo_create_layout(cr);
      auto* d = pango_font_description_new();
      pango_font_description_set_family(d, "Inter");
      pango_font_description_set_size(d, static_cast<int>(15.0f * PANGO_SCALE));
      pango_font_description_set_weight(d, static_cast<PangoWeight>(700));
      pango_layout_set_font_description(l, d);
      pango_layout_set_text(l, "Hg", -1);
      pango_layout_get_pixel_size(l, nullptr, &headingPh);
      pango_font_description_set_size(d, static_cast<int>(13.0f * PANGO_SCALE));
      pango_font_description_set_weight(d, static_cast<PangoWeight>(500));
      pango_layout_set_text(l, "Hg", -1);
      pango_layout_get_pixel_size(l, nullptr, &namePh);
      pango_font_description_set_size(d, static_cast<int>(11.0f * PANGO_SCALE));
      pango_font_description_set_weight(d, static_cast<PangoWeight>(400));
      pango_layout_set_text(l, "Hg", -1);
      pango_layout_get_pixel_size(l, nullptr, &descPh);
      pango_font_description_free(d);
      g_object_unref(l);
    }
    auto paint_snd_heading = [&](const SoundSectionGeom& sec, const char* title, const char* material_icon) {
      const double iconPx = 24.0;
      const double iconCenterY = static_cast<double>(sec.y + kMonSecTitlePadTop + 12);
      const double ix = static_cast<double>(sec.x + kCardPad) + iconPx * 0.5;
      material_symbols_draw_glyph(cr, ix, iconCenterY, iconPx, material_icon, Theme::AccR, Theme::AccG, Theme::AccB,
                                  0.88 * glassOv);
      const double textY = iconCenterY + static_cast<double>(headingPh) * 0.5 - 2.0;
      settings_show_text(cr, static_cast<double>(sec.x + kCardPad) + iconPx + 10.0,
                    textY, title, 15, 700, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    };

    fill_snd_card(sg.header);
    {
      const double iconPx = 28.0;
      const double iconCenterY = static_cast<double>(sg.header.y) + static_cast<double>(sg.header.h) * 0.5 - 5.0;
      const double ix = static_cast<double>(sg.header.x + kCardPad) + iconPx * 0.5;
      material_symbols_draw_glyph(cr, ix, iconCenterY, iconPx, "volume_up",
                                  Theme::AccR, Theme::AccG, Theme::AccB, 1.0 * glassOv);
      const double titleX = static_cast<double>(sg.header.x + kCardPad) + iconPx + 10.0;
      const double textY = iconCenterY + static_cast<double>(headingPh) * 0.5 - 2.0;
      settings_show_text(cr, titleX, textY, "Sound", 15, 700,
                         Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
      const double subY = textY + static_cast<double>(descPh);
      settings_show_text(cr, titleX, subY, "Default devices and per-application mixer (PipeWire).", 11, 400,
                         Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    }

    auto measure_text_px = [&](const char* t) -> int {
      if (!t || !t[0]) return 0;
      auto* layout = pango_cairo_create_layout(cr);
      auto* desc = pango_font_description_new();
      pango_font_description_set_family(desc, "Inter");
      pango_font_description_set_size(desc, static_cast<int>(12.0f * PANGO_SCALE));
      pango_font_description_set_weight(desc, static_cast<PangoWeight>(400));
      pango_layout_set_font_description(layout, desc);
      pango_layout_set_text(layout, t, -1);
      int tw = 0, th = 0;
      pango_layout_get_pixel_size(layout, &tw, &th);
      pango_font_description_free(desc);
      g_object_unref(layout);
      return tw;
    };
    auto snd_combo_geom = [&](const SoundSectionGeom& sec, int row_ix, const char* value_text,
                              int& cx, int& cy, int& cw, int& ch) {
      monitors_combo_geom_at_content_row(sec.content_y0, sec.x, sec.w, row_ix, &cx, &cy, &cw, &ch);
      const int textW = measure_text_px(value_text);
      const int desiredW = textW + 48;
      const int rightEdge = sec.x + sec.w - kCardPad;
      const int maxW = std::max(kSettingsComboW, rightEdge - (sec.x + kCardPad + kSpacingM));
      cw = std::clamp(desiredW, kSettingsComboW, maxW);
      cx = rightEdge - cw;
    };
    auto paint_snd_combo_row = [&](const SoundSectionGeom& sec, int row_ix, const char* label, const char* value_text,
                                   bool expanded) {
      const int labY = sec.content_y0 + row_ix * kMonFormRowPitch;
      settings_draw_trimmed_text_line(cr, std::string(label), sec.x + kCardPad, static_cast<double>(labY + 19), 22,
                                      1.0, 12.f, 400);
      int cx = 0;
      int cy = 0;
      int cw = 0;
      int ch = 0;
      snd_combo_geom(sec, row_ix, value_text, cx, cy, cw, ch);
      const bool hovered = app.pointerX >= cx && pyH >= cy && app.pointerX < cx + cw && pyH < cy + ch;
      {
        m3::Box box;
        float r, g, b;
        if (app.drawChromeMatugen) {
          r = app.drawChrome.panelFillR;
          g = app.drawChrome.panelFillG;
          b = app.drawChrome.panelFillB;
        } else {
          r = static_cast<float>(Theme::BgR);
          g = static_cast<float>(Theme::BgG);
          b = static_cast<float>(Theme::BgB);
        }
        box.setColor(r, g, b, static_cast<float>((hovered ? 0.94 : 0.88) * glassOv));
        box.setRadius(9.0f);
        box.setGeometry(static_cast<float>(cx), static_cast<float>(cy),
                        static_cast<float>(cw), static_cast<float>(ch));
        box.setGlassy(true);
        box.paint(cr);
      }
      cairo_round_rect(cr, cx, cy, cw, ch, 9.0);
      paint_src_glass_hi(app, cr, 0.11 * glassOv);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      settings_draw_text_line_fit_width(cr, std::string(value_text), cx + 12.0, cy + 19.0, cw - 40,
                                        1.0);
      {
        const double chevCy = static_cast<double>(cy) + static_cast<double>(ch) * 0.52;
        const double chevAlpha = (hovered ? 0.95 : 0.55) * glassOv;
        material_symbols_draw_glyph(cr, static_cast<double>(cx + cw) - 14.0, chevCy, 18.0,
                                    expanded ? "expand_less" : "expand_more", Theme::TextR, Theme::TextG, Theme::TextB,
                                    chevAlpha);
      }
    };

    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

    fill_snd_card(sg.devices);
    paint_snd_heading(sg.devices, "Devices", "devices");
    {
      // Default output.
      const eh::audio::Device* def_sink = nullptr;
      for (const auto& d : snap.sinks) {
        if (d.node_id == snap.default_sink) def_sink = &d;
      }
      const std::string sink_disp =
          def_sink ? (def_sink->display_name.empty() ? def_sink->name : def_sink->display_name) : std::string("(none)");

      {
        const double iconPx = 16.0;
        const double ix = static_cast<double>(sg.devices.x + kCardPad) + iconPx * 0.5;
        const double iconCy = static_cast<double>(sg.devices.content_y0 + 14);
        material_symbols_draw_glyph(cr, ix, iconCy, iconPx, "volume_up", Theme::AccR, Theme::AccG, Theme::AccB, 0.88 * glassOv);
        settings_show_text(cr, static_cast<double>(sg.devices.x + kCardPad) + iconPx + 8.0,
                      iconCy + static_cast<double>(namePh) * 0.5 - 2.0, "Default output", 13, 600, Theme::TextR, Theme::TextG, Theme::TextB, 0.95 * glassOv);
      }
      int cx = 0, cy = 0, cw = 0, ch = 0;
      snd_combo_geom(sg.devices, 0, sink_disp.c_str(), cx, cy, cw, ch);
      {
        const bool expanded = (app.soundActiveDd == 0);
        const bool hovered = app.pointerX >= cx && pyH >= cy && app.pointerX < cx + cw && pyH < cy + ch;
        {
          m3::Box box;
          float r, g, b;
          if (app.drawChromeMatugen) {
            r = app.drawChrome.panelFillR;
            g = app.drawChrome.panelFillG;
            b = app.drawChrome.panelFillB;
          } else {
            r = static_cast<float>(Theme::BgR);
            g = static_cast<float>(Theme::BgG);
            b = static_cast<float>(Theme::BgB);
          }
          box.setColor(r, g, b, static_cast<float>((hovered ? 0.94 : 0.88) * glassOv));
          box.setRadius(9.0f);
          box.setGeometry(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(cw), static_cast<float>(ch));
          box.setGlassy(true);
          box.paint(cr);
        }
        cairo_round_rect(cr, cx, cy, cw, ch, 9.0);
        paint_src_glass_hi(app, cr, 0.11 * glassOv);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        settings_draw_text_line_fit_width(cr, sink_disp, cx + 12.0, cy + 19.0, cw - 40, 1.0);
        {
          const double chevCy = static_cast<double>(cy) + static_cast<double>(ch) * 0.52;
          const double chevAlpha = (hovered ? 0.95 : 0.55) * glassOv;
          material_symbols_draw_glyph(cr, static_cast<double>(cx + cw) - 14.0, chevCy, 18.0,
                                      expanded ? "expand_less" : "expand_more", Theme::TextR, Theme::TextG, Theme::TextB, chevAlpha);
        }
      }
      if (def_sink && snap.default_sink != 0) {
        int trx = 0, trey = 0, trw = 0;
        monitors_form_slider_track_geom_content(sg.devices.content_y0, sg.devices.x, sg.devices.w, 1, &trx, &trey, &trw);
        const int iconX = trx - kSndSliderIconW - kSpacingS;
        const bool iconHot = app.pointerX >= iconX && pyH >= trey - 4 && app.pointerX < iconX + kSndSliderIconW && pyH < trey + 28;
        material_symbols_draw_glyph(cr, static_cast<double>(iconX + kSndSliderIconW / 2),
                                    static_cast<double>(trey + 12), 18.0,
                                    def_sink->muted ? "volume_off" : "volume_up",
                                    Theme::TextR, Theme::TextG, Theme::TextB, (iconHot ? 0.95 : 0.50) * glassOv);
        char ddisp[32];
        std::snprintf(ddisp, sizeof(ddisp), "%d%%", std::clamp(def_sink->volume_pct, 0, 100));
        sound_settings_slider(app, cr, trx, trey, trw, std::clamp(def_sink->volume_pct, 0, 100), 0, 100, paintPointerYOffset,
                        ddisp, false,
                        (app.soundVolDragCode == 2000) ? app.settingsSliderDragNormT : -1.0);
      }

      // Default input.
      const eh::audio::Device* def_src = nullptr;
      for (const auto& d : snap.sources) {
        if (d.node_id == snap.default_source) def_src = &d;
      }
      const std::string src_disp =
          def_src ? (def_src->display_name.empty() ? def_src->name : def_src->display_name) : std::string("(none)");

      {
        const double iconPx = 16.0;
        const int rowY2 = sg.devices.content_y0 + 2 * kMonFormRowPitch;
        const double ix = static_cast<double>(sg.devices.x + kCardPad) + iconPx * 0.5;
        const double iconCy = static_cast<double>(rowY2 + 14);
        material_symbols_draw_glyph(cr, ix, iconCy, iconPx, "mic", Theme::AccR, Theme::AccG, Theme::AccB, 0.88 * glassOv);
        settings_show_text(cr, static_cast<double>(sg.devices.x + kCardPad) + iconPx + 8.0,
                      iconCy + static_cast<double>(namePh) * 0.5 - 2.0, "Default input", 13, 600, Theme::TextR, Theme::TextG, Theme::TextB, 0.95 * glassOv);
      }
      snd_combo_geom(sg.devices, 2, src_disp.c_str(), cx, cy, cw, ch);
      {
        const bool expanded = (app.soundActiveDd == 1);
        const bool hovered = app.pointerX >= cx && pyH >= cy && app.pointerX < cx + cw && pyH < cy + ch;
        {
          m3::Box box;
          float r, g, b;
          if (app.drawChromeMatugen) {
            r = app.drawChrome.panelFillR;
            g = app.drawChrome.panelFillG;
            b = app.drawChrome.panelFillB;
          } else {
            r = static_cast<float>(Theme::BgR);
            g = static_cast<float>(Theme::BgG);
            b = static_cast<float>(Theme::BgB);
          }
          box.setColor(r, g, b, static_cast<float>((hovered ? 0.94 : 0.88) * glassOv));
          box.setRadius(9.0f);
          box.setGeometry(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(cw), static_cast<float>(ch));
          box.setGlassy(true);
          box.paint(cr);
        }
        cairo_round_rect(cr, cx, cy, cw, ch, 9.0);
        paint_src_glass_hi(app, cr, 0.11 * glassOv);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        settings_draw_text_line_fit_width(cr, src_disp, cx + 12.0, cy + 19.0, cw - 40, 1.0);
        {
          const double chevCy = static_cast<double>(cy) + static_cast<double>(ch) * 0.52;
          const double chevAlpha = (hovered ? 0.95 : 0.55) * glassOv;
          material_symbols_draw_glyph(cr, static_cast<double>(cx + cw) - 14.0, chevCy, 18.0,
                                      expanded ? "expand_less" : "expand_more", Theme::TextR, Theme::TextG, Theme::TextB, chevAlpha);
        }
      }
      if (def_src && snap.default_source != 0) {
        int trx = 0, trey = 0, trw = 0;
        monitors_form_slider_track_geom_content(sg.devices.content_y0, sg.devices.x, sg.devices.w, 3, &trx, &trey, &trw);
        const int iconX = trx - kSndSliderIconW - kSpacingS;
        const bool iconHot = app.pointerX >= iconX && pyH >= trey - 4 && app.pointerX < iconX + kSndSliderIconW && pyH < trey + 28;
        material_symbols_draw_glyph(cr, static_cast<double>(iconX + kSndSliderIconW / 2),
                                    static_cast<double>(trey + 12), 18.0,
                                    def_src->muted ? "mic_off" : "mic",
                                    Theme::TextR, Theme::TextG, Theme::TextB, (iconHot ? 0.95 : 0.50) * glassOv);
        char sdisp[32];
        std::snprintf(sdisp, sizeof(sdisp), "%d%%", std::clamp(def_src->volume_pct, 0, 100));
        sound_settings_slider(app, cr, trx, trey, trw, std::clamp(def_src->volume_pct, 0, 100), 0, 100, paintPointerYOffset,
                        sdisp, false,
                        (app.soundVolDragCode == 2001) ? app.settingsSliderDragNormT : -1.0);
      }
    }

    if (sg.n_bt_cards > 0 && sg.bluetooth.h > 0) {
      fill_snd_card(sg.bluetooth);
      paint_snd_heading(sg.bluetooth, "Bluetooth", "bluetooth");
      for (int bi = 0; bi < sg.n_bt_cards; ++bi) {
        const auto& bc = bt_cards_full[static_cast<size_t>(bi)];
        std::string bt_disp = bc.active_profile_key;
        for (const auto& p : bc.profiles) {
          if (p.key == bc.active_profile_key) {
            bt_disp = p.description.empty() ? p.key : p.description;
            break;
          }
        }
        paint_snd_combo_row(sg.bluetooth, bi, bc.card_name.c_str(), bt_disp.c_str(),
                            app.soundActiveDd == (kSoundBtDdBase + bi));
      }
    }

    fill_snd_card(sg.engine);
    paint_snd_heading(sg.engine, "Graph & default output", "graphic_eq");
    {
      eh::audio::EngineSettings es{};
      eh::audio::CompatDefaultSinkFormat cf{};
      settings_sound_engine_paint_cache_read(app, &es, &cf);
      const std::string disp_rate =
          es.available ? sound_engine_hz_display(es.clock_rate_hz) : std::string("(pw-metadata unavailable)");
      const std::string disp_force =
          !es.available ? std::string("\xe2\x80\x94")
                      : (es.clock_force_rate_hz == 0 ? std::string("Auto") : sound_engine_hz_display(es.clock_force_rate_hz));
      const std::string disp_allow =
          es.available ? sound_engine_allowed_combo_display(es) : std::string("\xe2\x80\x94");
      const std::string disp_pcm = sound_compat_pcm_combo_display(cf);

      const struct { const char* label; int ddId; const std::string* value; } kEngRows[4] = {
        {"Default rate",          2, &disp_rate},
        {"Force graph rate",      3, &disp_force},
        {"Allowed rates preset",  4, &disp_allow},
        {"Default PCM format",    5, &disp_pcm},
      };

      int colW, col1X, col2X;
      snd_engine_grid_geom(sg.engine, &colW, &col1X, &col2X);

      auto paint_engine_cell = [&](int colIx, int rowIx, const char* label, const std::string& value, int ddId) {
        const int colX = (colIx == 0) ? col1X : col2X;
        const int labY = snd_engine_label_y(sg.engine, rowIx);
        const int selY = labY + kSndGridLabelH + kSndGridLabelGap;
        settings_show_text(cr, static_cast<double>(colX), static_cast<double>(labY),
                      label, 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.70 * glassOv);
        int sx = colX, sy = selY, sw = colW, sh = kSettingsComboH;
        const bool expanded = (app.soundActiveDd == ddId);
        const bool hovered = app.pointerX >= sx && pyH >= sy && app.pointerX < sx + sw && pyH < sy + sh;
        {
          m3::Box box;
          float r, g, b;
          if (app.drawChromeMatugen) {
            r = app.drawChrome.panelFillR;
            g = app.drawChrome.panelFillG;
            b = app.drawChrome.panelFillB;
          } else {
            r = static_cast<float>(Theme::BgR);
            g = static_cast<float>(Theme::BgG);
            b = static_cast<float>(Theme::BgB);
          }
          box.setColor(r, g, b, static_cast<float>((hovered ? 0.94 : 0.88) * glassOv));
          box.setRadius(9.0f);
          box.setGeometry(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sw), static_cast<float>(sh));
          box.setGlassy(true);
          box.paint(cr);
        }
        cairo_round_rect(cr, sx, sy, sw, sh, 9.0);
        paint_src_glass_hi(app, cr, 0.11 * glassOv);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        settings_draw_text_line_fit_width(cr, value, sx + 12.0, sy + 19.0, sw - 40, 1.0);
        {
          const double chevCy = static_cast<double>(sy) + static_cast<double>(sh) * 0.52;
          const double chevAlpha = (hovered ? 0.95 : 0.55) * glassOv;
          material_symbols_draw_glyph(cr, static_cast<double>(sx + sw) - 14.0, chevCy, 18.0,
                                      expanded ? "expand_less" : "expand_more", Theme::TextR, Theme::TextG, Theme::TextB, chevAlpha);
        }
      };

      paint_engine_cell(0, 0, kEngRows[0].label, *kEngRows[0].value, kEngRows[0].ddId);
      paint_engine_cell(1, 0, kEngRows[1].label, *kEngRows[1].value, kEngRows[1].ddId);
      paint_engine_cell(0, 1, kEngRows[2].label, *kEngRows[2].value, kEngRows[2].ddId);
      paint_engine_cell(1, 1, kEngRows[3].label, *kEngRows[3].value, kEngRows[3].ddId);
    }

    auto enrich = [](const std::string& icon_name, const std::string& app_id,
                      const std::string& process_binary, const std::string& app_name,
                      const std::string& process_path, const std::string& node_name,
                      const std::string& description,
                      std::string& out_name, std::string& out_icon) {
      out_name = app_name;
      out_icon = icon_name;
      auto lc = [](const std::string& s) {
        std::string o = s;
        std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return o;
      };
      if (!out_icon.empty()) {
        std::string n = lc(out_icon);
        if (n == "chromium" || n == "electron" || n == "chromium-browser" ||
            n == "google-chrome" || n == "google-chrome-stable" ||
            (n.starts_with("electron") && n.size() > 8))
          out_icon.clear();
      }

      StreamDesktopIds ids;
      ids.process_binary = process_binary;
      ids.app_id = app_id;
      ids.app_name = app_name;
      ids.node_name = node_name;
      ids.node_description = description;
      auto dei = resolve_desktop_entry_for_stream(ids);
      if (dei) {
        if (!dei->icon.empty()) out_icon = dei->icon;
        if (!dei->name.empty()) { out_name = dei->name; return; }
      }

      if (!description.empty() && !is_generic_runtime_binary(lc(description))) {
        out_name = description;
        return;
      }

      if (!out_name.empty() && !is_generic_runtime_binary(lc(out_name)))
        return;

      auto candidate = [&](const std::string& s) -> std::string {
        if (s.empty()) return {};
        std::string low = lc(s);
        if (is_generic_runtime_binary(low)) return {};
        low[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(low[0])));
        return low;
      };

      for (const std::string* src : {&process_binary, &process_path, &app_id, &node_name}) {
        if (src->empty()) continue;
        std::string bin = *src;
        const auto slash = bin.find_last_of("/\\");
        if (slash != std::string::npos && slash + 1 < bin.size()) bin = bin.substr(slash + 1);
        std::string name = candidate(bin);
        if (!name.empty()) { out_name = name; return; }
      }
    };

    fill_snd_card(sg.playback);
    paint_snd_heading(sg.playback, "Playback", "play_circle");
    if (n_play <= 0) {
      const int ly = sg.playback.content_y0;
      settings_show_text(cr, sg.playback.x + kCardPad, ly + 6, "No applications are playing audio.", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.70 * glassOv);
    } else {
      for (int i = 0; i < n_play; ++i) {
        const auto& st = snap.output_streams[static_cast<size_t>(i)];
        std::string displayName, displayIcon;
        enrich(st.icon_name, st.app_id, st.process_binary, st.app_name,
               st.process_path, st.node_name, st.description, displayName, displayIcon);
        const int comboRow = i * 2;
        const int appRowY = sg.playback.content_y0 + comboRow * kMonFormRowPitch;

        // App icon + name + description
        {
          const double iconCx = static_cast<double>(sg.playback.x + kCardPad + kSndAppIconSize / 2);
          const double iconCy = static_cast<double>(appRowY + kSndAppIconSize / 2 + 4);
          const double nameTextY = iconCy + static_cast<double>(namePh) * 0.5 - 2.0;
          const double descTextY = nameTextY + 18.0;
          eh::shell::mixer_icon::StreamIconIds ids;
          ids.icon_name = displayIcon;
          ids.app_id = st.app_id;
          ids.process_binary = st.process_binary;
          ids.process_path = st.process_path;
          ids.app_name = displayName;
          const eh::icons::IconEntry* iconEntry = eh::shell::mixer_icon::resolve_mixer_stream_theme_icon(app.icons, ids);
          {
            m3::Box box;
            box.setColor(Theme::AccR, Theme::AccG, Theme::AccB, 0.12f);
            box.setRadius(static_cast<float>(kSndAppIconRad));
            box.setGeometry(static_cast<float>(iconCx - kSndAppIconSize / 2),
                            static_cast<float>(iconCy - kSndAppIconSize / 2),
                            static_cast<float>(kSndAppIconSize), static_cast<float>(kSndAppIconSize));
            box.setGlassy(true);
            box.paint(cr);
          }
          if (iconEntry && iconEntry->surface) {
            cairo_save(cr);
            cairo_round_rect(cr, iconCx - kSndAppIconSize / 2.0, iconCy - kSndAppIconSize / 2.0,
                             kSndAppIconSize, kSndAppIconSize, kSndAppIconRad);
            cairo_clip(cr);
            const double s = static_cast<double>(kSndAppIconSize) / std::max(1, iconEntry->width);
            cairo_translate(cr, iconCx - static_cast<double>(kSndAppIconSize) / 2.0,
                            iconCy - static_cast<double>(kSndAppIconSize) / 2.0);
            cairo_scale(cr, s, s);
            cairo_set_source_surface(cr, iconEntry->surface, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
          } else {
            material_symbols_draw_glyph(cr, iconCx, iconCy, 18.0, "music_note",
                                        Theme::AccR, Theme::AccG, Theme::AccB, 0.75 * glassOv);
          }
          settings_show_text(cr, static_cast<double>(sg.playback.x + kCardPad + kSndAppIconSize + 10),
                        static_cast<double>(nameTextY),
                        displayName.empty() ? st.node_name.c_str() : displayName.c_str(),
                        13, 500, Theme::TextR, Theme::TextG, Theme::TextB, 0.95 * glassOv);
          settings_show_text(cr, static_cast<double>(sg.playback.x + kCardPad + kSndAppIconSize + 10),
                        descTextY,
                        st.node_name.c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.50 * glassOv);
        }

        // Route combo
        {
          std::string route = eh::audio::pw_link_list_playback_sink_for_stream(pw_link_list_blob, st.node_name);
          std::string route_disp = route;
          for (const auto& d : snap.sinks) {
            if (d.name == route) {
              route_disp = d.display_name.empty() ? d.name : d.display_name;
              break;
            }
          }
          if (route_disp.empty()) route_disp = "(unknown sink)";

          int cx = 0, cy = 0, cw = 0, ch = 0;
          snd_combo_geom(sg.playback, comboRow, route_disp.c_str(), cx, cy, cw, ch);
          const bool comb_hov = app.pointerX >= cx && pyH >= cy && app.pointerX < cx + cw && pyH < cy + ch;
          {
            m3::Box box;
            float r, g, b;
            if (app.drawChromeMatugen) {
              r = app.drawChrome.panelFillR;
              g = app.drawChrome.panelFillG;
              b = app.drawChrome.panelFillB;
            } else {
              r = static_cast<float>(Theme::BgR);
              g = static_cast<float>(Theme::BgG);
              b = static_cast<float>(Theme::BgB);
            }
            box.setColor(r, g, b, static_cast<float>((comb_hov ? 0.94 : 0.88) * glassOv));
            box.setRadius(9.0f);
            box.setGeometry(static_cast<float>(cx), static_cast<float>(cy),
                            static_cast<float>(cw), static_cast<float>(ch));
            box.setGlassy(true);
            box.paint(cr);
          }
          cairo_round_rect(cr, cx, cy, cw, ch, 9.0);
          paint_src_glass_hi(app, cr, 0.11 * glassOv);
          cairo_set_line_width(cr, 1.0);
          cairo_stroke(cr);
          const int valCharsP = static_cast<int>(std::clamp((cw - 40) / 7, 14, 48));
          settings_draw_trimmed_text_line(cr, route_disp, cx + 12.0, cy + 19.0, static_cast<size_t>(valCharsP),
                                          1.0, 12.f, 400);
          {
            const bool exp = app.soundActiveDd == (100 + i);
            const double chevCy = static_cast<double>(cy) + static_cast<double>(ch) * 0.52;
            const double chevAlpha = (comb_hov ? 0.95 : 0.55) * glassOv;
            material_symbols_draw_glyph(cr, static_cast<double>(cx + cw) - 14.0, chevCy, 18.0,
                                        exp ? "expand_less" : "expand_more", Theme::TextR, Theme::TextG, Theme::TextB, chevAlpha);
          }
        }

        // Volume slider with speaker icon (clickable for mute)
        {
          int trx = 0, trey = 0, trw = 0;
          sound_stream_slider_geom_content(sg.playback.content_y0, sg.playback.x, sg.playback.w, i, &trx, &trey, &trw);
          const int iconX = trx - kSndSliderIconW - kSpacingS;
          const bool iconHot = app.pointerX >= iconX && pyH >= trey - 4 && app.pointerX < iconX + kSndSliderIconW && pyH < trey + 28;
          material_symbols_draw_glyph(cr, static_cast<double>(iconX + kSndSliderIconW / 2),
                                      static_cast<double>(trey + 12), 18.0,
                                      st.muted ? "volume_off" : "volume_up",
                                      Theme::TextR, Theme::TextG, Theme::TextB, (iconHot ? 0.95 : 0.50) * glassOv);
          char disp[32];
          std::snprintf(disp, sizeof(disp), "%d%%", std::clamp(st.volume_pct, 0, 100));
          sound_settings_slider(app, cr, trx, trey, trw, std::clamp(st.volume_pct, 0, 100), 0, 100, paintPointerYOffset, disp,
                          false, (app.soundVolDragCode == i) ? app.settingsSliderDragNormT : -1.0);
        }
      }
    }

    fill_snd_card(sg.recording);
    paint_snd_heading(sg.recording, "Recording", "mic");
    if (n_rec <= 0) {
      const int ly = sg.recording.content_y0;
      settings_show_text(cr, sg.recording.x + kCardPad, ly + 6, "No applications are recording.", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.70 * glassOv);
    } else {
      for (int i = 0; i < n_rec; ++i) {
        const auto& st = snap.input_streams[static_cast<size_t>(i)];
        std::string displayName, displayIcon;
        enrich(st.icon_name, st.app_id, st.process_binary, st.app_name,
               st.process_path, st.node_name, st.description, displayName, displayIcon);
        const int comboRow = i * 2;
        const int appRowY = sg.recording.content_y0 + comboRow * kMonFormRowPitch;

        // App icon + name + description
        {
          const double iconCx = static_cast<double>(sg.recording.x + kCardPad + kSndAppIconSize / 2);
          const double iconCy = static_cast<double>(appRowY + kSndAppIconSize / 2 + 4);
          const double nameTextY = iconCy + static_cast<double>(namePh) * 0.5 - 2.0;
          const double descTextY = nameTextY + 18.0;
          eh::shell::mixer_icon::StreamIconIds ids;
          ids.icon_name = displayIcon;
          ids.app_id = st.app_id;
          ids.process_binary = st.process_binary;
          ids.process_path = st.process_path;
          ids.app_name = displayName;
          const eh::icons::IconEntry* iconEntry = eh::shell::mixer_icon::resolve_mixer_stream_theme_icon(app.icons, ids);
          {
            m3::Box box;
            box.setColor(Theme::AccR, Theme::AccG, Theme::AccB, 0.12f);
            box.setRadius(static_cast<float>(kSndAppIconRad));
            box.setGeometry(static_cast<float>(iconCx - kSndAppIconSize / 2),
                            static_cast<float>(iconCy - kSndAppIconSize / 2),
                            static_cast<float>(kSndAppIconSize), static_cast<float>(kSndAppIconSize));
            box.setGlassy(true);
            box.paint(cr);
          }
          if (iconEntry && iconEntry->surface) {
            cairo_save(cr);
            cairo_round_rect(cr, iconCx - kSndAppIconSize / 2.0, iconCy - kSndAppIconSize / 2.0,
                             kSndAppIconSize, kSndAppIconSize, kSndAppIconRad);
            cairo_clip(cr);
            const double s = static_cast<double>(kSndAppIconSize) / std::max(1, iconEntry->width);
            cairo_translate(cr, iconCx - static_cast<double>(kSndAppIconSize) / 2.0,
                            iconCy - static_cast<double>(kSndAppIconSize) / 2.0);
            cairo_scale(cr, s, s);
            cairo_set_source_surface(cr, iconEntry->surface, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
          } else {
            material_symbols_draw_glyph(cr, iconCx, iconCy, 18.0, "mic",
                                        Theme::AccR, Theme::AccG, Theme::AccB, 0.75 * glassOv);
          }
          settings_show_text(cr, static_cast<double>(sg.recording.x + kCardPad + kSndAppIconSize + 10),
                        nameTextY,
                        displayName.empty() ? st.node_name.c_str() : displayName.c_str(),
                        13, 500, Theme::TextR, Theme::TextG, Theme::TextB, 0.95 * glassOv);
          settings_show_text(cr, static_cast<double>(sg.recording.x + kCardPad + kSndAppIconSize + 10),
                        descTextY,
                        st.node_name.c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.50 * glassOv);
        }

        // Route combo
        {
          std::string route = eh::audio::pw_link_list_capture_source_for_stream(pw_link_list_blob, st.node_name);
          std::string route_disp = route;
          for (const auto& d : snap.sources) {
            if (d.name == route) {
              route_disp = d.display_name.empty() ? d.name : d.display_name;
              break;
            }
          }
          if (route_disp.empty()) route_disp = "(unknown source)";

          int cx = 0, cy = 0, cw = 0, ch = 0;
          snd_combo_geom(sg.recording, comboRow, route_disp.c_str(), cx, cy, cw, ch);
          const bool comb_hov = app.pointerX >= cx && pyH >= cy && app.pointerX < cx + cw && pyH < cy + ch;
          {
            m3::Box box;
            float r, g, b;
            if (app.drawChromeMatugen) {
              r = app.drawChrome.panelFillR;
              g = app.drawChrome.panelFillG;
              b = app.drawChrome.panelFillB;
            } else {
              r = static_cast<float>(Theme::BgR);
              g = static_cast<float>(Theme::BgG);
              b = static_cast<float>(Theme::BgB);
            }
            box.setColor(r, g, b, static_cast<float>((comb_hov ? 0.94 : 0.88) * glassOv));
            box.setRadius(9.0f);
            box.setGeometry(static_cast<float>(cx), static_cast<float>(cy),
                            static_cast<float>(cw), static_cast<float>(ch));
            box.setGlassy(true);
            box.paint(cr);
          }
          cairo_round_rect(cr, cx, cy, cw, ch, 9.0);
          paint_src_glass_hi(app, cr, 0.11 * glassOv);
          cairo_set_line_width(cr, 1.0);
          cairo_stroke(cr);
          const int valCharsR = static_cast<int>(std::clamp((cw - 40) / 7, 14, 48));
          settings_draw_trimmed_text_line(cr, route_disp, cx + 12.0, cy + 19.0, static_cast<size_t>(valCharsR),
                                          1.0, 12.f, 400);
          {
            const bool exp = app.soundActiveDd == (1000 + i);
            const double chevCy = static_cast<double>(cy) + static_cast<double>(ch) * 0.52;
            const double chevAlpha = (comb_hov ? 0.95 : 0.55) * glassOv;
            material_symbols_draw_glyph(cr, static_cast<double>(cx + cw) - 14.0, chevCy, 18.0,
                                        exp ? "expand_less" : "expand_more", Theme::TextR, Theme::TextG, Theme::TextB, chevAlpha);
          }
        }

        // Volume slider with speaker icon (clickable for mute)
        {
          int trx = 0, trey = 0, trw = 0;
          sound_stream_slider_geom_content(sg.recording.content_y0, sg.recording.x, sg.recording.w, i, &trx, &trey, &trw);
          const int iconX = trx - kSndSliderIconW - kSpacingS;
          const bool iconHot = app.pointerX >= iconX && pyH >= trey - 4 && app.pointerX < iconX + kSndSliderIconW && pyH < trey + 28;
          material_symbols_draw_glyph(cr, static_cast<double>(iconX + kSndSliderIconW / 2),
                                      static_cast<double>(trey + 12), 18.0,
                                      st.muted ? "mic_off" : "mic",
                                      Theme::TextR, Theme::TextG, Theme::TextB, (iconHot ? 0.95 : 0.50) * glassOv);
          char disp[32];
          std::snprintf(disp, sizeof(disp), "%d%%", std::clamp(st.volume_pct, 0, 100));
          sound_settings_slider(app, cr, trx, trey, trw, std::clamp(st.volume_pct, 0, 100), 0, 100, paintPointerYOffset, disp,
                          false, (app.soundVolDragCode == 1000 + i) ? app.settingsSliderDragNormT : -1.0);
        }
      }
    }

    cairo_restore(cr);
    settings_paint_sound_dropdown_unclipped(app, cr, contentX, contentW, glassOv, snap);
}

// settings_paint_sound_dropdown_unclipped (from settings_app.cpp)
static void settings_paint_sound_dropdown_unclipped(App& app, cairo_t* cr, int /*contentX*/, int /*contentW*/,
                                                    double glassOv, const eh::audio::Snapshot& snap) {
   
  if (app.soundActiveDd < 0) return;
  int tcx = 0;
  int tcw = 0;
  settings_content_column_geom(app, &tcx, &tcw);
  const int n_play = static_cast<int>(snap.output_streams.size());
  const int n_rec = static_cast<int>(snap.input_streams.size());
  const int n_bt = sound_tab_bt_count_clamped(app);
  const SoundTabGeom sg =
      sound_compute_tab_geom(tcx, tcw, kContentTop, settings_content_viewport_h(app), n_play, n_rec, n_bt);

  int dcx = 0;
  int dcy = 0;
  int dcw = 0;
  int dch = 0;
  if (!sound_dd_combo_geom(sg, app.soundActiveDd, n_play, n_rec, &dcx, &dcy, &dcw, &dch)) return;

  auto& pw = eh::audio::PipeWireService::instance();
  const eh::audio::EngineSettings es_dd = pw.query_engine_settings();

  static std::vector<std::string> soundDdStore;
  static std::vector<const char*> soundDdPtrs;
  soundDdStore.clear();
  soundDdPtrs.clear();
  int nrows = 0;
  int sel_ix = 0;

  if (app.soundActiveDd == 0) {
    nrows = static_cast<int>(snap.sinks.size());
    for (const auto& d : snap.sinks)
      soundDdStore.push_back(d.display_name.empty() ? d.name : d.display_name);
    for (int j = 0; j < nrows; ++j) {
      if (snap.sinks[static_cast<size_t>(j)].node_id == snap.default_sink) sel_ix = j;
    }
  } else if (app.soundActiveDd == 1) {
    nrows = static_cast<int>(snap.sources.size());
    for (const auto& d : snap.sources)
      soundDdStore.push_back(d.display_name.empty() ? d.name : d.display_name);
    for (int j = 0; j < nrows; ++j) {
      if (snap.sources[static_cast<size_t>(j)].node_id == snap.default_source) sel_ix = j;
    }
  } else if (app.soundActiveDd == 2) {
    nrows = kSoundEngineRateCount;
    for (int j = 0; j < nrows; ++j) soundDdStore.push_back(sound_engine_hz_display(kSoundEngineRates[j]));
    sel_ix = -1;
    for (int j = 0; j < nrows; ++j) {
      if (kSoundEngineRates[j] == es_dd.clock_rate_hz) sel_ix = j;
    }
  } else if (app.soundActiveDd == 3) {
    nrows = 1 + kSoundEngineRateCount;
    soundDdStore.emplace_back("Auto");
    for (int j = 0; j < kSoundEngineRateCount; ++j) soundDdStore.push_back(sound_engine_hz_display(kSoundEngineRates[j]));
    if (es_dd.clock_force_rate_hz == 0)
      sel_ix = 0;
    else {
      sel_ix = -1;
      for (int j = 0; j < kSoundEngineRateCount; ++j) {
        if (kSoundEngineRates[j] == es_dd.clock_force_rate_hz) sel_ix = 1 + j;
      }
    }
  } else if (app.soundActiveDd == 4) {
    nrows = 4;
    soundDdStore.emplace_back("Hi-Fi (44.1-192 kHz)");
    soundDdStore.emplace_back("Consumer (44.1 / 48 kHz)");
    soundDdStore.emplace_back("Pro (48 / 96 / 192 kHz)");
    soundDdStore.emplace_back("Single (default rate only)");
    sel_ix = sound_engine_allowed_preset_sel(es_dd);
  } else if (app.soundActiveDd == 5) {
    nrows = kSoundCompatPcmChoiceCount;
    const eh::audio::CompatDefaultSinkFormat cf_dd = pw.query_compat_default_sink_format();
    for (int j = 0; j < nrows; ++j) soundDdStore.emplace_back(kSoundCompatPcmLabels[j]);
    sel_ix = sound_compat_pcm_dropdown_sel_ix(cf_dd);
  } else if (app.soundActiveDd >= kSoundBtDdBase && app.soundActiveDd < kSoundBtDdBase + kSoundMaxBtCards) {
    const int bi = app.soundActiveDd - kSoundBtDdBase;
    const auto& btc = settings_sound_bt_cards_cached(app);
    if (bi < 0 || bi >= static_cast<int>(btc.size())) return;
    const auto& bc = btc[static_cast<size_t>(bi)];
    nrows = static_cast<int>(bc.profiles.size());
    for (const auto& p : bc.profiles) {
      soundDdStore.push_back(p.description.empty() ? p.key : (p.description + " (" + p.key + ")"));
    }
    sel_ix = -1;
    for (int j = 0; j < nrows; ++j) {
      if (bc.profiles[static_cast<size_t>(j)].key == bc.active_profile_key) sel_ix = j;
    }
  } else if (app.soundActiveDd >= 100 && app.soundActiveDd < 1000) {
    const int si = app.soundActiveDd - 100;
    if (si < 0 || si >= n_play) return;
    const auto& st = snap.output_streams[static_cast<size_t>(si)];
    nrows = static_cast<int>(snap.sinks.size());
    const std::string route = pw.playback_route_sink_name(st.node_id);
    for (const auto& d : snap.sinks)
      soundDdStore.push_back(d.display_name.empty() ? d.name : d.display_name);
    for (int j = 0; j < nrows; ++j) {
      if (snap.sinks[static_cast<size_t>(j)].name == route) sel_ix = j;
    }
  } else if (app.soundActiveDd >= 1000) {
    const int si = app.soundActiveDd - 1000;
    if (si < 0 || si >= n_rec) return;
    const auto& st = snap.input_streams[static_cast<size_t>(si)];
    nrows = static_cast<int>(snap.sources.size());
    const std::string route = pw.capture_route_source_name(st.node_id);
    for (const auto& d : snap.sources)
      soundDdStore.push_back(d.display_name.empty() ? d.name : d.display_name);
    for (int j = 0; j < nrows; ++j) {
      if (snap.sources[static_cast<size_t>(j)].name == route) sel_ix = j;
    }
  }

  for (const auto& s : soundDdStore) soundDdPtrs.push_back(s.c_str());
  if (nrows <= 0 || static_cast<int>(soundDdPtrs.size()) != nrows) return;

  const int text_max_px = settings_measure_max_text_advance_px_sans12(soundDdStore);
  constexpr int kSoundPopupTextHPad = 28;
  const int desired_w = text_max_px + kSoundPopupTextHPad;
  const int margin = 12;
  const int screen_budget = std::max(dcw, app.width - 2 * margin);
  int popup_w = std::max(dcw, std::min(desired_w, screen_budget));
  int popup_x = dcx;
  if (popup_x + popup_w > app.width - margin) popup_x = app.width - margin - popup_w;
  if (popup_x < margin) {
    popup_x = margin;
    popup_w = std::max(dcw, std::min(popup_w, app.width - 2 * margin));
  }

  const int list_doc_top =
      sound_dd_popup_list_doc_top_y(dcy, dch, nrows, settings_scroll_px_int(app), app.height);
  const int list_device_y = list_doc_top - settings_scroll_px_int(app);
  settings_paint_combo_list_popup(app, cr, popup_x, list_device_y, popup_w, kSettingsDdRowH, nrows, soundDdPtrs.data(),
                                  sel_ix, app.soundDdHoverRow, glassOv, 0, 0, true);
}

// settings_sound_consume_pointer_down (from settings_sound_ui.inl)
bool settings_sound_consume_pointer_down(App& app, int contentX, int contentW) {
    
  (void)contentX;
  (void)contentW;
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();

  const eh::audio::Snapshot snap = pw.snapshot();
  const int n_play = static_cast<int>(snap.output_streams.size());
  const int n_rec = static_cast<int>(snap.input_streams.size());
  const int n_bt = sound_tab_bt_count_clamped(app);
  settings_clamp_sound_scroll_px(app, n_play, n_rec, n_bt);
  const double pyLogical = app.pointerY + settings_scroll_px(app);

  int tcx = 0;
  int tcw = 0;
  settings_content_column_geom(app, &tcx, &tcw);
  const SoundTabGeom sg =
      sound_compute_tab_geom(tcx, tcw, kContentTop, settings_content_viewport_h(app), n_play, n_rec, n_bt);

  if (app.soundActiveDd >= 0) {
    int cx = 0;
    int cy = 0;
    int cw = 0;
    int ch = 0;
    if (!sound_dd_combo_geom(sg, app.soundActiveDd, n_play, n_rec, &cx, &cy, &cw, &ch)) {
      app.soundActiveDd = -1;
      draw(app);
      return true;
    }
    int nrows = 0;
    if (app.soundActiveDd == 0)
      nrows = static_cast<int>(snap.sinks.size());
    else if (app.soundActiveDd == 1)
      nrows = static_cast<int>(snap.sources.size());
    else if (app.soundActiveDd == 2)
      nrows = kSoundEngineRateCount;
    else if (app.soundActiveDd == 3)
      nrows = 1 + kSoundEngineRateCount;
    else if (app.soundActiveDd == 4)
      nrows = 4;
    else if (app.soundActiveDd == 5)
      nrows = kSoundCompatPcmChoiceCount;
    else if (app.soundActiveDd >= 100 && app.soundActiveDd < 1000)
      nrows = static_cast<int>(snap.sinks.size());
    else if (app.soundActiveDd >= 1000)
      nrows = static_cast<int>(snap.sources.size());
    else if (app.soundActiveDd >= kSoundBtDdBase && app.soundActiveDd < kSoundBtDdBase + kSoundMaxBtCards) {
      const int bi = app.soundActiveDd - kSoundBtDdBase;
      const auto& btc = settings_sound_bt_cards_cached(app);
      if (bi >= 0 && bi < static_cast<int>(btc.size()))
        nrows = static_cast<int>(btc[static_cast<size_t>(bi)].profiles.size());
    }

    std::vector<std::string> dd_labels;
    if (app.soundActiveDd == 0) {
      for (const auto& d : snap.sinks)
        dd_labels.push_back(d.display_name.empty() ? d.name : d.display_name);
    } else if (app.soundActiveDd == 1) {
      for (const auto& d : snap.sources)
        dd_labels.push_back(d.display_name.empty() ? d.name : d.display_name);
    } else if (app.soundActiveDd == 2) {
      for (int j = 0; j < kSoundEngineRateCount; ++j)
        dd_labels.push_back(sound_engine_hz_display(kSoundEngineRates[j]));
    } else if (app.soundActiveDd == 3) {
      dd_labels.emplace_back("Auto");
      for (int j = 0; j < kSoundEngineRateCount; ++j)
        dd_labels.push_back(sound_engine_hz_display(kSoundEngineRates[j]));
    } else if (app.soundActiveDd == 4) {
      dd_labels = {"Hi-Fi (44.1-192 kHz)", "Consumer (44.1 / 48 kHz)", "Pro (48 / 96 / 192 kHz)", "Single (default rate only)"};
    } else if (app.soundActiveDd == 5) {
      for (int j = 0; j < kSoundCompatPcmChoiceCount; ++j)
        dd_labels.emplace_back(kSoundCompatPcmLabels[j]);
    } else if (app.soundActiveDd >= 100 && app.soundActiveDd < 1000) {
      for (const auto& d : snap.sinks)
        dd_labels.push_back(d.display_name.empty() ? d.name : d.display_name);
    } else if (app.soundActiveDd >= 1000) {
      for (const auto& d : snap.sources)
        dd_labels.push_back(d.display_name.empty() ? d.name : d.display_name);
    } else if (app.soundActiveDd >= kSoundBtDdBase && app.soundActiveDd < kSoundBtDdBase + kSoundMaxBtCards) {
      const int bi = app.soundActiveDd - kSoundBtDdBase;
      const auto& btc = settings_sound_bt_cards_cached(app);
      if (bi >= 0 && bi < static_cast<int>(btc.size())) {
        for (const auto& p : btc[static_cast<size_t>(bi)].profiles)
          dd_labels.push_back(p.description.empty() ? p.key : (p.description + " (" + p.key + ")"));
      }
    }
    int popup_list_x = cx;
    int popup_list_w = cw;
    if (!dd_labels.empty()) {
      const int text_max_px = settings_measure_max_text_advance_px_sans12(dd_labels);
      constexpr int kSoundDdTextHPad = 28;
      const int desired_w = text_max_px + kSoundDdTextHPad;
      const int margin = 12;
      const int screen_budget = std::max(cw, app.width - 2 * margin);
      popup_list_w = std::max(cw, std::min(desired_w, screen_budget));
      popup_list_x = cx;
      if (popup_list_x + popup_list_w > app.width - margin) popup_list_x = app.width - margin - popup_list_w;
      if (popup_list_x < margin) {
        popup_list_x = margin;
        popup_list_w = std::max(cw, std::min(popup_list_w, app.width - 2 * margin));
      }
    }

    const int ly = sound_dd_popup_list_doc_top_y(cy, ch, nrows, settings_scroll_px_int(app), app.height);
    if (nrows > 0 &&
        point_in_rect(app.pointerX, pyLogical, popup_list_x, ly, popup_list_w, nrows * kSettingsDdRowH)) {
      const int rr = settings_mode_dd_pointer_row(app.pointerX, pyLogical, popup_list_x, ly, popup_list_w,
                                                  kSettingsDdRowH, nrows);
      if (rr >= 0) {
        if (app.soundActiveDd == 0 && rr < nrows) {
          pw.set_default_sink(snap.sinks[static_cast<size_t>(rr)].node_id);
          app.settings.audioDefaultSinkName = snap.sinks[static_cast<size_t>(rr)].name;
          save_settings(app.settings);
        } else if (app.soundActiveDd == 1 && rr < nrows) {
          pw.set_default_source(snap.sources[static_cast<size_t>(rr)].node_id);
          app.settings.audioDefaultSourceName = snap.sources[static_cast<size_t>(rr)].name;
          save_settings(app.settings);
        } else if (app.soundActiveDd == 2 && rr >= 0 && rr < kSoundEngineRateCount) {
          pw.apply_engine_clock_rate_hz(kSoundEngineRates[rr]);
          settings_sound_engine_paint_cache_invalidate();
          app.settings.audioEngineClockRateHz = kSoundEngineRates[rr];
          save_settings(app.settings);
        } else if (app.soundActiveDd == 3 && rr >= 0) {
          if (rr == 0)
            pw.apply_engine_force_rate_hz(0);
          else if (rr - 1 >= 0 && rr - 1 < kSoundEngineRateCount)
            pw.apply_engine_force_rate_hz(kSoundEngineRates[rr - 1]);
          settings_sound_engine_paint_cache_invalidate();
          app.settings.audioEngineForceRateHz = (rr == 0) ? 0 : kSoundEngineRates[rr - 1];
          save_settings(app.settings);
        } else if (app.soundActiveDd == 4 && rr >= 0 && rr < 4) {
          static const int kHi[] = {44100, 48000, 88200, 96000, 176400, 192000};
          const eh::audio::EngineSettings es_sel = pw.query_engine_settings();
          if (rr == 0) {
            pw.apply_engine_allowed_rates_hz(std::vector<int>(kHi, kHi + 6));
            app.settings.audioEngineAllowedRatesHz = std::vector<int>(kHi, kHi + 6);
          } else if (rr == 1) {
            pw.apply_engine_allowed_rates_hz(std::vector<int>{44100, 48000});
            app.settings.audioEngineAllowedRatesHz = {44100, 48000};
          } else if (rr == 2) {
            pw.apply_engine_allowed_rates_hz(std::vector<int>{48000, 96000, 192000});
            app.settings.audioEngineAllowedRatesHz = {48000, 96000, 192000};
          } else if (rr == 3 && es_sel.available) {
            pw.apply_engine_allowed_rates_hz(std::vector<int>{es_sel.clock_rate_hz});
            app.settings.audioEngineAllowedRatesHz = {es_sel.clock_rate_hz};
          }
          settings_sound_engine_paint_cache_invalidate();
          save_settings(app.settings);
        } else if (app.soundActiveDd == 5 && rr >= 0 && rr < kSoundCompatPcmChoiceCount) {
          pw.apply_compat_default_sink_pcm_format(rr);
          settings_sound_engine_paint_cache_invalidate();
          app.settings.audioCompatPcmFormat = rr;
          save_settings(app.settings);
        } else if (app.soundActiveDd >= kSoundBtDdBase && app.soundActiveDd < kSoundBtDdBase + kSoundMaxBtCards) {
          const int bi = app.soundActiveDd - kSoundBtDdBase;
          const auto& btc = settings_sound_bt_cards_cached(app);
          if (bi >= 0 && bi < static_cast<int>(btc.size()) && rr >= 0 &&
              rr < static_cast<int>(btc[static_cast<size_t>(bi)].profiles.size())) {
            pw.apply_bluetooth_card_profile(btc[static_cast<size_t>(bi)].card_index,
                                            btc[static_cast<size_t>(bi)].profiles[static_cast<size_t>(rr)].key);
            settings_sound_bt_invalidate_cache();
            save_settings(app.settings);
          }
        } else if (app.soundActiveDd >= 100 && app.soundActiveDd < 1000) {
          const int si = app.soundActiveDd - 100;
          if (si >= 0 && si < n_play && rr < nrows)
            pw.route_playback_stream_to_sink(snap.output_streams[static_cast<size_t>(si)].node_id,
                                             snap.sinks[static_cast<size_t>(rr)].node_id);
        } else if (app.soundActiveDd >= 1000) {
          const int si = app.soundActiveDd - 1000;
          if (si >= 0 && si < n_rec && rr < nrows)
            pw.route_capture_stream_to_source(snap.input_streams[static_cast<size_t>(si)].node_id,
                                              snap.sources[static_cast<size_t>(rr)].node_id);
        }
      }
    }
    app.soundActiveDd = -1;
    app.soundDdHoverRow = -1;
    draw(app);
    return true;
  }

  int cx = 0;
  int cy = 0;
  int cw = 0;
  int ch = 0;
  {
    const eh::audio::Device* snd_def_sink = nullptr;
    for (const auto& d : snap.sinks) {
      if (d.node_id == snap.default_sink) snd_def_sink = &d;
    }
    const std::string snd_sink_disp =
        snd_def_sink ? (snd_def_sink->display_name.empty() ? snd_def_sink->name : snd_def_sink->display_name) : std::string("(none)");
    snd_combo_text_geom(sg.devices.content_y0, sg.devices.x, sg.devices.w, 0, snd_sink_disp.c_str(), &cx, &cy, &cw, &ch);
  }
  if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
    settings_close_non_default_app_dropdowns(app);
    app.soundActiveDd = 0;
    draw(app);
    return true;
  }
  if (snap.default_sink != 0) {
    const eh::audio::Device* sd = nullptr;
    for (const auto& d : snap.sinks) {
      if (d.node_id == snap.default_sink) sd = &d;
    }
    if (sd) {
      int trx = 0;
      int trey = 0;
      int trw = 0;
      monitors_form_slider_track_geom_content(sg.devices.content_y0, sg.devices.x, sg.devices.w, 1, &trx, &trey, &trw);
      const int iconX = trx - kSndSliderIconW - kSpacingS;
      if (point_in_rect(app.pointerX, pyLogical, iconX, trey - 4, kSndSliderIconW, 28)) {
        pw.set_node_mute(sd->node_id, !sd->muted);
        app.settings.audioDefaultSinkMuted = !sd->muted;
        save_settings(app.settings);
        draw(app);
        return true;
      }
      if (point_in_rect(app.pointerX, pyLogical, trx - 6, trey - 4, trw + 12, 28)) {
        app.soundVolDragCode = 2000;
        app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trx, trw);
        pw.set_node_volume(sd->node_id, slider_norm_from_x(app.pointerX, trx, trw));
        app.soundVolDragPwNodeId = sd->node_id;
        app.soundVolPwLastApplyMonoMs = eh::shell::now_mono_ms();
        draw(app);
        return true;
      }
    }
  }

  {
    const eh::audio::Device* snd_def_src = nullptr;
    for (const auto& d : snap.sources) {
      if (d.node_id == snap.default_source) snd_def_src = &d;
    }
    const std::string snd_src_disp =
        snd_def_src ? (snd_def_src->display_name.empty() ? snd_def_src->name : snd_def_src->display_name) : std::string("(none)");
    snd_combo_text_geom(sg.devices.content_y0, sg.devices.x, sg.devices.w, 2, snd_src_disp.c_str(), &cx, &cy, &cw, &ch);
  }
  if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
    settings_close_non_default_app_dropdowns(app);
    app.soundActiveDd = 1;
    draw(app);
    return true;
  }

  if (snap.default_source != 0) {
    const eh::audio::Device* srcd = nullptr;
    for (const auto& d : snap.sources) {
      if (d.node_id == snap.default_source) srcd = &d;
    }
    if (srcd) {
      int trx = 0;
      int trey = 0;
      int trw = 0;
      monitors_form_slider_track_geom_content(sg.devices.content_y0, sg.devices.x, sg.devices.w, 3, &trx, &trey, &trw);
      const int iconX = trx - kSndSliderIconW - kSpacingS;
      if (point_in_rect(app.pointerX, pyLogical, iconX, trey - 4, kSndSliderIconW, 28)) {
        pw.set_node_mute(srcd->node_id, !srcd->muted);
        app.settings.audioDefaultSourceMuted = !srcd->muted;
        save_settings(app.settings);
        draw(app);
        return true;
      }
      if (point_in_rect(app.pointerX, pyLogical, trx - 6, trey - 4, trw + 12, 28)) {
        app.soundVolDragCode = 2001;
        app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trx, trw);
        pw.set_node_volume(srcd->node_id, slider_norm_from_x(app.pointerX, trx, trw));
        app.soundVolDragPwNodeId = srcd->node_id;
        app.soundVolPwLastApplyMonoMs = eh::shell::now_mono_ms();
        draw(app);
        return true;
      }
    }
  }

  for (int bi = 0; bi < sg.n_bt_cards; ++bi) {
    {
      const auto& btcc = settings_sound_bt_cards_cached(app);
      const auto& btcrd = btcc[static_cast<size_t>(bi)];
      std::string bt_disp = btcrd.active_profile_key;
      for (const auto& p : btcrd.profiles) {
        if (p.key == btcrd.active_profile_key) {
          bt_disp = p.description.empty() ? p.key : p.description;
          break;
        }
      }
      snd_combo_text_geom(sg.bluetooth.content_y0, sg.bluetooth.x, sg.bluetooth.w, bi, bt_disp.c_str(), &cx, &cy, &cw, &ch);
    }
    if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
      settings_close_non_default_app_dropdowns(app);
      app.soundActiveDd = kSoundBtDdBase + bi;
      draw(app);
      return true;
    }
  }

  const eh::audio::EngineSettings es_nav = pw.query_engine_settings();
  const eh::audio::CompatDefaultSinkFormat cf_nav = pw.query_compat_default_sink_format();
  if (es_nav.available) {
    {
      snd_engine_select_geom(sg.engine, 0, 0, &cx, &cy, &cw, &ch);
      if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
        settings_close_non_default_app_dropdowns(app);
        app.soundActiveDd = 2;
        draw(app);
        return true;
      }
    }
    {
      snd_engine_select_geom(sg.engine, 1, 0, &cx, &cy, &cw, &ch);
      if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
        settings_close_non_default_app_dropdowns(app);
        app.soundActiveDd = 3;
        draw(app);
        return true;
      }
    }
    {
      snd_engine_select_geom(sg.engine, 0, 1, &cx, &cy, &cw, &ch);
      if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
        settings_close_non_default_app_dropdowns(app);
        app.soundActiveDd = 4;
        draw(app);
        return true;
      }
    }
  }
  if (cf_nav.available) {
    snd_engine_select_geom(sg.engine, 1, 1, &cx, &cy, &cw, &ch);
    if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
      settings_close_non_default_app_dropdowns(app);
      app.soundActiveDd = 5;
      draw(app);
      return true;
    }
  }

  for (int i = 0; i < n_play; ++i) {
    monitors_combo_geom_at_content_row(sg.playback.content_y0, sg.playback.x, sg.playback.w, i * 2, &cx, &cy, &cw,
                                       &ch);
    if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
      settings_close_non_default_app_dropdowns(app);
      app.soundActiveDd = 100 + i;
      draw(app);
      return true;
    }
    int trx = 0;
    int trey = 0;
    int trw = 0;
    sound_stream_slider_geom_content(sg.playback.content_y0, sg.playback.x, sg.playback.w, i, &trx, &trey, &trw);
    const int iconX = trx - kSndSliderIconW - kSpacingS;
    if (point_in_rect(app.pointerX, pyLogical, iconX, trey - 4, kSndSliderIconW, 28)) {
      const auto& st = snap.output_streams[static_cast<size_t>(i)];
      pw.set_node_mute(st.node_id, !st.muted);
      draw(app);
      return true;
    }
    if (point_in_rect(app.pointerX, pyLogical, trx - 6, trey - 4, trw + 12, 28)) {
      app.soundVolDragCode = i;
      app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trx, trw);
      const std::uint32_t nid = snap.output_streams[static_cast<size_t>(i)].node_id;
      pw.set_node_volume(nid, slider_norm_from_x(app.pointerX, trx, trw));
      app.soundVolDragPwNodeId = nid;
      app.soundVolPwLastApplyMonoMs = eh::shell::now_mono_ms();
      draw(app);
      return true;
    }
  }

  for (int i = 0; i < n_rec; ++i) {
    monitors_combo_geom_at_content_row(sg.recording.content_y0, sg.recording.x, sg.recording.w, i * 2, &cx, &cy, &cw,
                                       &ch);
    if (point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch)) {
      settings_close_non_default_app_dropdowns(app);
      app.soundActiveDd = 1000 + i;
      draw(app);
      return true;
    }
    int trx = 0;
    int trey = 0;
    int trw = 0;
    sound_stream_slider_geom_content(sg.recording.content_y0, sg.recording.x, sg.recording.w, i, &trx, &trey, &trw);
    const int iconX = trx - kSndSliderIconW - kSpacingS;
    if (point_in_rect(app.pointerX, pyLogical, iconX, trey - 4, kSndSliderIconW, 28)) {
      const auto& st = snap.input_streams[static_cast<size_t>(i)];
      pw.set_node_mute(st.node_id, !st.muted);
      draw(app);
      return true;
    }
    if (point_in_rect(app.pointerX, pyLogical, trx - 6, trey - 4, trw + 12, 28)) {
      app.soundVolDragCode = 1000 + i;
      app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trx, trw);
      const std::uint32_t nid = snap.input_streams[static_cast<size_t>(i)].node_id;
      pw.set_node_volume(nid, slider_norm_from_x(app.pointerX, trx, trw));
      app.soundVolDragPwNodeId = nid;
      app.soundVolPwLastApplyMonoMs = eh::shell::now_mono_ms();
      draw(app);
      return true;
    }
  }

  return false;
}
