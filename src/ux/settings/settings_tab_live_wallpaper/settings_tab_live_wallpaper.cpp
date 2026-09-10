#include <cairo/cairo.h>
#include "m3/core/primitives/box.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/settings_tab_wallpaper/settings_tab_wallpaper.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "configuration/shell_config.hpp"
#include "wallpaper/apply/wallpaper_apply.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail_service.hpp"

extern void draw(App& app);
extern void save_settings(const struct Settings& s);
extern Settings load_settings();
extern void settings_close_mode_dropdowns(App& app);
extern void wallpaper_apply_if_digest_changed(const struct Settings& s);
extern void settings_paint_combo_list_popup(App& app, cairo_t* cr, int x, int y, int w, int rowH, int rowCount,
                                            const char* const* labels, int selectedIdx, int hoverRow, double glassOv,
                                            int scrollPx = 0, int viewPortH = 0, bool opaquePanel = false);

static constexpr int kWpSubTabH = 48;
static constexpr int kWpHeroTopGap = 8;
static constexpr int kWpHeroH = 200;
static constexpr int kWpAfterHeroGap = 12;

static constexpr const char* kWallpaperModeLabels[] = {"Fill", "Fit", "Stretch", "Center", "Tile"};

static std::string wallpaper_file_basename(const std::string& p) {
   
  const size_t s = p.rfind('/');
  return (s == std::string::npos) ? p : p.substr(s + 1);
}

static std::string wallpaper_truncate_visual(const std::string& p, size_t maxC) {
  if (p.size() <= maxC) return p;
  if (maxC < 12) return p.substr(0, maxC);
  const size_t head = maxC / 2 - 2;
  const size_t tail = maxC - head - 3;
  return p.substr(0, head) + "\u2026" + p.substr(p.size() - tail);
}

static void wallpaper_sort_paths_inplace(std::vector<std::string>& paths, int sortMode,
                                         std::unordered_map<std::string, time_t>& mtimeCache) {
  if (paths.empty()) return;
  if (sortMode == 0) {
    std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
      const std::string ba = wallpaper_file_basename(a);
      const std::string bb = wallpaper_file_basename(b);
      if (ba != bb) return ba < bb;
      return a < b;
    });
    return;
  }
  struct Entry {
    std::string path;
    time_t mt = 0;
  };
  std::vector<Entry> tmp;
  tmp.reserve(paths.size());
  for (const auto& p : paths) {
    auto it = mtimeCache.find(p);
    if (it != mtimeCache.end()) {
      tmp.push_back({p, it->second});
    } else {
      struct stat st {};
      time_t mt = 0;
      if (::stat(p.c_str(), &st) == 0) mt = st.st_mtime;
      mtimeCache[p] = mt;
      tmp.push_back({p, mt});
    }
  }
  if (sortMode == 1) {
    std::sort(tmp.begin(), tmp.end(), [](const Entry& a, const Entry& b) {
      if (a.mt != b.mt) return a.mt < b.mt;
      return a.path < b.path;
    });
  } else {
    std::sort(tmp.begin(), tmp.end(), [](const Entry& a, const Entry& b) {
      if (a.mt != b.mt) return a.mt > b.mt;
      return a.path < b.path;
    });
  }
  paths.clear();
  for (auto& e : tmp) paths.push_back(std::move(e.path));
}

WallpaperTabLayout live_wallpaper_tab_layout(const App& app, int cardInsetX, int contentW, int galleryGridTopY) {
  WallpaperTabLayout L{};
  L.galleryTop = galleryGridTopY;
  const int availW = contentW - 8 - 2 * kCardPad;
  L.gap = kWpThumbGap;
  L.cols = std::clamp(app.liveWallpaperGalleryColumns, 1, 10);
  const double scale = static_cast<double>(std::clamp(app.liveWallpaperGalleryScalePct, 50, 200)) / 100.0;
  int tw = static_cast<int>(
      std::ceil((static_cast<double>(availW - L.gap * (L.cols - 1)) / static_cast<double>(L.cols)) * scale));
  tw = std::max(40, tw);
  L.thumb = tw;
  L.thumbH = (tw * 9 + 8) / 16;
  L.rows = std::clamp(app.liveWallpaperGalleryRows, 1, 10);
  L.perPage = L.cols * L.rows;
  const size_t n = app.liveWallpaperGalleryPaths.size();
  const int rowStrideNav = L.thumbH + kWpThumbLabelH + 6;
  const int gridBottomNav = L.galleryTop + L.rows * rowStrideNav;
  L.navY = gridBottomNav + 14;
  L.showNav = n > static_cast<size_t>(L.perPage);
  if (L.showNav) {
    L.galleryBottom = L.navY - 10;
  } else {
    L.galleryBottom = gridBottomNav + 16;
  }
  L.prevX = cardInsetX + kCardPad;
  L.nextX = cardInsetX + contentW - kCardPad - WallpaperTabLayout::kNavW;
  return L;
}

void live_wallpaper_destroy_thumbs(App& app) {
  for (auto& e : app.liveWallpaperThumbs) {
    if (e.second) cairo_surface_destroy(e.second);
  }
  app.liveWallpaperThumbs.clear();
  app.liveWallpaperThumbLru.clear();
}

void wallpaper_clamp_page_lw(App& app, int perPage) {
  if (perPage < 1) perPage = 1;
  const size_t n = app.liveWallpaperGalleryPaths.size();
  const int pages = std::max(1, static_cast<int>((n + static_cast<size_t>(perPage) - 1) / static_cast<size_t>(perPage)));
  if (app.liveWallpaperGalleryPage >= pages) app.liveWallpaperGalleryPage = pages - 1;
  if (app.liveWallpaperGalleryPage < 0) app.liveWallpaperGalleryPage = 0;
}

static std::string wallpaper_folder_key(const std::string& folder) {
  if (folder.empty()) return {};
  std::string o = folder;
  while (o.size() > 1 && (o.back() == '/' || o.back() == '\\')) o.pop_back();
  return o;
}

