#include <cairo/cairo.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>

#include "services/bing/bing_wallpaper.hpp"
#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_serialize.hpp"
#include "ux/settings/settings_tab_bing/settings_tab_bing.hpp"

extern void draw(App& app);

static const char* kMonthNames[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

// Shared geometry.
static constexpr int kRowH    = 60;
static constexpr int kBtnH    = 28;
static constexpr int kBtnW    = 120;
static constexpr int kComboW  = 130;
static constexpr int kComboH  = 28;
static constexpr int kCbSize  = 18;

static constexpr int kTitleY  = 36;   // card title Y from card top
static constexpr int kDescY   = 53;   // card description Y from card top
static constexpr int kToggleY = 17;   // toggle top Y from card top (centered in 60px band)

// Card 1: General.
static constexpr int kGenCardTop = kContentTop;
static constexpr int kGenLabelY  = 100;
static constexpr int kGenPathRow = 124;
static constexpr int kGenCardH   = 168;  // 124 + btnH + 16

// Card 2: Daily.
static constexpr int kDailyCardTop = kGenCardTop + kGenCardH + kCardGap;
static constexpr int kDailyBtnRow  = 100;
static constexpr int kDailyFileRow = 136;
static constexpr int kDailyCardH   = 166;  // 136 + textH + 16

// Card 3: Updates.
static constexpr int kUpdCardTop = kDailyCardTop + kDailyCardH + kCardGap;
static constexpr int kUpdInfoRow = 96;
static constexpr int kUpdBtnRow  = 120;
static constexpr int kUpdCardH   = 164;  // 120 + btnH + 16

// Card 4: Archive.
static constexpr int kArchCardTop = kUpdCardTop + kUpdCardH + kCardGap;
static constexpr int kArchComboY   = 118;
static constexpr int kArchCbRow    = 150;
static constexpr int kArchDlRow    = 194;
static constexpr int kArchCardH    = 240;

// Card 5: Status (conditional).
static constexpr int kStatusCardH  = 140;
static int status_card_top() { return kArchCardTop + kArchCardH + kCardGap; }

// Card 6: Blocked Keywords.
static constexpr int kBlockCardH   = 120;

// Toggle geometry, mirrors settings_toggle() positioning.
static void toggle_geom(int cx, int cw, int cardTop, int& outX, int& outY, int& outW, int& outH) {
  constexpr int swW = 52, swH = 26;
  outX = cx + cw - swW - kSpacingXL;
  outY = cardTop + kToggleY;
  outW = swW;
  outH = swH;
}

// Blocklist sync helper.
static std::string s_lastBlocklist;
static void sync_blocklist(App& app) {
  auto& svc = eh::bing::BingWallpaperService::instance();
  if (app.settings.bingBlockedKeywords == s_lastBlocklist) return;
  s_lastBlocklist = app.settings.bingBlockedKeywords;
  std::vector<std::string> kw;
  const char* p = app.settings.bingBlockedKeywords.c_str();
  while (*p) {
    while (*p == ' ' || *p == ',') ++p;
    if (!*p) break;
    const char* start = p;
    while (*p && *p != ',') ++p;
    kw.emplace_back(start, static_cast<size_t>(p - start));
  }
  svc.set_blocklist(kw);
}

// ═══════════════════════════════════════════════════════════════════════════
// paint_bing_tab
// ═══════════════════════════════════════════════════════════════════════════
void paint_bing_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  
  auto& bingSvc = eh::bing::BingWallpaperService::instance();
  bingSvc.drain_wake();
  sync_blocklist(app);

  if (auto bp = bingSvc.take_progress_result()) {
    app.bingProgressCurrent      = bp->current;
    app.bingProgressTotal        = bp->total;
    app.bingFoundInArchive       = bp->found;
    if (!bp->newest_available_date.empty()) app.bingNewestAvailableDate = bp->newest_available_date;
    if (!bp->last_downloaded_filename.empty()) app.bingLastDownloadedFilename = bp->last_downloaded_filename;
    app.bingIsDownloading        = true;
  }
  if (auto dd = bingSvc.take_done_result()) {
    app.bingDownloadedCount  = dd->downloaded;
    app.bingSkippedCount     = dd->skipped;
    app.bingFailedCount      = dd->failed;
    app.bingIsDownloading    = false;
    app.bingIsCheckingUpdates= false;
    if (dd->ok && dd->downloaded > 0) {
      app.bingStatusText = "Downloaded " + std::to_string(dd->downloaded) + " wallpaper" + (dd->downloaded != 1 ? "s" : "");
      app.bingStatusType = "success";
    } else if (dd->failed > 0) {
      app.bingStatusText = std::to_string(dd->failed) + " download" + (dd->failed != 1 ? "s" : "") + " failed";
      app.bingStatusType = "error";
    } else if (dd->skipped > 0) {
      app.bingStatusText = "All " + std::to_string(dd->skipped) + " already downloaded";
      app.bingStatusType = "success";
    } else {
      app.bingStatusText = "Nothing to download";
      app.bingStatusType = "info";
    }
  }
  if (auto uc = bingSvc.take_updates_result()) {
    app.bingIsCheckingUpdates = false;
    app.bingUpdateCount       = uc->new_count;
    app.bingHasUpdates        = uc->new_count > 0;
    if (uc->new_count > 0) {
      app.bingStatusText = std::to_string(uc->new_count) + " new wallpaper" + (uc->new_count != 1 ? "s" : "") + " available";
      app.bingStatusType = "info";
    } else {
      app.bingStatusText = "Already up to date";
      app.bingStatusType = "success";
    }
  }
  if (auto dl = bingSvc.take_daily_result()) {
    app.bingIsFetchingDaily = false;
    if (dl->ok) {
      app.bingDailyWallpaperPath = dl->local_path;
      app.bingStatusText = "Daily wallpaper: " + dl->local_path.substr(dl->local_path.rfind('/') + 1);
      app.bingStatusType = "success";
      if (app.settings.bingDailyEnabled) {
        app.settings.wallpaperImage = dl->local_path;
        app.settings.wallpaperEnabled = true;
        save_settings(app.settings);
        wallpaper_apply_if_digest_changed(app.settings);
      }
    } else {
      app.bingStatusText = "Daily wallpaper failed: " + dl->error_msg;
      app.bingStatusType = "error";
    }
  }

  cairo_save(cr);
  cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                  static_cast<double>(contentW),
                  static_cast<double>(app.height - kContentTop - kSpacingL));
  cairo_clip(cr);

  const double pyBing  = app.pointerY;
  const double cardX   = static_cast<double>(contentX + 8);
  const double cardW   = static_cast<double>(contentW - 16);
  const int    bingCX  = contentX + 8;
  const int    bingCW  = contentW - 16;
  const int    kBingL  = bingCX + kCardPad;
  const int    kBingW  = bingCW - kCardPad * 2;

  // resolve theme colours
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  auto paint_btn = [&](int bx, int by, int bw, int bh, const char* label, bool disabled) {
    m3::Button btn;
    btn.setMinSize(0, 0);
    btn.setLabel(label);
    btn.setGeometry(static_cast<float>(bx), static_cast<float>(by),
                    static_cast<float>(bw), static_cast<float>(bh));
    btn.setStyle(m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    btn.setEnabled(!disabled);
    btn.setAccentColor(a_r, a_g, a_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    btn.setPointer(static_cast<float>(app.pointerX), static_cast<float>(pyBing));
    btn.paint(cr);
  };
  auto paint_small_btn = [&](int bx, int by, const char* label, bool disabled) {
    paint_btn(bx, by, kBtnW, kBtnH, label, disabled);
  };

  const bool busy = app.bingIsCheckingUpdates || app.bingIsDownloading || app.bingIsFetchingDaily;

  // ═════════════════════════════════════════════════════════════════════════
  // CARD 1 — General
  // ═════════════════════════════════════════════════════════════════════════
  {
    const int ct = kGenCardTop;
    settings_card(app, cr, cardX, static_cast<double>(ct), cardW, static_cast<double>(kGenCardH), glassOv);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kTitleY),
                       "General", 14.f, 500, t_r, t_g, t_b, 0.93f);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kDescY),
                       "Download wallpapers from the Bing archive", 11.f, 400,
                       t_r, t_g, t_b, 0.46f);

    {
      int tgX, tgY, tgW, tgH;
      toggle_geom(bingCX, bingCW, ct, tgX, tgY, tgW, tgH);
      settings_toggle(app, cr, bingCX, ct + kToggleY, bingCW,
                      static_cast<double>(tgY), static_cast<double>(tgH),
                      app.settings.bingEnabled, 0.0);
    }

    settings_label(cr, static_cast<double>(kBingL), static_cast<double>(ct + kGenLabelY),
                   "Save location", nullptr);
    const int pathBY = ct + kGenPathRow;
    const int pathFW = kBingW - kBtnW - kSpacingM;
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
      box.setColor(r, g, b, 0.30f);
      box.setRadius(6.f);
      box.setGeometry(static_cast<float>(kBingL), static_cast<float>(pathBY),
                      static_cast<float>(pathFW), static_cast<float>(kBtnH));
      box.paint(cr);
    }
    const std::string& pp = app.settings.bingDownloadPath;
    std::string dispStr = pp.empty() ? "~/Pictures/BingWallpaper" : (pp.size() > 38 ? pp.substr(0, 35) + "..." : pp);
    {
      auto* layout = pango_cairo_create_layout(cr);
      auto* desc = pango_font_description_new();
      pango_font_description_set_family(desc, "Inter");
      pango_font_description_set_size(desc, static_cast<int>(11.0f * PANGO_SCALE));
      pango_layout_set_font_description(layout, desc);
      pango_layout_set_text(layout, dispStr.c_str(), -1);
      int tw, th;
      pango_layout_get_pixel_size(layout, &tw, &th);
      cairo_save(cr);
      cairo_translate(cr, static_cast<double>(kBingL + kSpacingS),
                      static_cast<double>(pathBY + (kBtnH - th) / 2));
      cairo_set_source_rgba(cr, t_r, t_g, t_b, 1.0f);
      pango_cairo_show_layout(cr, layout);
      cairo_restore(cr);
      pango_font_description_free(desc);
      g_object_unref(layout);
    }
    paint_small_btn(kBingL + pathFW + kSpacingM, pathBY, "Browse", false);
  }
  // bench_.section("card1");

  // ═════════════════════════════════════════════════════════════════════════
  // CARD 2 — Daily Wallpaper
  // ═════════════════════════════════════════════════════════════════════════
  {
    const int ct = kDailyCardTop;
    settings_card(app, cr, cardX, static_cast<double>(ct), cardW, static_cast<double>(kDailyCardH), glassOv);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kTitleY),
                       "Daily Wallpaper", 14.f, 500, t_r, t_g, t_b, 0.93f);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kDescY),
                       "Automatically fetch and set today's Bing wallpaper", 11.f, 400,
                       t_r, t_g, t_b, 0.46f);

    {
      int tgX, tgY, tgW, tgH;
      toggle_geom(bingCX, bingCW, ct, tgX, tgY, tgW, tgH);
      settings_toggle(app, cr, bingCX, ct + kToggleY, bingCW,
                      static_cast<double>(tgY), static_cast<double>(tgH),
                      app.settings.bingDailyEnabled, 0.0);
    }

    const int fetchBtnX = kBingL + kBingW - kBtnW;
    paint_small_btn(fetchBtnX, ct + kDailyBtnRow,
                    app.bingIsFetchingDaily ? "Fetching..." : "Fetch Now",
                    app.bingIsFetchingDaily);

    if (!app.bingDailyWallpaperPath.empty() && !app.bingIsFetchingDaily) {
      std::string fname = app.bingDailyWallpaperPath;
      auto sl = fname.rfind('/');
      if (sl != std::string::npos) fname = fname.substr(sl + 1);
      settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kDailyFileRow),
                         fname.c_str(), 10.f, 400,
                         t_r, t_g, t_b, 0.70f);
    }
  }
  // bench_.section("card2");

  // ═════════════════════════════════════════════════════════════════════════
  // CARD 3 — Updates
  // ═════════════════════════════════════════════════════════════════════════
  {
    const int ct = kUpdCardTop;
    settings_card(app, cr, cardX, static_cast<double>(ct), cardW, static_cast<double>(kUpdCardH), glassOv);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kTitleY),
                       "Updates", 14.f, 500, t_r, t_g, t_b, 0.93f);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kDescY),
                       "Check for new wallpapers", 11.f, 400,
                       t_r, t_g, t_b, 0.46f);

    if (app.bingIsCheckingUpdates) {
      settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kUpdInfoRow),
                         "Checking for updates...", 12.f, 400,
                         t_r, t_g, t_b, 0.80f);
    } else if (app.bingHasUpdates) {
      char ub[64];
      std::snprintf(ub, sizeof(ub), "%d new wallpaper%s available",
                    app.bingUpdateCount, app.bingUpdateCount != 1 ? "s" : "");
      settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kUpdInfoRow),
                         ub, 12.f, 400, 0.40f, 0.85f, 0.40f, 0.85f);
    }

    paint_small_btn(kBingL + kBingW - kBtnW, ct + kUpdBtnRow, "Check Now", busy);
    if (app.bingHasUpdates) {
      paint_small_btn(kBingL + kBingW - kBtnW * 2 - kSpacingM, ct + kUpdBtnRow, "Download All", busy);
    }
  }
  // bench_.section("card3");

  // ═════════════════════════════════════════════════════════════════════════
  // CARD 4 — Archive Download
  // ═════════════════════════════════════════════════════════════════════════
  {
    const int ct = kArchCardTop;
    settings_card(app, cr, cardX, static_cast<double>(ct), cardW, static_cast<double>(kArchCardH), glassOv);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kTitleY),
                       "Archive Download", 14.f, 500, t_r, t_g, t_b, 0.93f);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(ct + kDescY),
                       "Download wallpapers from past months", 11.f, 400,
                       t_r, t_g, t_b, 0.46f);

    // Year combo
    settings_label(cr, static_cast<double>(kBingL), static_cast<double>(ct + 92), "Year", nullptr);
    const int yearComboX = kBingL;
    const int yearComboY = ct + kArchComboY;
    char yearLabel[8];
    std::snprintf(yearLabel, sizeof(yearLabel), "%d", app.bingSelectedYear);
    settings_paint_combo_closed(app, cr, yearComboX, yearComboY, kComboW, kComboH, glassOv,
                                yearLabel, app.bingYearDropdownOpen, 0);
    if (app.bingYearDropdownOpen) {
      for (int y = 0; y < 13; ++y) {
        const int yy = 2026 - y;
        const int fry = yearComboY + kComboH + 2 + y * kSettingsDdRowH;
        const bool fh = point_in_rect(app.pointerX, pyBing, yearComboX, fry, kComboW, kSettingsDdRowH);
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
          box.setColor(r, g, b, fh ? 0.60f : 0.45f);
          box.setRadius(4.f);
          box.setGeometry(static_cast<float>(yearComboX), static_cast<float>(fry),
                          static_cast<float>(kComboW), static_cast<float>(kSettingsDdRowH));
          box.paint(cr);
        }
        char yb[8];
        std::snprintf(yb, sizeof(yb), "%d", yy);
        settings_show_text(cr, static_cast<double>(yearComboX + kSpacingS),
                           static_cast<double>(fry + kSettingsDdRowH / 2),
                           yb, 12.f, 400,
                           t_r, t_g, t_b, 1.0f);
      }
    }

    // Month combo
    const int monthComboX = kBingL + kComboW + kSpacingXL;
    settings_label(cr, static_cast<double>(monthComboX), static_cast<double>(ct + 92), "Month", nullptr);
    const int monthComboY = ct + kArchComboY;
    settings_paint_combo_closed(app, cr, monthComboX, monthComboY, kComboW, kComboH, glassOv,
                                kMonthNames[std::clamp(app.bingSelectedMonth - 1, 0, 11)],
                                app.bingMonthDropdownOpen, 0);
    if (app.bingMonthDropdownOpen) {
      for (int mi = 0; mi < 12; ++mi) {
        const int fry = monthComboY + kComboH + 2 + mi * kSettingsDdRowH;
        const bool fh = point_in_rect(app.pointerX, pyBing, monthComboX, fry, kComboW, kSettingsDdRowH);
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
          box.setColor(r, g, b, fh ? 0.60f : 0.45f);
          box.setRadius(4.f);
          box.setGeometry(static_cast<float>(monthComboX), static_cast<float>(fry),
                          static_cast<float>(kComboW), static_cast<float>(kSettingsDdRowH));
          box.paint(cr);
        }
        settings_show_text(cr, static_cast<double>(monthComboX + kSpacingS),
                           static_cast<double>(fry + kSettingsDdRowH / 2),
                           kMonthNames[mi], 12.f, 400,
                           t_r, t_g, t_b, 1.0f);
      }
    }

    // Download button
    {
      char dlb[64];
      std::snprintf(dlb, sizeof(dlb), "%s %d",
                    kMonthNames[std::clamp(app.bingSelectedMonth - 1, 0, 11)],
                    app.bingSelectedYear);
      const int dlBtnW = 180;
      paint_btn(kBingL + kBingW - dlBtnW, ct + kArchDlRow, dlBtnW, kBtnH, dlb, busy);
    }
  }
  // bench_.section("card4");

  // ═════════════════════════════════════════════════════════════════════════
  // CARD 5 — Status (conditional)
  // ═════════════════════════════════════════════════════════════════════════
  if (app.bingIsDownloading || app.bingIsCheckingUpdates || app.bingIsFetchingDaily ||
      !app.bingStatusText.empty()) {
    const int st = status_card_top();
    settings_card(app, cr, cardX, static_cast<double>(st), cardW, static_cast<double>(kStatusCardH), glassOv);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(st + kTitleY),
                       "Status", 14.f, 500, t_r, t_g, t_b, 0.93f);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(st + kDescY),
                       "Current activity and results", 11.f, 400,
                       t_r, t_g, t_b, 0.46f);

    const int sLabelY = st + 80;
    if (!app.bingStatusText.empty()) {
      float sr = t_r, sg = t_g, sb = t_b;
      float sa = 1.0f;
      if (app.bingStatusType == "error") { sr = 0.90f; sg = 0.30f; sb = 0.30f; sa = 0.85f; }
      else if (app.bingStatusType == "success") { sr = 0.40f; sg = 0.85f; sb = 0.40f; sa = 0.85f; }
      settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(sLabelY),
                         app.bingStatusText.c_str(), 11.f, 400, sr, sg, sb, sa);
    } else if (app.bingIsCheckingUpdates) {
      settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(sLabelY),
                         "Checking for updates...", 11.f, 400,
                         t_r, t_g, t_b, 1.0f);
    } else if (app.bingIsFetchingDaily) {
      settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(sLabelY),
                         "Fetching daily wallpaper...", 11.f, 400,
                         t_r, t_g, t_b, 1.0f);
    }

    if (app.bingIsDownloading) {
      const double pbarY = static_cast<double>(sLabelY) + kSpacingM + 4;
      const double pbarW = static_cast<double>(kBingW) - kCardPad * 2;
      const double pbarH = 8;
      char pct[64];
      if (app.bingProgressTotal > 0) {
        std::snprintf(pct, sizeof(pct), "%d / %d  (%d%%)",
                      app.bingProgressCurrent, app.bingProgressTotal,
                      static_cast<int>(static_cast<double>(app.bingProgressCurrent) / app.bingProgressTotal * 100.0));
      } else {
        std::snprintf(pct, sizeof(pct), "Fetching list...");
      }
      settings_show_text(cr, static_cast<double>(kBingL), pbarY - kSpacingS,
                         pct, 10.f, 400,
                         t_r, t_g, t_b, 1.0f);

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
        box.setColor(r, g, b, 0.20f);
        box.setRadius(static_cast<float>(pbarH * 0.5));
        box.setGeometry(static_cast<float>(kBingL), static_cast<float>(pbarY),
                        static_cast<float>(pbarW), static_cast<float>(pbarH));
        box.paint(cr);
      }

      float pr = a_r, pg = a_g, pb = a_b;
      if (app.bingProgressTotal > 0) {
        const double fill = std::clamp(static_cast<double>(app.bingProgressCurrent) / app.bingProgressTotal, 0.0, 1.0);
        if (fill > 0.0) {
          m3::Box box;
          box.setColor(pr, pg, pb, 0.70f);
          box.setRadius(static_cast<float>(pbarH * 0.5));
          box.setGeometry(static_cast<float>(kBingL), static_cast<float>(pbarY),
                          static_cast<float>(pbarW * fill), static_cast<float>(pbarH));
          box.paint(cr);
        }
      } else {
        m3::Box box;
        box.setColor(pr, pg, pb, 0.50f);
        box.setRadius(static_cast<float>(pbarH * 0.5));
        box.setGeometry(static_cast<float>(kBingL), static_cast<float>(pbarY),
                        static_cast<float>(pbarW * 0.3), static_cast<float>(pbarH));
        box.paint(cr);
      }

      if (!app.bingLastDownloadedFilename.empty()) {
        settings_show_text(cr, static_cast<double>(kBingL), pbarY + pbarH + kSpacingM + 4,
                           app.bingLastDownloadedFilename.c_str(), 9.f, 400,
                           t_r, t_g, t_b, 0.70f);
      }
    }
  }
  // bench_.section("card5");

  // ═════════════════════════════════════════════════════════════════════════
  // CARD 6 — Blocked Keywords
  // ═════════════════════════════════════════════════════════════════════════
  {
    const int blockTop = (app.bingIsDownloading || app.bingIsCheckingUpdates ||
                          app.bingIsFetchingDaily || !app.bingStatusText.empty())
                         ? status_card_top() + kStatusCardH + kCardGap
                         : kArchCardTop + kArchCardH + kCardGap;
    settings_card(app, cr, cardX, static_cast<double>(blockTop), cardW, static_cast<double>(kBlockCardH), glassOv);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(blockTop + kTitleY),
                       "Blocked Keywords", 14.f, 500, t_r, t_g, t_b, 0.93f);
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(blockTop + kDescY),
                       "Wallpapers matching these words will be skipped", 11.f, 400,
                       t_r, t_g, t_b, 0.46f);

    const std::string& kw = app.settings.bingBlockedKeywords;
    std::string display;
    if (kw.empty()) {
      display = "(none)";
    } else if (app.bingShowBlockedKeywords) {
      display = kw;
    } else {
      display.assign(kw.size(), '*');
    }
    settings_show_text(cr, static_cast<double>(kBingL), static_cast<double>(blockTop + 80),
                       display.c_str(), 11.f, kBingW - kBtnW * 2 - kSpacingM * 2,
                       t_r, t_g, t_b, 1.0f);

    paint_small_btn(kBingL + kBingW - kBtnW * 2 - kSpacingM, blockTop + 74,
                    app.bingShowBlockedKeywords ? "Hide" : "Show", false);
    paint_small_btn(kBingL + kBingW - kBtnW, blockTop + 74, "Edit...", false);
  }
  // bench_.section("card6");

  cairo_restore(cr);
}

