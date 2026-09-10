#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/settings_tab_power/settings_tab_power.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

static constexpr int kChildContentTop = kContentTop + kDockChildTabH + 12;

// Card layout constants (dock row pitch: title=36, desc=53, toggle=17).
static constexpr int kPowerRowPitch = 60;     // matching kDockVisRowPitch
static constexpr int kPowerTitleY   = 36;     // matching dock's bandTop + 36
static constexpr int kPowerDescY    = 53;     // matching dock's titleY + 17
static constexpr int kPowerToggleY  = 17;     // (60-26)/2 centered in 60px row

// Tab 0: Sleep & Power card positions.
static constexpr int kSleepCardTop = kChildContentTop;
static constexpr int kSleepCardH   = 156;     // slider at ~120, 36px gap below
static constexpr int kSleepLabelY  = 85;      // "Sleep after" label
static constexpr int kSleepSliderY = 120;     // slider track near bottom

static constexpr int kSuspendCardTop = kSleepCardTop + kSleepCardH + kCardGap;
static constexpr int kSuspendCardH   = kSleepCardH;

// Power Button + Lid Close: side-by-side grid
static constexpr int kPwrGridTop     = kSuspendCardTop + kSuspendCardH + kCardGap;
static constexpr int kPwrGridCardH   = 144;    // combo at ~104, 12px gap below

// Battery: header-only card
static constexpr int kBatteryCardTop = kPwrGridTop + kPwrGridCardH + kCardGap;
static constexpr int kBatteryCardH   = 84;     // title at 28, desc at 45, toggle centered at 29

// Tab 1: Performance card positions (dropdowns).
static constexpr int kCpuCardTop   = kChildContentTop;
static constexpr int kCpuCardH     = 210;    // header + 2 combo rows (governor + EPP)
static constexpr int kPerfRow1LblY = 80;     // "Scaling Governor" label Y
static constexpr int kPerfRow1ComboY = 100;  // governor combo Y
static constexpr int kPerfRow2LblY = 144;    // "EPP" label Y
static constexpr int kPerfRow2ComboY = 164;  // EPP combo Y

static constexpr int kTunedCardTop  = kCpuCardTop + kCpuCardH + kCardGap;
static constexpr int kTunedCardH    = 150;    // header + 1 combo row
static constexpr int kTunedRowLblY  = 80;     // "Profile" label Y
static constexpr int kTunedRowComboY = 100;   // TuneD combo Y

// Command applications.
static void apply_cpu_governor(int idx) {
  const char* gov = idx == 0 ? "performance" : "powersave";
  std::string cmd = "cpupower frequency-set -g ";
  cmd += gov;
  cmd += " 2>/dev/null";
  (void)std::system(cmd.c_str());
}

static void apply_epp(int idx) {
  static const char* eppVals[] = {
    "performance", "balance_performance", "default", "balance_power", "power"
  };
  if (idx >= 0 && idx < 5) {
    std::string cmd = "cpupower frequency-set -e ";
    cmd += eppVals[idx];
    cmd += " 2>/dev/null";
    (void)std::system(cmd.c_str());
  }
}

static void apply_tuned_profile(const std::string& profile) {
  if (!profile.empty()) {
    std::thread t([profile]() {
      std::string cmd = "tuned-adm profile ";
      cmd += profile;
      cmd += " 2>/dev/null";
      (void)std::system(cmd.c_str());
    });
    t.detach();
  }
}

// Label helpers.
static const char* power_button_action_label(int mode) {
  switch (mode) {
    case 0: return "Ask what to do";
    case 1: return "Suspend";
    case 2: return "Hibernate";
    case 3: return "Shut down";
    default: return "Ask what to do";
  }
}

static const char* lid_close_action_label(int mode) {
  switch (mode) {
    case 0: return "Do nothing";
    case 1: return "Suspend";
    case 2: return "Hibernate";
    default: return "Do nothing";
  }
}



// Combo geometry helpers.
static int grid_left_card_x(int contentX, int) {
  return contentX + 8;
}
static int grid_right_card_x(int contentX, int contentW) {
  const int cardW = contentW - 16;
  const int colW = (cardW - kCardGap) / 2;
  return contentX + 8 + colW + kCardGap;
}