void ensure_live_wallpaper_gallery(App& app) {
  if (app.settings.wallpaperFolder.empty()) {
    if (!app.liveWallpaperGalleryPaths.empty()) {
      eh::wallpaper::WallpaperThumbnailService::instance().release_all();
      live_wallpaper_destroy_thumbs(app);
      app.liveWallpaperGalleryPaths.clear();
      app.liveWallpaperGalleryMtimeCache.clear();
      app.liveWallpaperGalleryPage = 0;
    }
    app.liveWallpaperGalleryFolderSynced.clear();
    app.liveWallpaperGalleryFolderMtime = 0;
    app.liveWallpaperGallerySortModeApplied = -1;
    return;
  }
  const std::string key = wallpaper_folder_key(app.settings.wallpaperFolder);
  const auto now = std::chrono::steady_clock::now();
  constexpr auto kStatDebounce = std::chrono::milliseconds(500);
  bool needsRescan = false;
  if (now - app.liveWallpaperGalleryLastStatCheck >= kStatDebounce) {
    app.liveWallpaperGalleryLastStatCheck = now;
    struct stat st {};
    const time_t curMtime = (::stat(key.c_str(), &st) == 0) ? st.st_mtime : 0;
    if (app.liveWallpaperGalleryFolderSynced != key || app.liveWallpaperGalleryFolderMtime != curMtime) {
      needsRescan = true;
      app.liveWallpaperGalleryFolderSynced = key;
      app.liveWallpaperGalleryFolderMtime = curMtime;
    }
  }
  if (needsRescan || app.liveWallpaperGalleryPaths.empty()) {
    eh::wallpaper::WallpaperThumbnailService::instance().release_all();
    live_wallpaper_destroy_thumbs(app);
    app.liveWallpaperGalleryPaths = eh::wallpaper::scan_image_files(key.empty() ? app.settings.wallpaperFolder : key);
    app.liveWallpaperGalleryMtimeCache.clear();
    app.liveWallpaperGalleryPage = 0;
    app.liveWallpaperGallerySortModeApplied = -1;
    app.liveWallpaperGalleryPrecached = false;
  }
  if (app.liveWallpaperGallerySortMode != app.liveWallpaperGallerySortModeApplied) {
    wallpaper_sort_paths_inplace(app.liveWallpaperGalleryPaths, app.liveWallpaperGallerySortMode, app.liveWallpaperGalleryMtimeCache);
    app.liveWallpaperGallerySortModeApplied = app.liveWallpaperGallerySortMode;
  }

  if (!app.liveWallpaperGalleryPrecached && !app.liveWallpaperGalleryPaths.empty()) {
    app.liveWallpaperGalleryPrecached = true;
    for (const auto& fp : app.liveWallpaperGalleryPaths) {
      eh::wallpaper::WallpaperThumbnailService::instance().request(fp, 512);
    }
  }
}

static int lw_tab_logical_bottom_px(App& app, int contentX, int contentW) {
  ensure_live_wallpaper_gallery(app);
  const WallpaperVerticalMetrics wm =
      wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
  const int cardInsetXi = contentX + 8;
  const WallpaperTabLayout wl = live_wallpaper_tab_layout(app, cardInsetXi, contentW, wm.galleryGridTop);
  if (app.liveWallpaperUiSubTab == 1) {
    return wm.subTabY + kWpSubTabH + 20 + 40 + 28 + 12 + 4 * 48 + 24;
  }
  const int rowStride = wl.thumbH + kWpThumbLabelH + 6;
  const int gridBottom = wl.galleryTop + wl.rows * rowStride;
  if (wl.showNav) return wl.navY + WallpaperTabLayout::kNavH + 20;
  return gridBottom + 28;
}

int settings_live_wallpaper_scroll_max_px(App& app) {
  const int contentX = kSpacingL + kSidebarW + kSpacingL;
  const int contentW = app.width - contentX - kSpacingL;
  const int viewH = app.height - kContentTop - kSpacingL;
  const int logicalExtent = lw_tab_logical_bottom_px(app, contentX, contentW) - kContentTop + 12;
  return std::max(0, logicalExtent - viewH);
}

void settings_clamp_live_wallpaper_scroll_px(App& app) {
  const int mx = settings_live_wallpaper_scroll_max_px(app);
  app.liveWallpaperScrollPx = std::clamp(app.liveWallpaperScrollPx, 0, mx);
}

void live_wallpaper_invalidate_hero(App& app) {
  if (app.liveWallpaperHeroSurf) {
    cairo_surface_destroy(app.liveWallpaperHeroSurf);
    app.liveWallpaperHeroSurf = nullptr;
  }
  app.liveWallpaperHeroPath.clear();
}

void live_wallpaper_ensure_hero_surface(App& app) {
  const std::string& img = app.settings.wallpaperImage;
  if (img.empty()) {
    live_wallpaper_invalidate_hero(app);
    return;
  }
  if (app.liveWallpaperHeroPath == img && app.liveWallpaperHeroSurf &&
      cairo_surface_status(app.liveWallpaperHeroSurf) == CAIRO_STATUS_SUCCESS)
    return;
  if (!app.liveWallpaperHeroRequest.empty()) {
    if (app.liveWallpaperHeroRequest == img) return;
    app.liveWallpaperHeroRequest.clear();
  }
  app.liveWallpaperHeroRequest = img;
  eh::wallpaper::WallpaperThumbnailService::instance().request(img, 512);
}

void live_wallpaper_cycle_selection(App& app, int delta) {
  ensure_live_wallpaper_gallery(app);
  if (app.liveWallpaperGalleryPaths.empty()) return;
  const std::string& cur = app.settings.wallpaperImage;
  auto it = std::find(app.liveWallpaperGalleryPaths.begin(), app.liveWallpaperGalleryPaths.end(), cur);
  size_t idx = 0;
  if (it != app.liveWallpaperGalleryPaths.end()) idx = static_cast<size_t>(it - app.liveWallpaperGalleryPaths.begin());
  const size_t n = app.liveWallpaperGalleryPaths.size();
  idx = (idx + static_cast<size_t>(delta) + n) % n;
  app.settings.wallpaperImage = app.liveWallpaperGalleryPaths[idx];
  app.settings.wallpaperEnabled = true;
}

bool live_wallpaper_thumb_needs_followup_frame(App& app) {
  if (app.activeTab != 49) return false;
  if (app.liveWallpaperUiSubTab != 0) return false;
  ensure_live_wallpaper_gallery(app);
  if (app.liveWallpaperGalleryPaths.empty()) return false;
  const int contentX = kSpacingL + kSidebarW + kSpacingL;
  const int contentW = app.width - contentX - kSpacingL;
  const int cardInsetXi = contentX + 8;
  const WallpaperVerticalMetrics vm = wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
  const WallpaperTabLayout wl = live_wallpaper_tab_layout(app, cardInsetXi, contentW, vm.galleryGridTop);
  wallpaper_clamp_page_lw(app, wl.perPage);
  const size_t gallOff = static_cast<size_t>(std::max(0, app.liveWallpaperGalleryPage)) * static_cast<size_t>(wl.perPage);
  const size_t nTotal = app.liveWallpaperGalleryPaths.size();
  const size_t nSlots =
      (nTotal <= gallOff) ? static_cast<size_t>(0) : std::min(static_cast<size_t>(wl.perPage), nTotal - gallOff);
  for (size_t ii = 0; ii < nSlots; ++ii) {
    const std::string& fp = app.liveWallpaperGalleryPaths[gallOff + ii];
    if (app.liveWallpaperThumbs.find(fp) == app.liveWallpaperThumbs.end()) return true;
  }
  return eh::wallpaper::WallpaperThumbnailService::instance().has_pending_completed();
}