// ═══════════════════════════════════════════════════════════════════════════
// handle_bing_click
// ═══════════════════════════════════════════════════════════════════════════
void handle_bing_click(App& app, int contentX, int contentW) {
  
  auto& bingSvc = eh::bing::BingWallpaperService::instance();
  const bool busy = app.bingIsCheckingUpdates || app.bingIsDownloading || app.bingIsFetchingDaily;
  const int bingCX = contentX + 8;
  const int bingCW = contentW - 16;
  const int kBingL = bingCX + kCardPad;
  const int kBingW = bingCW - kCardPad * 2;

  auto btn_hit = [&](int bx, int by, int bw, int bh) {
    return app.pointerX >= bx && app.pointerX < bx + bw &&
           app.pointerY >= by && app.pointerY < by + bh;
  };

  // Card 1: General.
  {
    const int ct = kGenCardTop;
    int tgX, tgY, tgW, tgH;
    toggle_geom(bingCX, bingCW, ct, tgX, tgY, tgW, tgH);
    if (point_in_rect(app.pointerX, app.pointerY, tgX, tgY, tgW, tgH)) {
      app.settings.bingEnabled = !app.settings.bingEnabled;
      draw(app);
      return;
    }
    // Browse button
    const int pathFW = kBingW - kBtnW - kSpacingM;
    if (btn_hit(kBingL + pathFW + kSpacingM, ct + kGenPathRow, kBtnW, kBtnH)) {
      app.bingFolderPickerOpen = true;
      draw(app);
      return;
    }
  }

  // Card 2: Daily.
  {
    const int ct = kDailyCardTop;
    int tgX, tgY, tgW, tgH;
    toggle_geom(bingCX, bingCW, ct, tgX, tgY, tgW, tgH);
    if (point_in_rect(app.pointerX, app.pointerY, tgX, tgY, tgW, tgH)) {
      app.settings.bingDailyEnabled = !app.settings.bingDailyEnabled;
      if (app.settings.bingDailyEnabled) {
        app.bingIsFetchingDaily = true;
        app.bingStatusText = "Fetching daily wallpaper...";
        app.bingStatusType = "idle";
        bingSvc.daily_async(app.settings.bingDownloadPath, false);
      }
      draw(app);
      return;
    }
    // Fetch Now button
    const int fetchBtnX = kBingL + kBingW - kBtnW;
    if (!app.bingIsFetchingDaily && btn_hit(fetchBtnX, ct + kDailyBtnRow, kBtnW, kBtnH)) {
      app.bingIsFetchingDaily = true;
      app.bingStatusText = "Fetching daily wallpaper...";
      app.bingStatusType = "idle";
      bingSvc.daily_async(app.settings.bingDownloadPath, true);
      draw(app);
      return;
    }
  }

  // Card 3: Updates.
  {
    const int ct = kUpdCardTop;
    if (!busy) {
      if (btn_hit(kBingL + kBingW - kBtnW, ct + kUpdBtnRow, kBtnW, kBtnH)) {
        app.bingIsCheckingUpdates = true;
        app.bingStatusText = "Checking for updates...";
        app.bingStatusType = "idle";
        bingSvc.check_updates_async(app.settings.bingDownloadPath);
        draw(app);
        return;
      }
      if (app.bingHasUpdates && btn_hit(kBingL + kBingW - kBtnW * 2 - kSpacingM, ct + kUpdBtnRow, kBtnW, kBtnH)) {
        app.bingIsDownloading = true;
        app.bingProgressCurrent = 0;
        app.bingProgressTotal = 0;
        bingSvc.download_async(app.settings.bingDownloadPath, eh::bing::Filter::All, "");
        draw(app);
        return;
      }
    }
  }

  // Card 6: Blocked Keywords.
  {
    const int blockTop = (app.bingIsDownloading || app.bingIsCheckingUpdates ||
                          app.bingIsFetchingDaily || !app.bingStatusText.empty())
                         ? status_card_top() + kStatusCardH + kCardGap
                         : kArchCardTop + kArchCardH + kCardGap;
    if (btn_hit(kBingL + kBingW - kBtnW * 2 - kSpacingM, blockTop + 74, kBtnW, kBtnH)) {
      app.bingShowBlockedKeywords = !app.bingShowBlockedKeywords;
      draw(app);
      return;
    }
    if (btn_hit(kBingL + kBingW - kBtnW, blockTop + 74, kBtnW, kBtnH)) {
      std::string result;
      int exitCode = -1;
      {
        std::string cmd = "zenity --entry --title='Blocked Keywords' "
                          "--text='Enter keywords to block, comma-separated:' "
                          "--entry-text='" + app.settings.bingBlockedKeywords + "' 2>/dev/null";
        FILE* fp = popen(cmd.c_str(), "r");
        if (fp) {
          char buf[4096] = {};
          if (fgets(buf, (int)sizeof(buf), fp)) result.assign(buf);
          exitCode = pclose(fp);
        }
      }
      if (exitCode == 0) {
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
        app.settings.bingBlockedKeywords = std::move(result);
      }
      draw(app);
      return;
    }
  }

  // Card 4: Archive Download.
  {
    const int ct = kArchCardTop;

    // Year dropdown trigger
    const int yearComboX = kBingL;
    const int yearComboY = ct + kArchComboY;
    if (point_in_rect(app.pointerX, app.pointerY, yearComboX, yearComboY, kComboW, kComboH)) {
      app.bingYearDropdownOpen = !app.bingYearDropdownOpen;
      app.bingMonthDropdownOpen = false;
      draw(app);
      return;
    }
    if (app.bingYearDropdownOpen) {
      for (int y = 0; y < 13; ++y) {
        const int yy = 2026 - y;
        const int fry = yearComboY + kComboH + 2 + y * kSettingsDdRowH;
        if (point_in_rect(app.pointerX, app.pointerY, yearComboX, fry, kComboW, kSettingsDdRowH)) {
          app.bingSelectedYear = yy;
          app.bingYearDropdownOpen = false;
          draw(app);
          return;
        }
      }
    }

    // Month dropdown trigger
    const int monthComboX = kBingL + kComboW + kSpacingXL;
    const int monthComboY = ct + kArchComboY;
    if (point_in_rect(app.pointerX, app.pointerY, monthComboX, monthComboY, kComboW, kComboH)) {
      app.bingMonthDropdownOpen = !app.bingMonthDropdownOpen;
      app.bingYearDropdownOpen = false;
      draw(app);
      return;
    }
    if (app.bingMonthDropdownOpen) {
      for (int mi = 0; mi < 12; ++mi) {
        const int fry = monthComboY + kComboH + 2 + mi * kSettingsDdRowH;
        if (point_in_rect(app.pointerX, app.pointerY, monthComboX, fry, kComboW, kSettingsDdRowH)) {
          app.bingSelectedMonth = mi + 1;
          app.bingMonthDropdownOpen = false;
          draw(app);
          return;
        }
      }
    }

    // Download button
    if (!busy) {
      const int dlBtnW = 180;
      if (btn_hit(kBingL + kBingW - dlBtnW, ct + kArchDlRow, dlBtnW, kBtnH)) {
        char monthStr[8];
        std::snprintf(monthStr, sizeof(monthStr), "%04d-%02d",
                      app.bingSelectedYear, app.bingSelectedMonth);
        app.bingIsDownloading = true;
        bingSvc.download_async(app.settings.bingDownloadPath, eh::bing::Filter::Custom, monthStr);
        app.bingProgressCurrent = 0;
        app.bingProgressTotal = 0;
        draw(app);
        return;
      }
    }
  }
}