void power_btn_combo_geom(int contentX, int contentW, int& bx, int& by, int& bw, int& bh) {
  bx = grid_left_card_x(contentX, contentW) + kCardPad;
  by = kPwrGridTop + 100;
  bw = 220;
  bh = 28;
}

void lid_close_combo_geom(int contentX, int contentW, int& bx, int& by, int& bw, int& bh) {
  bx = grid_right_card_x(contentX, contentW) + kCardPad;
  by = kPwrGridTop + 100;
  bw = 220;
  bh = 28;
}

static constexpr int kPerfComboW = 220;
static constexpr int kPerfComboH = 28;

void power_gov_combo_geom(int contentX, int contentW, int& bx, int& by, int& bw, int& bh) {
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;
  bx = cardX + cardW - kCardPad - kPerfComboW;
  by = kCpuCardTop + kPerfRow1ComboY;
  bw = kPerfComboW;
  bh = kPerfComboH;
}

void power_epp_combo_geom(int contentX, int contentW, int& bx, int& by, int& bw, int& bh) {
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;
  bx = cardX + cardW - kCardPad - kPerfComboW;
  by = kCpuCardTop + kPerfRow2ComboY;
  bw = kPerfComboW;
  bh = kPerfComboH;
}

void power_tuned_combo_geom(int contentX, int contentW, int& bx, int& by, int& bw, int& bh) {
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;
  bx = cardX + cardW - kCardPad - kPerfComboW;
  by = kTunedCardTop + kTunedRowComboY;
  bw = kPerfComboW;
  bh = kPerfComboH;
}

// Performance dropdown sync.
static constexpr const char* kGovLabels[] = {"Performance", "Powersave"};
static constexpr int kGovCount = 2;

static constexpr const char* kEppLabels[] = {"Performance", "Balance performance", "Default",
                                              "Balance power", "Power"};
static constexpr int kEppCount = 5;

static constexpr const char* kTunedLabels[] = {
  "balanced", "desktop", "desktop-powersave", "latency-performance",
  "throughput-performance", "network-latency", "powersave", "virtual-host"
};
static constexpr int kTunedCount = 8;

void power_gov_dd_sync(App& app, int contentX, int contentW) {
  int bx, by, bw, bh;
  power_gov_combo_geom(contentX, contentW, bx, by, bw, bh);
  app.powerGovDd.set_labels(kGovLabels, kGovCount);
  app.powerGovDd.set_selected(app.settings.powerCpuGovernor);
  app.powerGovDd.set_row_h(kSettingsDdRowH);
  app.powerGovDd.set_anchor(bx, by, bw, bh);
}

void power_epp_dd_sync(App& app, int contentX, int contentW) {
  int bx, by, bw, bh;
  power_epp_combo_geom(contentX, contentW, bx, by, bw, bh);
  app.powerEppDd.set_labels(kEppLabels, kEppCount);
  app.powerEppDd.set_selected(app.settings.powerEpp);
  app.powerEppDd.set_row_h(kSettingsDdRowH);
  app.powerEppDd.set_anchor(bx, by, bw, bh);
}

void power_tuned_dd_sync(App& app, int contentX, int contentW) {
  int bx, by, bw, bh;
  power_tuned_combo_geom(contentX, contentW, bx, by, bw, bh);
  app.powerTunedDd.set_labels(kTunedLabels, kTunedCount);
  int selIdx = 0;
  for (int i = 0; i < kTunedCount; ++i) {
    if (app.settings.powerTunedProfile == kTunedLabels[i]) { selIdx = i; break; }
  }
  app.powerTunedDd.set_selected(selIdx);
  app.powerTunedDd.set_row_h(kSettingsDdRowH);
  app.powerTunedDd.set_anchor(bx, by, bw, bh);
}

// Hit-test helpers.
static void power_close_dropdowns(App& app);

static bool power_toggle_hit(const App& app, double ly, int cardX, int cardW, int cardTop) {
  constexpr int swW = 52;
  constexpr int swH = 26;
  const int swX = cardX + cardW - swW - kCardPad;
  const int swY = cardTop + kPowerToggleY;
  return point_in_rect(app.pointerX, ly, swX, swY, swW, swH);
}