// Paint function.
void paint_live_wallpaper_tab(App& app, cairo_t* cr, int contentX, int contentW,
                         double cardX, double cardW, double glassOv,
                         double dockMatA, double paintPointerYOffset) {
  const double pyH = app.pointerY + paintPointerYOffset;

  const WallpaperVerticalMetrics WM =
      wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
  const int cardInsetXi = contentX + 8;
  ensure_live_wallpaper_gallery(app);
  const WallpaperTabLayout WL =
      live_wallpaper_tab_layout(app, cardInsetXi, contentW, WM.galleryGridTop);
  wallpaper_clamp_page_lw(app, WL.perPage);

  const double wpTop = static_cast<double>(kContentTop);
  const double wpFillHView =
      static_cast<double>(app.height) - static_cast<double>(kContentTop) - static_cast<double>(kSpacingXL);
  const int bottomPx =
      app.liveWallpaperUiSubTab == 1
          ? (WM.subTabY + kWpSubTabH + 20 + 40 + 28 + 12 + 4 * 48 + 24)
          : (WL.showNav ? (WL.navY + WallpaperTabLayout::kNavH + 20)
                        : (WL.galleryTop + WL.rows * (WL.thumbH + kWpThumbLabelH + 6) + 28));
  const double cardLogicalH =
      std::max(wpFillHView, static_cast<double>(bottomPx - kContentTop) + 12.0);

  settings_card(app, cr, cardX, wpTop, cardW, cardLogicalH, glassOv);
  settings_label(cr, static_cast<double>(contentX + kCardPad), wpTop + 36.0, "Live wallpaper",
                 "Top FAB: folder; middle FAB: picker backend. Gallery tab has the same picker row. swaybg or EH_WALLPAPER_CMD (%p, %m).");

  const int wpTopI = kContentTop;
  (void)settings_toggle(app, cr, static_cast<int>(cardX), wpTopI, static_cast<int>(cardW), wpTop,
                        static_cast<double>(kWallpaperToggleBandH), app.settings.wallpaperEnabled,
                        dockMatA);

  // Sub-tabs (wallpaper / gallery settings)
  {
    const int stX = contentX + 12;
    const int stW = contentW - 24;
    const int stY = WM.subTabY;
    const int half = stW / 2;
    const bool h0 = app.pointerX >= stX && app.pointerX < stX + half && pyH >= static_cast<double>(stY) &&
                    pyH < static_cast<double>(stY + kWpSubTabH);
    const bool h1 = app.pointerX >= stX + half && app.pointerX < stX + stW && pyH >= static_cast<double>(stY) &&
                    pyH < static_cast<double>(stY + kWpSubTabH);
    {
      m3::Box box;
      if (app.liveWallpaperUiSubTab == 0) {
        float r, g, b;
        if (app.drawChromeMatugen) {
          r = app.drawChrome.accentR;
          g = app.drawChrome.accentG;
          b = app.drawChrome.accentB;
        } else {
          r = static_cast<float>(Theme::AccR);
          g = static_cast<float>(Theme::AccG);
          b = static_cast<float>(Theme::AccB);
        }
        box.setColor(r, g, b, 0.52f);
      } else {
        float hr, hg, hb;
        if (app.drawChromeMatugen) {
          hr = 0.48f + 0.52f * app.drawChrome.outlineR;
          hg = 0.48f + 0.52f * app.drawChrome.outlineG;
          hb = 0.48f + 0.52f * app.drawChrome.outlineB;
        } else {
          hr = 1.0f; hg = 1.0f; hb = 1.0f;
        }
        box.setColor(hr, hg, hb, h0 ? 0.14f : 0.08f);
      }
      box.setRadius(8.0f);
      box.setGeometry(static_cast<float>(stX), static_cast<float>(stY),
                      static_cast<float>(half), static_cast<float>(kWpSubTabH));
      box.paint(cr);
    }
    {
      m3::Box box;
      if (app.liveWallpaperUiSubTab == 1) {
        float r, g, b;
        if (app.drawChromeMatugen) {
          r = app.drawChrome.accentR;
          g = app.drawChrome.accentG;
          b = app.drawChrome.accentB;
        } else {
          r = static_cast<float>(Theme::AccR);
          g = static_cast<float>(Theme::AccG);
          b = static_cast<float>(Theme::AccB);
        }
        box.setColor(r, g, b, 0.52f);
      } else {
        float hr, hg, hb;
        if (app.drawChromeMatugen) {
          hr = 0.48f + 0.52f * app.drawChrome.outlineR;
          hg = 0.48f + 0.52f * app.drawChrome.outlineG;
          hb = 0.48f + 0.52f * app.drawChrome.outlineB;
        } else {
          hr = 1.0f; hg = 1.0f; hb = 1.0f;
        }
        box.setColor(hr, hg, hb, h1 ? 0.14f : 0.08f);
      }
      box.setRadius(8.0f);
      box.setGeometry(static_cast<float>(stX + half), static_cast<float>(stY),
                      static_cast<float>(stW - half), static_cast<float>(kWpSubTabH));
      box.paint(cr);
    }
    const double stMidY = static_cast<double>(stY + kWpSubTabH / 2);
    material_symbols_draw_glyph(cr, stX + 22.0, stMidY, 20.0, "wallpaper", Theme::TextR, Theme::TextG, Theme::TextB,
                                app.liveWallpaperUiSubTab == 0 ? 0.98 : 0.62);
    material_symbols_draw_glyph(cr, stX + half + 16.0, stMidY, 20.0, "grid_view", Theme::TextR, Theme::TextG, Theme::TextB,
                                app.liveWallpaperUiSubTab == 1 ? 0.98 : 0.62);
    settings_show_text(cr, stX + 46, stY + 31, "Wallpaper", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    settings_show_text(cr, stX + half + 42, stY + 31, "Gallery settings", 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
  }

  if (app.liveWallpaperUiSubTab == 1) {
    // Gallery settings sub-tab
    const int gsTop = WM.subTabY + kWpSubTabH + 20;
    settings_show_text(cr, contentX + kCardPad, gsTop, "Grid layout", 13, 700, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    settings_show_text(cr, contentX + kCardPad, gsTop + 18, "Session only \u2014 not written to shell config.", 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    constexpr int kGalPickerBtnH = 28;
    const int galPickerY = gsTop + 40;
    {
      const int pbx = contentX + kCardPad;
      const int pbw = contentW - 2 * kCardPad;
      const bool phov = app.pointerX >= pbx && app.pointerX < pbx + pbw && pyH >= static_cast<double>(galPickerY) &&
                          pyH < static_cast<double>(galPickerY + kGalPickerBtnH);
      m3::Button pickerBtn;
      pickerBtn.setMinSize(0, 0);
      pickerBtn.setLabel("Folder dialog (native / kdialog / \u2026)");
      pickerBtn.setGeometry(static_cast<float>(pbx), static_cast<float>(galPickerY),
                            static_cast<float>(pbw), static_cast<float>(kGalPickerBtnH));
      pickerBtn.setStyle(m3::Button::Style::Outlined);
      pickerBtn.setSize(m3::Button::Size::XS);
      pickerBtn.setAccentColor(a_r, a_g, a_b);
      pickerBtn.setOutlineColor(o_r, o_g, o_b);
      pickerBtn.setHovered(phov);
      pickerBtn.paint(cr);
    }

    const int row0 = galPickerY + kGalPickerBtnH + 12;
    const int rowPitch = 48;
    const int btnW = 36;
    const int btnH = 28;
    const int minusX = contentX + contentW - kCardPad - 2 * btnW - 10;
    const int plusX = contentX + contentW - kCardPad - btnW;
    auto paint_gs_row = [&](int row, const char* label, const char* valStr) {
      const int ry = row0 + row * rowPitch;
      settings_show_text(cr, contentX + kCardPad, ry + 20, label, 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
      settings_show_text(cr, contentX + kCardPad + 200, ry + 20, valStr, 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
      const bool hm = app.pointerX >= minusX && app.pointerX < minusX + btnW && pyH >= static_cast<double>(ry + 4) &&
                      pyH < static_cast<double>(ry + 4 + btnH);
      const bool hp = app.pointerX >= plusX && app.pointerX < plusX + btnW && pyH >= static_cast<double>(ry + 4) &&
                      pyH < static_cast<double>(ry + 4 + btnH);
      {
        m3::Button mBtn;
        mBtn.setMinSize(0, 0);
        mBtn.setLabel("\u2212");
        mBtn.setGeometry(static_cast<float>(minusX), static_cast<float>(ry + 4),
                          static_cast<float>(btnW), static_cast<float>(btnH));
        mBtn.setStyle(m3::Button::Style::Outlined);
        mBtn.setSize(m3::Button::Size::M);
        mBtn.setAccentColor(a_r, a_g, a_b);
        mBtn.setOutlineColor(o_r, o_g, o_b);
        mBtn.setHovered(hm);
        mBtn.paint(cr);
      }
      {
        m3::Button pBtn;
        pBtn.setMinSize(0, 0);
        pBtn.setLabel("+");
        pBtn.setGeometry(static_cast<float>(plusX), static_cast<float>(ry + 4),
                          static_cast<float>(btnW), static_cast<float>(btnH));
        pBtn.setStyle(m3::Button::Style::Outlined);
        pBtn.setSize(m3::Button::Size::M);
        pBtn.setAccentColor(a_r, a_g, a_b);
        pBtn.setOutlineColor(o_r, o_g, o_b);
        pBtn.setHovered(hp);
        pBtn.paint(cr);
      }
    };
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", app.liveWallpaperGalleryColumns);
    paint_gs_row(0, "Columns", buf);
    std::snprintf(buf, sizeof(buf), "%d", app.liveWallpaperGalleryRows);
    paint_gs_row(1, "Rows", buf);
    std::snprintf(buf, sizeof(buf), "%d%%", app.liveWallpaperGalleryScalePct);
    paint_gs_row(2, "Thumbnail scale", buf);
    std::snprintf(buf, sizeof(buf), "%d px", app.liveWallpaperGalleryThumbRadiusPx);
    paint_gs_row(3, "Corner radius", buf);
  } else {
    // Wallpaper sub-tab (hero + gallery)
    live_wallpaper_ensure_hero_surface(app);
    const double hx = WM.heroX;
    const double hy = static_cast<double>(WM.heroY);
    const double hw = WM.heroW;
    const double hh = static_cast<double>(kWpHeroH);
    cairo_save(cr);
    cairo_round_rect(cr, hx, hy, hw, hh, 12.0);
    cairo_clip(cr);
    paint_src_bg(app, cr, 0.94);
    cairo_paint(cr);
    if (app.liveWallpaperHeroSurf && cairo_surface_status(app.liveWallpaperHeroSurf) == CAIRO_STATUS_SUCCESS) {
      cairo_surface_flush(app.liveWallpaperHeroSurf);
      const int iw = cairo_image_surface_get_width(app.liveWallpaperHeroSurf);
      const int ih = cairo_image_surface_get_height(app.liveWallpaperHeroSurf);
      if (iw > 0 && ih > 0) {
        const double sc = std::max(hw / static_cast<double>(iw), hh / static_cast<double>(ih));
        const double dispW = static_cast<double>(iw) * sc;
        const double dispH = static_cast<double>(ih) * sc;
        const double ox = hx + (hw - dispW) * 0.5;
        const double oy = hy + (hh - dispH) * 0.5;
        cairo_translate(cr, ox, oy);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, app.liveWallpaperHeroSurf, 0, 0);
        cairo_paint(cr);
      }
    }
    cairo_restore(cr);
    cairo_pattern_t* gTop = cairo_pattern_create_linear(hx, hy, hx, hy + 64);
    cairo_pattern_add_color_stop_rgba(gTop, 0, 0, 0, 0, 0.5);
    cairo_pattern_add_color_stop_rgba(gTop, 1, 0, 0, 0, 0);
    cairo_set_source(cr, gTop);
    cairo_rectangle(cr, hx, hy, hw, 64);
    cairo_fill(cr);
    cairo_pattern_destroy(gTop);
    cairo_pattern_t* gBot = cairo_pattern_create_linear(hx, hy + hh - 72, hx, hy + hh);
    cairo_pattern_add_color_stop_rgba(gBot, 0, 0, 0, 0, 0);
    cairo_pattern_add_color_stop_rgba(gBot, 1, 0, 0, 0, 0.55);
    cairo_set_source(cr, gBot);
    cairo_rectangle(cr, hx, hy + hh - 72, hw, 72);
    cairo_fill(cr);
    cairo_pattern_destroy(gBot);

    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

    // FAB buttons
    const double fr = 18.0;
    const double fabCx = hx + hw - fr - 14.0;
    const double fabY0 = hy + 22.0;
    static const char* const kFabGlyph[] = {"folder_open", "tune", "delete"};
    for (int fi = 0; fi < 3; ++fi) {
      const double fcy = fabY0 + static_cast<double>(fi) * 50.0;
      const double fbSize = fr * 2.0;
      const double fbx = fabCx - fr;
      const double fby = fcy - fr;
      const bool fh = app.pointerX >= fbx && app.pointerX < fbx + fbSize && pyH >= fby && pyH < fby + fbSize;
      m3::Button fabBtn;
      fabBtn.setMinSize(0, 0);
      fabBtn.setGlyph(kFabGlyph[fi]);
      fabBtn.setGeometry(static_cast<float>(fbx), static_cast<float>(fby),
                          static_cast<float>(fbSize), static_cast<float>(fbSize));
      fabBtn.setStyle(m3::Button::Style::Outlined);
      fabBtn.setSize(m3::Button::Size::XS);
      fabBtn.setAccentColor(a_r, a_g, a_b);
      fabBtn.setOutlineColor(o_r, o_g, o_b);
      fabBtn.setHovered(fh);
      fabBtn.paint(cr);
    }

    // Hero nav arrows
    const int heroBarY = static_cast<int>(hy + hh) - 36;
    const int heroBarH = 32;
    const int arW = 40;
    const bool hHpL = app.pointerX >= hx + 6 && app.pointerX < hx + 6 + arW && pyH >= static_cast<double>(heroBarY) &&
                      pyH < static_cast<double>(heroBarY + heroBarH);
    const bool hHpR = app.pointerX >= hx + hw - arW - 6 && app.pointerX < hx + hw - 6 && pyH >= static_cast<double>(heroBarY) &&
                      pyH < static_cast<double>(heroBarY + heroBarH);
    {
      {
        m3::Button navL;
        navL.setMinSize(0, 0);
        navL.setGlyph("chevron_left");
        navL.setGeometry(static_cast<float>(hx + 6), static_cast<float>(heroBarY),
                          static_cast<float>(arW), static_cast<float>(heroBarH));
        navL.setStyle(m3::Button::Style::Outlined);
        navL.setSize(m3::Button::Size::XS);
        navL.setAccentColor(a_r, a_g, a_b);
        navL.setOutlineColor(o_r, o_g, o_b);
        navL.setHovered(hHpL);
        navL.paint(cr);
      }
      {
        m3::Button navR;
        navR.setMinSize(0, 0);
        navR.setGlyph("chevron_right");
        navR.setGeometry(static_cast<float>(hx + hw - arW - 6), static_cast<float>(heroBarY),
                          static_cast<float>(arW), static_cast<float>(heroBarH));
        navR.setStyle(m3::Button::Style::Outlined);
        navR.setSize(m3::Button::Size::XS);
        navR.setAccentColor(a_r, a_g, a_b);
        navR.setOutlineColor(o_r, o_g, o_b);
        navR.setHovered(hHpR);
        navR.paint(cr);
      }
    }
    std::string heroName = app.settings.wallpaperImage.empty()
                               ? std::string("(no image)")
                               : wallpaper_file_basename(app.settings.wallpaperImage);
    if (heroName.size() > 48) heroName = wallpaper_truncate_visual(heroName, 48);
    settings_show_text(cr, hx + hw * 0.5, hy + hh - 14, heroName.c_str(), 11, 400, 1, 1, 1, 0.88);

    // Mode combo
    {
      int wcx, wcy, wcw, wch;
      wallpaper_mode_combo_geom(static_cast<int>(contentX), static_cast<int>(contentW), WM.modeRowY, &wcx, &wcy, &wcw,
                                &wch);
      const int mm = std::clamp(app.settings.wallpaperMode, 0, 4);
      settings_paint_combo_closed(app, cr, wcx, wcy, wcw, wch, glassOv, kWallpaperModeLabels[mm],
                                    app.liveWallpaperModeDropdownOpen, app.liveWallpaperScrollPx);
    }

    // Folder line
    {
      std::string folderLine = app.settings.wallpaperFolder.empty()
                                   ? "(no folder)"
                                   : wallpaper_truncate_visual(app.settings.wallpaperFolder, 96);
      settings_show_text(cr, static_cast<double>(contentX + kCardPad), WM.folderLineY + 14, folderLine.c_str(), 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    }
    // Picker link
    {
      constexpr const char* kPickerLink = "Picker\u2026";
      constexpr int kPickerLinkW = 72;
      constexpr int kPickerLinkH = 16;
      const int pickerLinkX = static_cast<int>(contentX + contentW - kCardPad - kPickerLinkW);
      const int pickerLinkY = WM.folderLineY + 2;
      const bool pickerLinkHov =
          app.pointerX >= pickerLinkX && app.pointerX < pickerLinkX + kPickerLinkW &&
          pyH >= static_cast<double>(pickerLinkY) && pyH < static_cast<double>(pickerLinkY + kPickerLinkH);
      m3::Button pickerBtn;
      pickerBtn.setMinSize(0, 0);
      pickerBtn.setLabel(kPickerLink);
      pickerBtn.setGeometry(static_cast<float>(pickerLinkX), static_cast<float>(pickerLinkY),
                             static_cast<float>(kPickerLinkW), static_cast<float>(kPickerLinkH));
      pickerBtn.setStyle(m3::Button::Style::Outlined);
      pickerBtn.setSize(m3::Button::Size::XS);
      pickerBtn.setAccentColor(a_r, a_g, a_b);
      pickerBtn.setOutlineColor(o_r, o_g, o_b);
      pickerBtn.setHovered(pickerLinkHov);
      pickerBtn.paint(cr);
    }

    // Gallery header
    {
      std::ostringstream hd;
      if (app.settings.wallpaperFolder.empty()) {
        hd << "No folder";
      } else {
        hd << wallpaper_file_basename(app.settings.wallpaperFolder);
      }
      hd << "  \u00b7  " << app.liveWallpaperGalleryPaths.size() << " wallpapers";
      const std::string hds = hd.str();
      settings_show_text(cr, contentX + kCardPad, WM.galleryHeaderY + 18, hds.c_str(), 12, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    }

    // Sort pills
    const int pillY = WM.sortPillsY;
    const int pillH = 26;
    const char* plab[] = {"Name", "Oldest", "Newest"};
    int px = contentX + kCardPad;
    for (int pi = 0; pi < 3; ++pi) {
      const int pww = (pi == 0 ? 72 : 88);
      const bool sel = app.liveWallpaperGallerySortMode == pi;
      const bool ph = app.pointerX >= px && app.pointerX < px + pww && pyH >= static_cast<double>(pillY) &&
                      pyH < static_cast<double>(pillY + pillH);
      {
        m3::Button pillBtn;
        pillBtn.setMinSize(0, 0);
        pillBtn.setLabel(plab[pi]);
        pillBtn.setGeometry(static_cast<float>(px), static_cast<float>(pillY),
                             static_cast<float>(pww), static_cast<float>(pillH));
        pillBtn.setStyle(m3::Button::Style::Outlined);
        pillBtn.setSize(m3::Button::Size::XS);
        if (sel) {
          pillBtn.setAccentColor(a_r, a_g, a_b);
          pillBtn.setOutlineColor(a_r, a_g, a_b);
        } else {
          float d_r, d_g, d_b;
          if (app.drawChromeMatugen) {
            d_r = 0.48f + 0.52f * app.drawChrome.outlineR;
            d_g = 0.48f + 0.52f * app.drawChrome.outlineG;
            d_b = 0.48f + 0.52f * app.drawChrome.outlineB;
          } else {
            d_r = 1.0f; d_g = 1.0f; d_b = 1.0f;
          }
          pillBtn.setAccentColor(d_r, d_g, d_b);
          pillBtn.setOutlineColor(o_r, o_g, o_b);
        }
        pillBtn.setHovered(ph);
        pillBtn.paint(cr);
      }
      px += pww + 8;
    }
    // Gallery grid
    const double gx0d = static_cast<double>(cardInsetXi + kCardPad);
    const size_t gallOff = static_cast<size_t>(app.liveWallpaperGalleryPage * WL.perPage);
    const std::string& preferImg = app.settings.wallpaperImage;
    const int decodePx = std::min(512, std::max(WL.thumb, WL.thumbH));
    if (!preferImg.empty() && app.liveWallpaperThumbs.find(preferImg) == app.liveWallpaperThumbs.end()) {
      eh::wallpaper::WallpaperThumbnailService::instance().request(preferImg, decodePx);
    }

    const size_t nSlots =
        (app.liveWallpaperGalleryPaths.size() <= gallOff)
            ? static_cast<size_t>(0)
            : std::min(static_cast<size_t>(WL.perPage), app.liveWallpaperGalleryPaths.size() - gallOff);

    const int rowStride = WL.thumbH + kWpThumbLabelH + 6;
    const double tileRad = static_cast<double>(std::clamp(app.liveWallpaperGalleryThumbRadiusPx, 0, 48));
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    for (size_t ii = 0; ii < nSlots; ++ii) {
      const size_t gi = gallOff + ii;
      const std::string& fp = app.liveWallpaperGalleryPaths[gi];
      const int row = static_cast<int>(ii / static_cast<size_t>(WL.cols));
      const int col = static_cast<int>(ii % static_cast<size_t>(WL.cols));
      const double gx = gx0d + static_cast<double>(col * (WL.thumb + WL.gap));
      const double gy = static_cast<double>(WL.galleryTop) + static_cast<double>(row * rowStride);

      cairo_surface_t* surf = nullptr;
      const auto thIt = app.liveWallpaperThumbs.find(fp);
      if (thIt != app.liveWallpaperThumbs.end()) surf = thIt->second;
      else {
        eh::wallpaper::WallpaperThumbnailService::instance().request(fp, decodePx);
      }

      const bool hovered = static_cast<int>(ii) == app.liveWallpaperHoveredSlot;
      const double hs = hovered ? static_cast<double>(app.liveWallpaperHoverScale) : 1.0;

      cairo_save(cr);
      if (hs > 1.001) {
        cairo_translate(cr, gx + WL.thumb * 0.5, gy + WL.thumbH * 0.5);
        cairo_scale(cr, hs, hs);
        cairo_translate(cr, -gx - WL.thumb * 0.5, -gy - WL.thumbH * 0.5);
      }
      cairo_round_rect(cr, gx, gy, WL.thumb, static_cast<double>(WL.thumbH), tileRad);
      cairo_clip(cr);
      cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
      if (surf && cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
        cairo_surface_flush(surf);
        const int iw = cairo_image_surface_get_width(surf);
        const int ih = cairo_image_surface_get_height(surf);
        if (iw > 0 && ih > 0) {
          const double sc = std::max(static_cast<double>(WL.thumb) / static_cast<double>(iw),
                                     static_cast<double>(WL.thumbH) / static_cast<double>(ih));
          const double dispW = static_cast<double>(iw) * sc;
          const double dispH = static_cast<double>(ih) * sc;
          const double ox = gx + (static_cast<double>(WL.thumb) - dispW) * 0.5;
          const double oy = gy + (static_cast<double>(WL.thumbH) - dispH) * 0.5;
          cairo_translate(cr, ox, oy);
          cairo_scale(cr, sc, sc);
          cairo_set_source_surface(cr, surf, 0, 0);
          cairo_paint(cr);
        }
      } else {
        paint_src_bg(app, cr, 0.95);
        cairo_paint(cr);
      }
      cairo_restore(cr);

      if (fp == app.settings.wallpaperImage) {
        cairo_save(cr);
        cairo_round_rect(cr, gx, gy, WL.thumb, static_cast<double>(WL.thumbH), tileRad);
        cairo_clip(cr);
        paint_src_accent(app, cr, 0.22);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_paint(cr);
        cairo_restore(cr);
      }

      cairo_round_rect(cr, gx, gy, WL.thumb, static_cast<double>(WL.thumbH), tileRad);
      cairo_set_line_width(cr, fp == app.settings.wallpaperImage ? 2.3 : 1.0);
      if (fp == app.settings.wallpaperImage)
        paint_src_accent(app, cr, 0.72);
      else
        paint_src_glass_hi(app, cr, 0.12);
      cairo_stroke(cr);

      if (fp == app.settings.wallpaperImage) {
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
          box.setColor(r, g, b, 0.35f);
          box.setRadius(6.0f);
          box.setGeometry(static_cast<float>(gx + 6), static_cast<float>(gy + 6), 54.0f, 18.0f);
          box.paint(cr);
        }
        settings_show_text(cr, gx + 10, gy + 19, "In use", 9, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
      }

      if (hovered) {
        cairo_save(cr);
        cairo_round_rect(cr, gx, gy, WL.thumb, static_cast<double>(WL.thumbH), tileRad);
        cairo_clip(cr);
        paint_src_accent(app, cr, 0.15f);
        cairo_paint(cr);
        cairo_restore(cr);
      }

    }

    // Navigation
    if (WL.showNav) {
      const bool hPrev = (app.pointerX >= WL.prevX && app.pointerX < WL.prevX + WL.kNavW &&
                          pyH >= static_cast<double>(WL.navY) && pyH < static_cast<double>(WL.navY + WL.kNavH));
      const bool hNext =
          (app.pointerX >= WL.nextX && app.pointerX < WL.nextX + WL.kNavW && pyH >= static_cast<double>(WL.navY) &&
           pyH < static_cast<double>(WL.navY + WL.kNavH));
      {
        float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
        settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
        m3::Button prevBtn;
        prevBtn.setMinSize(0, 0);
        prevBtn.setLabel("Prev");
        prevBtn.setGeometry(static_cast<float>(WL.prevX), static_cast<float>(WL.navY),
                              static_cast<float>(WL.kNavW), static_cast<float>(WL.kNavH));
        prevBtn.setStyle(m3::Button::Style::Outlined);
        prevBtn.setSize(m3::Button::Size::XS);
        prevBtn.setAccentColor(a_r, a_g, a_b);
        prevBtn.setOutlineColor(o_r, o_g, o_b);
        prevBtn.setHovered(hPrev);
        prevBtn.paint(cr);
      }
      {
        float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
        settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
        m3::Button nextBtn;
        nextBtn.setMinSize(0, 0);
        nextBtn.setLabel("Next");
        nextBtn.setGeometry(static_cast<float>(WL.nextX), static_cast<float>(WL.navY),
                              static_cast<float>(WL.kNavW), static_cast<float>(WL.kNavH));
        nextBtn.setStyle(m3::Button::Style::Outlined);
        nextBtn.setSize(m3::Button::Size::XS);
        nextBtn.setAccentColor(a_r, a_g, a_b);
        nextBtn.setOutlineColor(o_r, o_g, o_b);
        nextBtn.setHovered(hNext);
        nextBtn.paint(cr);
      }

        {
          std::ostringstream os;
          const int pages =
              std::max(1, static_cast<int>((app.liveWallpaperGalleryPaths.size() + static_cast<size_t>(WL.perPage) - 1) /
                                           static_cast<size_t>(WL.perPage)));
          os << static_cast<long>(app.liveWallpaperGalleryPage + 1) << " / " << pages;
          const std::string ps = os.str();
          settings_show_text(cr, cardX + cardW / 2.0, WL.navY + 18, ps.c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
        }
    }
  }

  // Wallpaper mode dropdown popup
  if (app.liveWallpaperUiSubTab == 0 && app.liveWallpaperModeDropdownOpen) {
    int ddCx, ddCy, ddCw, ddCh;
    wallpaper_mode_combo_geom(static_cast<int>(contentX), static_cast<int>(contentW), WM.modeRowY, &ddCx, &ddCy,
                              &ddCw, &ddCh);
    const int ddLy = ddCy + ddCh + 2;
    settings_paint_combo_list_popup(app, cr, ddCx, ddLy, ddCw, kSettingsDdRowH, 5, kWallpaperModeLabels,
                                    app.settings.wallpaperMode, app.liveWallpaperModeDropdownHoverRow, glassOv, 0, 0, false);
  }
}

// Pointer-down handler.
bool settings_live_wallpaper_consume_pointer_down(App& app, int contentX, int contentW) {
  const int cardInsetX = contentX + 8;
  const WallpaperVerticalMetrics wm =
      wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
  const double pyL = app.pointerY + settings_scroll_px(app);

  // Sub-tab toggle
  {
    const int stX = contentX + 12;
    const int stW = contentW - 24;
    const int stY = wm.subTabY;
    const int half = stW / 2;
    if (point_in_rect(app.pointerX, pyL, stX, stY, half, kWpSubTabH)) {
      app.liveWallpaperUiSubTab = 0;
      draw(app);
      return true;
    }
    if (point_in_rect(app.pointerX, pyL, stX + half, stY, stW - half, kWpSubTabH)) {
      app.liveWallpaperUiSubTab = 1;
      draw(app);
      return true;
    }
  }

  // Wallpaper enabled toggle
  {
    constexpr int wgW = 52;
    constexpr int wgH = 26;
    const int wgX = cardInsetX + (contentW - 16) - wgW - kSpacingXL;
    const int wgY = kContentTop + (kWallpaperToggleBandH - wgH) / 2;
    if (point_in_rect(app.pointerX, pyL, wgX, wgY, wgW, wgH)) {
      app.settings.wallpaperEnabled = !app.settings.wallpaperEnabled;
      save_settings(app.settings);
      wallpaper_apply_if_digest_changed(app.settings);
      eh::config::shell_config_reload_from_disk_now();
      app.settings = load_settings();
      draw(app);
      return true;
    }
  }

  // Gallery settings sub-tab
  if (app.liveWallpaperUiSubTab == 1) {
    const int gsTop = wm.subTabY + kWpSubTabH + 20;
    constexpr int kGalPickerBtnH = 28;
    const int galPickerY = gsTop + 40;
    if (point_in_rect(app.pointerX, pyL, contentX + kCardPad, galPickerY, contentW - 2 * kCardPad, kGalPickerBtnH)) {
      settings_close_mode_dropdowns(app);
      app.wallpaperFolderPickerOpen = true;
      draw(app);
      return true;
    }
    const int row0 = galPickerY + kGalPickerBtnH + 12;
    const int rowPitch = 48;
    const int btnW = 36;
    const int btnH = 28;
    const int minusX = contentX + contentW - kCardPad - 2 * btnW - 10;
    const int plusX = contentX + contentW - kCardPad - btnW;
    for (int r = 0; r < 4; ++r) {
      const int ry = row0 + r * rowPitch;
      if (point_in_rect(app.pointerX, pyL, minusX, ry + 4, btnW, btnH)) {
        if (r == 0)
          app.liveWallpaperGalleryColumns = std::max(1, app.liveWallpaperGalleryColumns - 1);
        else if (r == 1)
          app.liveWallpaperGalleryRows = std::max(1, app.liveWallpaperGalleryRows - 1);
        else if (r == 2)
          app.liveWallpaperGalleryScalePct = std::max(50, app.liveWallpaperGalleryScalePct - 5);
        else
          app.liveWallpaperGalleryThumbRadiusPx = std::max(0, app.liveWallpaperGalleryThumbRadiusPx - 1);
        draw(app);
        return true;
      }
      if (point_in_rect(app.pointerX, pyL, plusX, ry + 4, btnW, btnH)) {
        if (r == 0)
          app.liveWallpaperGalleryColumns = std::min(10, app.liveWallpaperGalleryColumns + 1);
        else if (r == 1)
          app.liveWallpaperGalleryRows = std::min(10, app.liveWallpaperGalleryRows + 1);
        else if (r == 2)
          app.liveWallpaperGalleryScalePct = std::min(200, app.liveWallpaperGalleryScalePct + 5);
        else
          app.liveWallpaperGalleryThumbRadiusPx = std::min(32, app.liveWallpaperGalleryThumbRadiusPx + 1);
        draw(app);
        return true;
      }
    }
    return true;
  }

  // Mode combo
  {
    int wcx, wcy, wcw, wch;
    wallpaper_mode_combo_geom(contentX, contentW, wm.modeRowY, &wcx, &wcy, &wcw, &wch);
    if (point_in_rect(app.pointerX, pyL, wcx, wcy, wcw, wch)) {
      settings_close_mode_dropdowns(app);
      app.liveWallpaperModeDropdownOpen = true;
      draw(app);
      return true;
    }
  }

  // Picker link
  {
    constexpr int kPickerLinkW = 72;
    constexpr int kPickerLinkH = 16;
    const int pickerLinkX = static_cast<int>(contentX + contentW - kCardPad - kPickerLinkW);
    const int pickerLinkY = wm.folderLineY + 2;
    if (point_in_rect(app.pointerX, pyL, pickerLinkX, pickerLinkY, kPickerLinkW, kPickerLinkH)) {
      settings_close_mode_dropdowns(app);
      app.wallpaperFolderPickerOpen = true;
      draw(app);
      return true;
    }
  }

  // FAB buttons
  const double hx = wm.heroX;
  const double hy = static_cast<double>(wm.heroY);
  const double hw = wm.heroW;
  const double hh = static_cast<double>(kWpHeroH);
  const double fr = 18.0;
  const double fabCx = hx + hw - fr - 14.0;
  const double fabY0 = hy + 22.0;
  for (int fi = 0; fi < 3; ++fi) {
    const double fcy = fabY0 + static_cast<double>(fi) * 50.0;
    if (std::hypot(app.pointerX - fabCx, pyL - fcy) <= fr + 2.0) {
      if (fi == 0) {
        std::string picked{};
        if (eh::wallpaper::pick_folder(&picked, app.settings.wallpaperFolderPickerMode)) {
          app.settings.wallpaperFolder = std::move(picked);
          app.liveWallpaperGalleryFolderSynced.clear();
          ensure_live_wallpaper_gallery(app);
          WallpaperTabLayout wlF =
              live_wallpaper_tab_layout(app, cardInsetX, contentW, wm.galleryGridTop);
          wallpaper_clamp_page_lw(app, wlF.perPage);
          save_settings(app.settings);
          draw(app);
        }
      } else if (fi == 1) {
        settings_close_mode_dropdowns(app);
        app.wallpaperFolderPickerOpen = true;
        draw(app);
      } else if (fi == 2) {
        app.settings.wallpaperImage.clear();
        app.settings.wallpaperEnabled = false;
        live_wallpaper_invalidate_hero(app);
        save_settings(app.settings);
        wallpaper_apply_if_digest_changed(app.settings);
        eh::config::shell_config_reload_from_disk_now();
        app.settings = load_settings();
        draw(app);
      }
      return true;
    }
  }

  // Hero nav arrows
  {
    const int heroBarY = static_cast<int>(hy + hh) - 36;
    const int heroBarH = 32;
    const int arW = 40;
    if (point_in_rect(app.pointerX, pyL, static_cast<int>(hx + 6), heroBarY, arW, heroBarH)) {
      live_wallpaper_cycle_selection(app, -1);
      live_wallpaper_invalidate_hero(app);
      save_settings(app.settings);
      draw(app);
      return true;
    }
    if (point_in_rect(app.pointerX, pyL, static_cast<int>(hx + hw - arW - 6), heroBarY, arW, heroBarH)) {
      live_wallpaper_cycle_selection(app, 1);
      live_wallpaper_invalidate_hero(app);
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  // Sort pills
  {
    const int pillY = wm.sortPillsY;
    const int pillH = 26;
    int px = contentX + kCardPad;
    for (int pi = 0; pi < 3; ++pi) {
      const int pww = (pi == 0 ? 72 : 88);
      if (point_in_rect(app.pointerX, pyL, px, pillY, pww, pillH)) {
        if (app.liveWallpaperGallerySortMode != pi) {
          app.liveWallpaperGallerySortMode = pi;
          app.liveWallpaperGalleryPage = 0;
          ensure_live_wallpaper_gallery(app);
        }
        draw(app);
        return true;
      }
      px += pww + 8;
    }
  }

  // Gallery thumb selection
  ensure_live_wallpaper_gallery(app);
  WallpaperTabLayout wlHit = live_wallpaper_tab_layout(app, cardInsetX, contentW, wm.galleryGridTop);
  wallpaper_clamp_page_lw(app, wlHit.perPage);

  const double gx0Hit = static_cast<double>(cardInsetX + kCardPad);
  const size_t gallOffHit = static_cast<size_t>(app.liveWallpaperGalleryPage * wlHit.perPage);
  const int rowStrideHit = wlHit.thumbH + kWpThumbLabelH + 6;

  if (!app.liveWallpaperGalleryPaths.empty()) {
    for (size_t ii = 0; ii < static_cast<size_t>(wlHit.perPage); ii++) {
      const size_t gi = gallOffHit + ii;
      if (gi >= app.liveWallpaperGalleryPaths.size()) break;
      const int row = static_cast<int>(ii / static_cast<size_t>(wlHit.cols));
      const int col = static_cast<int>(ii % static_cast<size_t>(wlHit.cols));
      const double gx = gx0Hit + static_cast<double>(col * (wlHit.thumb + wlHit.gap));
      const double gy = static_cast<double>(wlHit.galleryTop) + static_cast<double>(row * rowStrideHit);
      const std::string& fp = app.liveWallpaperGalleryPaths[gi];
      if (point_in_rect(app.pointerX, pyL, static_cast<int>(std::floor(gx)), static_cast<int>(std::floor(gy)),
                        wlHit.thumb, wlHit.thumbH)) {
        const auto t0 = std::chrono::steady_clock::now();
        app.settings.wallpaperImage = fp;
        app.settings.wallpaperEnabled = true;
        live_wallpaper_invalidate_hero(app);
        save_settings(app.settings);
        draw(app);  // instant feedback before apply (spawn + possible matugen) and reload
        const auto t1 = std::chrono::steady_clock::now();
        wallpaper_apply_if_digest_changed(app.settings);
        eh::config::shell_config_reload_from_disk_now();
        app.settings = load_settings();
        const auto t2 = std::chrono::steady_clock::now();
        // draw already issued before the slow apply/reload for 240Hz responsiveness
        const double saveMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double applyMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
        std::cerr << "[wp-click] save=" << saveMs << "ms apply=" << applyMs << "ms (early draw for responsiveness)\n";
        return true;
      }
    }
  }

  // Prev/Next navigation
  if (wlHit.showNav) {
    if (point_in_rect(app.pointerX, pyL, wlHit.prevX, wlHit.navY, WallpaperTabLayout::kNavW,
                      WallpaperTabLayout::kNavH)) {
      if (app.liveWallpaperGalleryPage > 0) {
        --app.liveWallpaperGalleryPage;
        draw(app);
      }
      return true;
    }
    if (point_in_rect(app.pointerX, pyL, wlHit.nextX, wlHit.navY, WallpaperTabLayout::kNavW,
                      WallpaperTabLayout::kNavH)) {
      const int pages = std::max(
          1, static_cast<int>((app.liveWallpaperGalleryPaths.size() + static_cast<size_t>(wlHit.perPage) - 1) /
                              static_cast<size_t>(wlHit.perPage)));
      if (app.liveWallpaperGalleryPage + 1 < pages) {
        ++app.liveWallpaperGalleryPage;
        draw(app);
      }
      return true;
    }
  }

  return false;
}
