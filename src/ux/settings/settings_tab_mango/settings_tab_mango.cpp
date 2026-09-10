#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "ux/settings/data/mango/settings_mango_data.hpp"
#include "ux/settings/settings_tab_mango/settings_tab_mango.hpp"
#include "ux/settings/common/settings_common.hpp"

extern void draw(App& app);

using MangoCfg = eh::settings_mango::MangoConfig;

void mango_commit_cfg(App& app) {
   
  eh::settings_mango::MangoConfigFile mcf;
  mcf.cfg = app.mangoConfig;
  mcf.lines = app.mangoConfigLines;
  eh::settings_mango::write_mango_config(mcf);
  eh::settings_mango::apply_mango_config();
}

// Slider dispatch.
// sliderIdx encodes (subTab * 100 + localIndex). Returns true if value changed.
bool mango_slider_apply(App& app, int sliderIdx, double px) {
   
  auto& cfg = app.mangoConfig;
  int st = sliderIdx / 100;
  int si = sliderIdx % 100;
  int contentX = 16 + 240 + 16;
  int contentW = app.width - contentX - 16;
  int trX = contentX + kCardPad;
  int trW = contentW - kCardPad - kCardPad - 52;
  if (trW < 40) trW = 40;

  if (st == 0) {
    int v;
    switch (si) {
      case 0: v = slider_value_from_x(px, trX, trW, 0, 20);   if (v == cfg.borderpx) return false; cfg.borderpx = v; return true;
      case 1: v = slider_value_from_x(px, trX, trW, 0, 50);   if (v == cfg.border_radius) return false; cfg.border_radius = v; return true;
      case 2: v = slider_value_from_x(px, trX, trW, 10, 100); if (v == static_cast<int>(cfg.focused_opacity * 100.0)) return false; cfg.focused_opacity = v / 100.0; return true;
      case 3: v = slider_value_from_x(px, trX, trW, 10, 100); if (v == static_cast<int>(cfg.unfocused_opacity * 100.0)) return false; cfg.unfocused_opacity = v / 100.0; return true;
      case 4: v = slider_value_from_x(px, trX, trW, 0, 50);   if (v == cfg.shadows_size) return false; cfg.shadows_size = v; return true;
      case 5: v = slider_value_from_x(px, trX, trW, 0, 50);   if (v == cfg.shadows_blur) return false; cfg.shadows_blur = v; return true;
      case 6: v = slider_value_from_x(px, trX, trW, -30, 30); if (v == cfg.shadows_position_x) return false; cfg.shadows_position_x = v; return true;
      case 7: v = slider_value_from_x(px, trX, trW, -30, 30); if (v == cfg.shadows_position_y) return false; cfg.shadows_position_y = v; return true;
      case 8: v = slider_value_from_x(px, trX, trW, 1, 20);   if (v == cfg.blur_params_radius) return false; cfg.blur_params_radius = v; return true;
      case 9: v = slider_value_from_x(px, trX, trW, 1, 6);    if (v == cfg.blur_params_num_passes) return false; cfg.blur_params_num_passes = v; return true;
      case 10: v = slider_value_from_x(px, trX, trW, 0, 10);  if (v == static_cast<int>(cfg.blur_params_noise * 100.0)) return false; cfg.blur_params_noise = v / 100.0; return true;
      case 11: v = slider_value_from_x(px, trX, trW, 0, 200); if (v == static_cast<int>(cfg.blur_params_brightness * 100.0)) return false; cfg.blur_params_brightness = v / 100.0; return true;
      case 12: v = slider_value_from_x(px, trX, trW, 0, 200); if (v == static_cast<int>(cfg.blur_params_contrast * 100.0)) return false; cfg.blur_params_contrast = v / 100.0; return true;
      case 13: v = slider_value_from_x(px, trX, trW, 0, 200); if (v == static_cast<int>(cfg.blur_params_saturation * 100.0)) return false; cfg.blur_params_saturation = v / 100.0; return true;
      default: return false;
    }
  }

  if (st == 4) {
    int v;
    switch (si) {
      case 0:  v = slider_value_from_x(px, trX, trW, 0, 100); if (v == cfg.gappih) return false; cfg.gappih = v; return true;
      case 1:  v = slider_value_from_x(px, trX, trW, 0, 100); if (v == cfg.gappiv) return false; cfg.gappiv = v; return true;
      case 2:  v = slider_value_from_x(px, trX, trW, 0, 100); if (v == cfg.gappoh) return false; cfg.gappoh = v; return true;
      case 3:  v = slider_value_from_x(px, trX, trW, 0, 100); if (v == cfg.gappov) return false; cfg.gappov = v; return true;
      case 4: {
        int vd = slider_value_from_x(px, trX, trW, 5, 95);
        double vf = vd / 100.0;
        if (std::abs(vf - cfg.default_mfact) < 0.01) return false;
        cfg.default_mfact = vf; return true;
      }
      case 5:  v = slider_value_from_x(px, trX, trW, 1, 9); if (v == cfg.default_nmaster) return false; cfg.default_nmaster = v; return true;
      default: return false;
    }
  }

  if (st == 5) {
    int v;
    switch (si) {
      case 0:  v = slider_value_from_x(px, trX, trW, 10, 100); if (v == cfg.repeat_rate) return false; cfg.repeat_rate = v; return true;
      case 1:  v = slider_value_from_x(px, trX, trW, 100, 2000); if (v == cfg.repeat_delay) return false; cfg.repeat_delay = v; return true;
      case 2:  v = slider_value_from_x(px, trX, trW, 0, 100); if (v == cfg.swipe_min_threshold) return false; cfg.swipe_min_threshold = v; return true;
      default: return false;
    }
  }

  if (st == 6) {
    int v;
    switch (si) {
      case 0:  v = slider_value_from_x(px, trX, trW, 16, 64); if (v == cfg.cursor_size) return false; cfg.cursor_size = v; return true;
      case 1:  v = slider_value_from_x(px, trX, trW, 0, 200); if (v == cfg.snap_distance) return false; cfg.snap_distance = v; return true;
      case 2:  v = slider_value_from_x(px, trX, trW, 0, 300); if (v == cfg.axis_bind_apply_timeout) return false; cfg.axis_bind_apply_timeout = v; return true;
      case 3:  v = slider_value_from_x(px, trX, trW, 0, 60);  if (v == cfg.cursor_hide_timeout) return false; cfg.cursor_hide_timeout = v; return true;
      default: return false;
    }
  }

  if (st == 2) {
    int v;
    switch (si) {
      case 0:  v = slider_value_from_x(px, trX, trW, 50, 2000); if (v == cfg.animation_duration_move) return false; cfg.animation_duration_move = v; return true;
      case 1:  v = slider_value_from_x(px, trX, trW, 50, 2000); if (v == cfg.animation_duration_open) return false; cfg.animation_duration_open = v; return true;
      case 2:  v = slider_value_from_x(px, trX, trW, 50, 2000); if (v == cfg.animation_duration_close) return false; cfg.animation_duration_close = v; return true;
      case 3:  v = slider_value_from_x(px, trX, trW, 50, 2000); if (v == cfg.animation_duration_tag) return false; cfg.animation_duration_tag = v; return true;
      default: return false;
    }
  }

  return false;
}