static bool power_slider_hit(const App& app, double ly, int contentX, int contentW, int cardTop) {
  const int trX = contentX + kCardPad;
  const int trW = contentW - kCardPad - kCardPad - 80;
  const int trY = cardTop + kSleepSliderY;  // slider near card bottom
  return point_in_rect(app.pointerX, ly, trX - 6, trY - 10, trW + 12, 46);
}

// Combo hit: pass absolute comboY instead of computing from cardTop+offset
static bool power_combo_hit(const App& app, double ly, int comboX, int comboY, int cbw) {
  const int cbh = 28;
  return point_in_rect(app.pointerX, ly, comboX, comboY, cbw, cbh);
}

// Child tab bar.
static void paint_power_child_tab_bar(cairo_t* cr, int contentX, int contentW,
                                       float textR, float textG, float textB,
                                       int activeTab) {
  constexpr int kTabW = 150;
  constexpr int kChildTabGap = 6;
  constexpr int kTabCount = 2;
  const int barX = contentX + 8;
  const int barY = kContentTop;
  const int barW = contentW - 16;

  m3::Box bg;
  bg.setColor(textR, textG, textB, 0.04f);
  bg.setRadius(8.0f);
  bg.setGeometry(static_cast<float>(barX), static_cast<float>(barY),
                 static_cast<float>(barW), static_cast<float>(kDockChildTabH + 6));
  bg.paint(cr);

  const int tabY = barY + 3;
  const int tabH = kDockChildTabH;
  const int tabsW = kTabCount * kTabW + (kTabCount - 1) * kChildTabGap;
  const int startX = barX + (barW - tabsW) / 2;

  static const char* kChildLabels[] = {"Sleep & Power", "Performance"};
  for (int i = 0; i < kTabCount; ++i) {
    const int tx = startX + i * (kTabW + kChildTabGap);
    const bool sel = (i == activeTab);

    if (sel) {
      m3::Box selBg;
      selBg.setColor(textR, textG, textB, 0.10f);
      selBg.setRadius(6.0f);
      selBg.setGeometry(static_cast<float>(tx), static_cast<float>(tabY),
                        static_cast<float>(kTabW), static_cast<float>(tabH));
      selBg.paint(cr);
    }

    const char* label = kChildLabels[i];
    m3::Label lbl;
    lbl.setText(label);
    lbl.setFontSize(13.0f);
    lbl.setFontWeight(sel ? 600 : 400);
    lbl.setColor(textR, textG, textB, sel ? 0.90f : 0.55f);
    float lw, lh;
    lbl.measureExtents(lw, lh);
    lbl.paintAt(cr, static_cast<float>(tx) + (static_cast<float>(kTabW) - lw) * 0.5f,
                static_cast<float>(tabY) + (static_cast<float>(tabH) - lh) * 0.5f);
  }
}

static int power_hit_child_tab(float px, float py, int contentX, int contentW) {
  constexpr int kTabW = 150;
  constexpr int kChildTabGap = 6;
  constexpr int kTabCount = 2;
  const int barX = contentX + 8;
  const int barW = contentW - 16;
  const int barY = kContentTop + 3;
  const int tabsW = kTabCount * kTabW + (kTabCount - 1) * kChildTabGap;
  const int startX = barX + (barW - tabsW) / 2;
  for (int i = 0; i < kTabCount; ++i) {
    const int tx = startX + i * (kTabW + kChildTabGap);
    if (px >= tx && px < tx + kTabW && py >= barY && py < barY + kDockChildTabH)
      return i;
  }
  return -1;
}

// Local toggle, colored by the resolved (dynamic-aware) accent.
static SwGeom power_toggle_emerald(App& app, cairo_t* cr, int cardX, int cardTop, int cardW, bool on) {
  constexpr int swW = 52, swH = 26;
  const int swX = cardX + cardW - swW - kCardPad;
  const int swY = cardTop + kPowerToggleY;
  float t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b, a_r, a_g, a_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  m3::Toggle toggle;
  toggle.setGeometry(static_cast<float>(swX), static_cast<float>(swY),
                     static_cast<float>(swW), static_cast<float>(swH));
  toggle.setOn(on);
  toggle.setAccentColor(a_r, a_g, a_b);
  toggle.setSurfaceColor(s_r, s_g, s_b);
  toggle.setTextColor(t_r, t_g, t_b);
  toggle.setOutlineColor(o_r, o_g, o_b);
  toggle.setHovered(false);
  toggle.paint(cr);
  return {swX, swY, swW, swH};
}

// Tab 0: Sleep & Power paint.
static void paint_sleep_power_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv,
                                   float textR, float textG, float textB) {
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;

  // Display Sleep.
  {
    const int ct = kSleepCardTop, ch = kSleepCardH;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(ct),
                  static_cast<double>(cardW), static_cast<double>(ch), glassOv);
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kPowerTitleY),
                       "Display Sleep", 14.f, 500, textR, textG, textB, 0.93f);
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kPowerDescY),
                       "Turn off the screen when inactive", 11.f, 400, textR, textG, textB, 0.46f);
    power_toggle_emerald(app, cr, cardX, ct, cardW, app.settings.powerDisplaySleep);

    char valBuf[32];
    std::snprintf(valBuf, sizeof(valBuf), "%d min", app.settings.powerDisplaySleepTimeout);
    settings_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kSleepLabelY),
                   "Sleep after", "", false);
    const int trX = contentX + kCardPad;
    const int trW = contentW - kCardPad - kCardPad - 80;
    settings_slider(app, cr, trX, ct + kSleepSliderY, trW,
                    app.settings.powerDisplaySleepTimeout, 1, 120, 0.0, valBuf,
                    !app.settings.powerDisplaySleep,
                    (app.sliderDrag == 10) ? app.settingsSliderDragNormT : -1.0);
  }

  // Idle Suspend.
  {
    const int ct = kSuspendCardTop, ch = kSuspendCardH;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(ct),
                  static_cast<double>(cardW), static_cast<double>(ch), glassOv);
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kPowerTitleY),
                       "Idle Suspend", 14.f, 500, textR, textG, textB, 0.93f);
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kPowerDescY),
                       "Suspend the system when idle", 11.f, 400, textR, textG, textB, 0.46f);
    power_toggle_emerald(app, cr, cardX, ct, cardW, app.settings.powerIdleSuspend);

    char valBuf[32];
    std::snprintf(valBuf, sizeof(valBuf), "%d min", app.settings.powerIdleSuspendTimeout);
    settings_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kSleepLabelY),
                   "Suspend after", "", false);
    const int trX = contentX + kCardPad;
    const int trW = contentW - kCardPad - kCardPad - 80;
    settings_slider(app, cr, trX, ct + kSleepSliderY, trW,
                    app.settings.powerIdleSuspendTimeout, 5, 240, 0.0, valBuf,
                    !app.settings.powerIdleSuspend,
                    (app.sliderDrag == 11) ? app.settingsSliderDragNormT : -1.0);
  }

  // Power Button + Lid Close (grid).
  {
    const int ct = kPwrGridTop, ch = kPwrGridCardH;
    const int colW = (cardW - kCardGap) / 2;
    const int lCardX = cardX, rCardX = cardX + colW + kCardGap;

    // Left: Power Button
    settings_card(app, cr, static_cast<double>(lCardX), static_cast<double>(ct),
                  static_cast<double>(colW), static_cast<double>(ch), glassOv);
    settings_show_text(cr, static_cast<double>(lCardX + kCardPad), static_cast<double>(ct + kPowerTitleY),
                       "Power Button", 14.f, 500, textR, textG, textB, 0.93f);
    settings_show_text(cr, static_cast<double>(lCardX + kCardPad), static_cast<double>(ct + kPowerDescY),
                       "When power button is pressed", 11.f, 400, textR, textG, textB, 0.46f);
    {
      const int cbx = lCardX + kCardPad;
      const int cby = ct + 100;
      const int cbw = 220;
      const int cbh = 28;
      settings_paint_combo_closed(app, cr, cbx, cby, cbw, cbh, glassOv,
                                   power_button_action_label(app.settings.powerPowerButtonAction), false,
                                   settings_scroll_px_int(app));
    }

    // Right: Lid Close
    settings_card(app, cr, static_cast<double>(rCardX), static_cast<double>(ct),
                  static_cast<double>(colW), static_cast<double>(ch), glassOv);
    settings_show_text(cr, static_cast<double>(rCardX + kCardPad), static_cast<double>(ct + kPowerTitleY),
                       "Lid Close", 14.f, 500, textR, textG, textB, 0.93f);
    settings_show_text(cr, static_cast<double>(rCardX + kCardPad), static_cast<double>(ct + kPowerDescY),
                       "When lid is closed", 11.f, 400, textR, textG, textB, 0.46f);
    {
      const int cbx = rCardX + kCardPad;
      const int cby = ct + 100;
      const int cbw = 220;
      const int cbh = 28;
      settings_paint_combo_closed(app, cr, cbx, cby, cbw, cbh, glassOv,
                                   lid_close_action_label(app.settings.powerLidCloseAction), false,
                                   settings_scroll_px_int(app));
    }
  }

  // Battery.
  {
    const int ct = kBatteryCardTop, ch = kBatteryCardH;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(ct),
                  static_cast<double>(cardW), static_cast<double>(ch), glassOv);
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kPowerTitleY),
                       "Show battery percentage", 14.f, 500, textR, textG, textB, 0.93f);
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kPowerDescY),
                       "In system tray and lock screen", 11.f, 400, textR, textG, textB, 0.46f);
    power_toggle_emerald(app, cr, cardX, ct, cardW, app.settings.powerShowBatteryPercentage);
  }
}