// Sub-tab includes.
#include "settings/settings_tab_mango/mango_decorations.inl"
#include "settings/settings_tab_mango/mango_colors.inl"
#include "settings/settings_tab_mango/mango_animations.inl"
#include "settings/settings_tab_mango/mango_keybinds.inl"
#include "settings/settings_tab_mango/mango_layout.inl"
#include "settings/settings_tab_mango/mango_input.inl"
#include "settings/settings_tab_mango/mango_misc.inl"

void paint_mango_tab(App& app, cairo_t* cr, int contentX, int contentW,
                     double cardX, double cardW, double glassOv,
                     double dockMatA, double paintPointerYOffset, int activeSubTab) {
   
  switch (activeSubTab) {
    case 0: paint_mango_decorations(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset); break;
    case 1: paint_mango_colors(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset); break;
    case 2: paint_mango_animations(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset); break;
    case 3: paint_mango_keybinds(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset); break;
    case 4: paint_mango_layout(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset); break;
    case 5: paint_mango_input(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset); break;
    case 6: paint_mango_misc(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset); break;
  }
}

bool settings_mango_consume_pointer_down(App& app, int contentX, int contentW, int activeSubTab) {
   
  switch (activeSubTab) {
    case 0: return mango_decorations_pointer(app, contentX, contentW);
    case 1: return mango_colors_pointer(app, contentX, contentW);
    case 2: return mango_animations_pointer(app, contentX, contentW);
    case 3: return mango_keybinds_pointer(app, contentX, contentW);
    case 4: return mango_layout_pointer(app, contentX, contentW);
    case 5: return mango_input_pointer(app, contentX, contentW);
    case 6: return mango_misc_pointer(app, contentX, contentW);
    default: return false;
  }
}

void settings_clamp_mango_scroll_px(App& app) {
   
  const int mx = std::max(0, app.mangoContentBottom - kContentTop - std::max(120, app.height - kContentTop - kSpacingL));
  app.settingsMangoScrollPx = std::clamp(app.settingsMangoScrollPx, 0, mx);
}