// Tab 1: Performance paint.
static void paint_performance_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv,
                                   float textR, float textG, float textB) {
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;
  const int pillAreaX = cardX + kCardPad;

  // CPU Performance.
  {
    const int ct = kCpuCardTop, ch = kCpuCardH;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(ct),
                  static_cast<double>(cardW), static_cast<double>(ch), glassOv);
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kPowerTitleY),
                       "CPU Performance", 14.f, 500, textR, textG, textB, 0.93f);
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kPowerDescY),
                       "Balance between speed and efficiency", 11.f, 400, textR, textG, textB, 0.46f);

    // Row 1: Scaling Governor combo
    settings_label(cr, static_cast<double>(pillAreaX), static_cast<double>(ct + kPerfRow1LblY),
                   "Scaling Governor", "", false);
    power_gov_dd_sync(app, contentX, contentW);
    app.powerGovDd.paint_trigger(app, cr, glassOv, settings_scroll_px_int(app));

    // Row 2: Energy Performance Preference combo
    settings_label(cr, static_cast<double>(pillAreaX), static_cast<double>(ct + kPerfRow2LblY),
                   "Energy Performance Preference", "", false);
    power_epp_dd_sync(app, contentX, contentW);
    app.powerEppDd.paint_trigger(app, cr, glassOv, settings_scroll_px_int(app));
  }

  // TuneD Profile.
  {
    const int ct = kTunedCardTop, ch = kTunedCardH;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(ct),
                  static_cast<double>(cardW), static_cast<double>(ch), glassOv);
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kPowerTitleY),
                       "System Tuning Profile", 14.f, 500, textR, textG, textB, 0.93f);
    settings_show_text(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(ct + kPowerDescY),
                       "Optimized for general desktop usage", 11.f, 400, textR, textG, textB, 0.46f);

    settings_label(cr, static_cast<double>(pillAreaX), static_cast<double>(ct + kTunedRowLblY),
                   "Profile", "", false);
    power_tuned_dd_sync(app, contentX, contentW);
    app.powerTunedDd.paint_trigger(app, cr, glassOv, settings_scroll_px_int(app));
  }
}

// Paint dispatch.
void paint_power_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  float textR, textG, textB;
  if (app.drawChromeMatugen) {
    textR = static_cast<float>(app.drawChrome.textR);
    textG = static_cast<float>(app.drawChrome.textG);
    textB = static_cast<float>(app.drawChrome.textB);
  } else {
    textR = static_cast<float>(Theme::TextR);
    textG = static_cast<float>(Theme::TextG);
    textB = static_cast<float>(Theme::TextB);
  }

  paint_power_child_tab_bar(cr, contentX, contentW, textR, textG, textB, app.powerChildTab);

  if (app.powerChildTab == 1) {
    paint_performance_tab(app, cr, contentX, contentW, glassOv, textR, textG, textB);
  } else {
    paint_sleep_power_tab(app, cr, contentX, contentW, glassOv, textR, textG, textB);
  }
}

// Tab 0: Sleep & Power pointer-down.
static bool sleep_power_pointer_down(App& app, int contentX, int contentW) {
  const double ly = app.pointerY + settings_scroll_px(app);
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;
  const int colW = (cardW - kCardGap) / 2;
  const int lCardX = cardX, rCardX = cardX + colW + kCardGap;

  // Display sleep toggle
  if (power_toggle_hit(app, ly, cardX, cardW, kSleepCardTop)) {
    app.settings.powerDisplaySleep = !app.settings.powerDisplaySleep;
    save_settings(app.settings);
    draw(app);
    return true;
  }
  // Display sleep slider
  if (app.settings.powerDisplaySleep && power_slider_hit(app, ly, contentX, contentW, kSleepCardTop)) {
    app.sliderDrag = 10;
    const int trX = contentX + kCardPad;
    const int trW = contentW - kCardPad - kCardPad - 80;
    app.settings.powerDisplaySleepTimeout = slider_value_from_x(app.pointerX, trX, trW, 1, 120);
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    draw(app);
    return true;
  }

  // Idle suspend toggle
  if (power_toggle_hit(app, ly, cardX, cardW, kSuspendCardTop)) {
    app.settings.powerIdleSuspend = !app.settings.powerIdleSuspend;
    save_settings(app.settings);
    draw(app);
    return true;
  }
  // Idle suspend slider
  if (app.settings.powerIdleSuspend && power_slider_hit(app, ly, contentX, contentW, kSuspendCardTop)) {
    app.sliderDrag = 11;
    const int trX = contentX + kCardPad;
    const int trW = contentW - kCardPad - kCardPad - 80;
    app.settings.powerIdleSuspendTimeout = slider_value_from_x(app.pointerX, trX, trW, 5, 240);
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    draw(app);
    return true;
  }

  // Power button combo (near bottom)
  {
    constexpr int cbw = 220, cbh = 28;
    const int cbx = lCardX + kCardPad;
    const int cby = kPwrGridTop + 100;
    if (power_combo_hit(app, ly, cbx, cby, cbw)) {
      app.powerBtnDropdownOpen = !app.powerBtnDropdownOpen;
      if (!app.powerBtnDropdownOpen) app.powerBtnDropdownHoverRow = -1;
      draw(app);
      return true;
    }
    // Power button dropdown selection
    if (app.powerBtnDropdownOpen) {
      static constexpr int kCount = 4;
      const int listTop = cby + cbh + 2;
      const int listH = kCount * kSettingsDdRowH;
      if (point_in_rect(app.pointerX, ly, cbx, listTop, cbw, listH)) {
        const int relY = static_cast<int>(ly) - listTop;
        const int idx = relY / kSettingsDdRowH;
        if (idx >= 0 && idx < kCount) {
          app.settings.powerPowerButtonAction = idx;
          app.powerBtnDropdownOpen = false;
          app.powerBtnDropdownHoverRow = -1;
          save_settings(app.settings);
          draw(app);
          return true;
        }
      }
      if (!point_in_rect(app.pointerX, ly, cbx, cby - 4, cbw, listTop - cby + listH + 8)) {
        app.powerBtnDropdownOpen = false;
        app.powerBtnDropdownHoverRow = -1;
        draw(app);
        return true;
      }
    }

    // Lid close combo
    const int lcbx = rCardX + kCardPad;
    if (power_combo_hit(app, ly, lcbx, cby, cbw)) {
      app.lidCloseDropdownOpen = !app.lidCloseDropdownOpen;
      if (!app.lidCloseDropdownOpen) app.lidCloseDropdownHoverRow = -1;
      draw(app);
      return true;
    }
    // Lid close dropdown selection
    if (app.lidCloseDropdownOpen) {
      static constexpr int kCount = 3;
      const int listTop = cby + cbh + 2;
      const int listH = kCount * kSettingsDdRowH;
      if (point_in_rect(app.pointerX, ly, lcbx, listTop, cbw, listH)) {
        const int relY = static_cast<int>(ly) - listTop;
        const int idx = relY / kSettingsDdRowH;
        if (idx >= 0 && idx < kCount) {
          app.settings.powerLidCloseAction = idx;
          app.lidCloseDropdownOpen = false;
          app.lidCloseDropdownHoverRow = -1;
          save_settings(app.settings);
          draw(app);
          return true;
        }
      }
      if (!point_in_rect(app.pointerX, ly, lcbx, cby - 4, cbw, listTop - cby + listH + 8)) {
        app.lidCloseDropdownOpen = false;
        app.lidCloseDropdownHoverRow = -1;
        draw(app);
        return true;
      }
    }
  }

  // Battery percentage toggle
  if (power_toggle_hit(app, ly, cardX, cardW, kBatteryCardTop)) {
    app.settings.powerShowBatteryPercentage = !app.settings.powerShowBatteryPercentage;
    save_settings(app.settings);
    draw(app);
    return true;
  }

  return false;
}

// Tab 1: Performance pointer-down.
static bool performance_pointer_down(App& app, int contentX, int contentW) {
  const int scr = settings_scroll_px_int(app);

  // Governor combo trigger
  {
    power_gov_dd_sync(app, contentX, contentW);
    if (app.powerGovDd.hit_trigger(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr)) {
      power_close_dropdowns(app);
      app.powerGovDd.open_popup();
      draw(app);
      return true;
    }
  }

  // EPP combo trigger
  {
    power_epp_dd_sync(app, contentX, contentW);
    if (app.powerEppDd.hit_trigger(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr)) {
      power_close_dropdowns(app);
      app.powerEppDd.open_popup();
      draw(app);
      return true;
    }
  }

  // TuneD combo trigger
  {
    power_tuned_dd_sync(app, contentX, contentW);
    if (app.powerTunedDd.hit_trigger(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr)) {
      power_close_dropdowns(app);
      app.powerTunedDd.open_popup();
      draw(app);
      return true;
    }
  }

  return false;
}

static void power_close_dropdowns(App& app) {
  app.powerBtnDropdownOpen = false;
  app.powerBtnDropdownHoverRow = -1;
  app.lidCloseDropdownOpen = false;
  app.lidCloseDropdownHoverRow = -1;
  app.powerGovDd.close();
  app.powerEppDd.close();
  app.powerTunedDd.close();
}

bool settings_power_consume_performance_dropdown_pointer_up(App& app, int contentX, int contentW) {
  if (app.powerChildTab != 1) return false;
  const int scr = settings_scroll_px_int(app);
  bool closed = false;

  if (app.powerGovDd.open()) {
    power_gov_dd_sync(app, contentX, contentW);
    const int rr = app.powerGovDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr,
                                          app.width, app.height);
    if (rr >= 0 && rr < kGovCount) {
      app.settings.powerCpuGovernor = rr;
      apply_cpu_governor(rr);
      save_settings(app.settings);
    }
    closed = true;
  }

  if (app.powerEppDd.open()) {
    power_epp_dd_sync(app, contentX, contentW);
    const int rr = app.powerEppDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr,
                                          app.width, app.height);
    if (rr >= 0 && rr < kEppCount) {
      app.settings.powerEpp = rr;
      apply_epp(rr);
      save_settings(app.settings);
    }
    closed = true;
  }

  if (app.powerTunedDd.open()) {
    power_tuned_dd_sync(app, contentX, contentW);
    const int rr = app.powerTunedDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr,
                                            app.width, app.height);
    if (rr >= 0 && rr < kTunedCount) {
      app.settings.powerTunedProfile = kTunedLabels[rr];
      apply_tuned_profile(kTunedLabels[rr]);
      save_settings(app.settings);
    }
    closed = true;
  }

  if (closed) {
    power_close_dropdowns(app);
    draw(app);
    return true;
  }
  return false;
}

// Pointer-down dispatch.
bool settings_power_consume_pointer_down(App& app, int contentX, int contentW) {
  const double ly = app.pointerY + settings_scroll_px(app);

  // Child tab switch
  const int tabHit = power_hit_child_tab(static_cast<float>(app.pointerX), static_cast<float>(ly), contentX, contentW);
  if (tabHit >= 0 && tabHit != app.powerChildTab) {
    power_close_dropdowns(app);
    app.powerChildTab = tabHit;
    draw(app);
    return true;
  }

  if (app.powerChildTab == 1) {
    return performance_pointer_down(app, contentX, contentW);
  }
  return sleep_power_pointer_down(app, contentX, contentW);
}
