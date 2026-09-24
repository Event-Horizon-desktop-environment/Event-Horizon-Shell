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
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/settings_tab_wallpaper/settings_tab_wallpaper.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "configuration/shell_config.hpp"
#include "wallpaper/apply/wallpaper_apply.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail_service.hpp"
#include "wallpaper/wallpaper_log.hpp"

extern void draw(App& app);
extern void save_settings(const struct Settings& s);
extern Settings load_settings();
extern void settings_close_mode_dropdowns(App& app);
extern void wallpaper_apply_if_digest_changed(const struct Settings& s);
extern void settings_paint_combo_list_popup(App& app, cairo_t* cr, int x, int y, int w, int rowH, int rowCount,
                                            const char* const* labels, int selectedIdx, int hoverRow, double glassOv,
                                            int scrollPx, int viewPortH, bool opaquePanel);

static constexpr double kCtrlCardRad = 16.0;
static constexpr int kCtrlCardH = 60;
static constexpr int kOpenPickerH = 48;
static constexpr int kActBtnH = 44;
static constexpr int kHeroH = 300;
static constexpr double kHeroRadius = 20.0;
// Floating toolbar inside the hero.
static constexpr int kToolBarH = 56;
static constexpr int kToolBtnS = 40;
static constexpr int kToolComboW = 128;
static constexpr int kToolComboH = 34;
static constexpr int kPickerH = 48;
static constexpr int kGalleryBarH = 32;
static constexpr int kViewPanelH = 148;

static constexpr const char* kWallpaperModeLabels[] = {"Fill", "Fit", "Stretch", "Center", "Tile"};

static std::string wallpaper_file_basename(const std::string& p) {
  WP_SCOPE();
  const size_t s = p.rfind('/');
  return (s == std::string::npos) ? p : p.substr(s + 1);
}

[[maybe_unused]] static std::string wallpaper_truncate_visual(const std::string& p, size_t maxC) {
  WP_SCOPE();
  if (p.size() <= maxC) return p;
  if (maxC < 12) return p.substr(0, maxC);
  const size_t head = maxC / 2 - 2;
  const size_t tail = maxC - head - 3;
  return p.substr(0, head) + "\u2026" + p.substr(p.size() - tail);
}

static void wallpaper_sort_paths_inplace(std::vector<std::string>& paths, int sortMode,
                                         std::unordered_map<std::string, time_t>& mtimeCache) {
  WP_SCOPE();
  WP_LOG("sortMode=%d paths=%zu", sortMode, paths.size());
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
  WP_LOG("sorted done");
}

WallpaperTabLayout wallpaper_tab_layout(const App& app, int cardInsetX, int contentW, int galleryGridTopY) {
  WP_SCOPE();
  WallpaperTabLayout L{};
  L.galleryTop = galleryGridTopY;
  const int availW = contentW - 8 - 2 * kCardPad;
  L.gap = kWpThumbGap;
  L.cols = std::clamp(app.wallpaperGalleryColumns, 1, 10);
  const double scale = static_cast<double>(std::clamp(app.wallpaperGalleryScalePct, 50, 200)) / 100.0;
  int tw = static_cast<int>(
      std::ceil((static_cast<double>(availW - L.gap * (L.cols - 1)) / static_cast<double>(L.cols)) * scale));
  tw = std::max(40, tw);
  L.thumb = tw;
  L.thumbH = (tw * 9 + 8) / 16;
  L.rows = std::clamp(app.wallpaperGalleryRows, 1, 10);
  L.perPage = L.cols * L.rows;
  const size_t n = app.wallpaperGalleryPaths.size();
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

void wallpaper_destroy_thumbs(App& app) {
  WP_SCOPE();
  for (auto& e : app.wallpaperThumbs) {
    if (e.second) cairo_surface_destroy(e.second);
  }
  app.wallpaperThumbs.clear();
  app.wallpaperThumbLru.clear();
}

void wallpaper_clamp_page(App& app, int perPage) {
  WP_SCOPE();
  if (perPage < 1) perPage = 1;
  const size_t n = app.wallpaperGalleryPaths.size();
  const int pages = std::max(1, static_cast<int>((n + static_cast<size_t>(perPage) - 1) / static_cast<size_t>(perPage)));
  if (app.wallpaperGalleryPage >= pages) app.wallpaperGalleryPage = pages - 1;
  if (app.wallpaperGalleryPage < 0) app.wallpaperGalleryPage = 0;
}

static std::string wallpaper_folder_key(const std::string& folder) {
  WP_SCOPE();
  if (folder.empty()) return {};
  std::string o = folder;
  while (o.size() > 1 && (o.back() == '/' || o.back() == '\\')) o.pop_back();
  return o;
}

void ensure_wallpaper_gallery(App& app) {
  WP_SCOPE();
  WP_LOG("folder=%s paths=%zu synced_key=%s", app.settings.wallpaperFolder.c_str(), app.wallpaperGalleryPaths.size(), app.wallpaperGalleryFolderSynced.c_str());
  if (app.settings.wallpaperFolder.empty()) {
    if (!app.wallpaperGalleryPaths.empty()) {
      eh::wallpaper::WallpaperThumbnailService::instance().release_all();
      wallpaper_destroy_thumbs(app);
      app.wallpaperGalleryPaths.clear();
      app.wallpaperGalleryMtimeCache.clear();
      app.wallpaperGalleryPage = 0;
    }
    app.wallpaperGalleryFolderSynced.clear();
    app.wallpaperGalleryFolderMtime = 0;
    app.wallpaperGallerySortModeApplied = -1;
    app.wallpaperGalleryValid = false;
    return;
  }
  const std::string key = wallpaper_folder_key(app.settings.wallpaperFolder);
  const auto now = std::chrono::steady_clock::now();
  constexpr auto kStatDebounce = std::chrono::milliseconds(500);
  bool needsRescan = false;
  if (now - app.wallpaperGalleryLastStatCheck >= kStatDebounce) {
    app.wallpaperGalleryLastStatCheck = now;
    struct stat st {};
    const time_t curMtime = (::stat(key.c_str(), &st) == 0) ? st.st_mtime : 0;
    if (app.wallpaperGalleryFolderSynced != key || app.wallpaperGalleryFolderMtime != curMtime) {
      needsRescan = true;
      app.wallpaperGalleryFolderSynced = key;
      app.wallpaperGalleryFolderMtime = curMtime;
    }
  }
  if (needsRescan || app.wallpaperGalleryPaths.empty() || !app.wallpaperGalleryValid) {
    eh::wallpaper::WallpaperThumbnailService::instance().release_all();
    wallpaper_destroy_thumbs(app);
    app.wallpaperGalleryPaths = eh::wallpaper::scan_image_files(key.empty() ? app.settings.wallpaperFolder : key);
    WP_LOG("rescan: found %zu files", app.wallpaperGalleryPaths.size());
    app.wallpaperGalleryMtimeCache.clear();
    app.wallpaperGalleryPage = 0;
    app.wallpaperGallerySortModeApplied = -1;
    app.wallpaperGalleryPrecached = false;
    app.wallpaperGalleryValid = true;
  } else {
    app.wallpaperGalleryValid = true;
  }
  if (app.wallpaperGallerySortMode != app.wallpaperGallerySortModeApplied) {
    wallpaper_sort_paths_inplace(app.wallpaperGalleryPaths, app.wallpaperGallerySortMode, app.wallpaperGalleryMtimeCache);
    app.wallpaperGallerySortModeApplied = app.wallpaperGallerySortMode;
  }

  if (!app.wallpaperGalleryPrecached && !app.wallpaperGalleryPaths.empty()) {
    app.wallpaperGalleryPrecached = true;
    for (const auto& fp : app.wallpaperGalleryPaths) {
      eh::wallpaper::WallpaperThumbnailService::instance().request(fp, 512);
    }
  }
}

static int wp_tab_logical_bottom_px(App& app, int contentX, int contentW) {
  WP_SCOPE();
  // Guarded: only full work if not valid (folder change etc.). Paint path uses lighter visible requests.
  if (!app.wallpaperGalleryValid || app.wallpaperGalleryPaths.empty()) {
    ensure_wallpaper_gallery(app);
  }
  const WallpaperVerticalMetrics wm =
      wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW),
                                 app.wallpaperViewOptionsOpen);
  const int cardInsetXi = contentX + 8;
  const WallpaperTabLayout wl = wallpaper_tab_layout(app, cardInsetXi, contentW, wm.galleryGridTop);
  if (app.wallpaperUiSubTab == 1) {
    return wm.subTabY + 48 + 88 + 28 + 12 + 5 * 48 + 8 + 24;
  }
  const int rowStride = wl.thumbH + kWpThumbLabelH + 6;
  const int gridBottom = wl.galleryTop + wl.rows * rowStride;
  if (wl.showNav) return wl.navY + WallpaperTabLayout::kNavH + 20;
  return gridBottom + 28;
}

void settings_clamp_wallpaper_scroll_px(App& app) {
  WP_SCOPE();
  const int contentX = kSpacingL + kSidebarW + kSpacingL;
  const int contentW = app.width - contentX - kSpacingL;
  const int viewH = app.height - kContentTop - kSpacingL;
  const int logicalExtent = wp_tab_logical_bottom_px(app, contentX, contentW) - kContentTop + 12;
  const int mx = std::max(0, logicalExtent - viewH);
  app.settingsWallpaperScrollPx = std::clamp(app.settingsWallpaperScrollPx, 0, mx);
}

void wallpaper_invalidate_hero(App& app) {
  WP_SCOPE();
  if (app.wallpaperHeroSurf) {
    cairo_surface_destroy(app.wallpaperHeroSurf);
    app.wallpaperHeroSurf = nullptr;
  }
  app.wallpaperHeroPath.clear();
}

static void wallpaper_ensure_hero_surface(App& app) {
  WP_SCOPE();
  const std::string& img = app.settings.wallpaperImage;
  if (img.empty()) {
    wallpaper_invalidate_hero(app);
    return;
  }
  if (app.wallpaperHeroPath == img && app.wallpaperHeroSurf &&
      cairo_surface_status(app.wallpaperHeroSurf) == CAIRO_STATUS_SUCCESS)
    return;
  if (!app.wallpaperHeroRequest.empty()) {
    if (app.wallpaperHeroRequest == img) return;
    app.wallpaperHeroRequest.clear();
  }
  app.wallpaperHeroRequest = img;
  eh::wallpaper::WallpaperThumbnailService::instance().request(img, 512);
}

void wallpaper_cycle_selection(App& app, int delta) {
  WP_SCOPE();
  WP_LOG("delta=%d paths=%zu", delta, app.wallpaperGalleryPaths.size());
  if (!app.wallpaperGalleryValid || app.wallpaperGalleryPaths.empty()) {
    ensure_wallpaper_gallery(app);
  }
  if (app.wallpaperGalleryPaths.empty()) return;
  const std::string& cur = app.settings.wallpaperImage;
  auto it = std::find(app.wallpaperGalleryPaths.begin(), app.wallpaperGalleryPaths.end(), cur);
  size_t idx = 0;
  if (it != app.wallpaperGalleryPaths.end()) idx = static_cast<size_t>(it - app.wallpaperGalleryPaths.begin());
  const size_t n = app.wallpaperGalleryPaths.size();
  idx = (idx + static_cast<size_t>(delta) + n) % n;
  app.settings.wallpaperImage = app.wallpaperGalleryPaths[idx];
  app.settings.wallpaperEnabled = true;
}

bool wallpaper_thumb_needs_followup_frame(App& app) {
  WP_SCOPE();
  WP_LOG("thumbnail_count=%zu", app.wallpaperThumbs.size());
  if (app.activeTab != 6) return false;
  if (app.wallpaperUiSubTab != 0) return false;
  WP_LOG("need_followup paths=%zu", app.wallpaperGalleryPaths.size());
  if (!app.wallpaperGalleryValid || app.wallpaperGalleryPaths.empty()) {
    ensure_wallpaper_gallery(app);
  }
  if (app.wallpaperGalleryPaths.empty()) return false;
  const int contentX = kSpacingL + kSidebarW + kSpacingL;
  const int contentW = app.width - contentX - kSpacingL;
  const int cardInsetXi = contentX + 8;
  const WallpaperVerticalMetrics vm = wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW),
                                                             app.wallpaperViewOptionsOpen);
  const WallpaperTabLayout wl = wallpaper_tab_layout(app, cardInsetXi, contentW, vm.galleryGridTop);
  wallpaper_clamp_page(app, wl.perPage);
  const size_t gallOff = static_cast<size_t>(std::max(0, app.wallpaperGalleryPage)) * static_cast<size_t>(wl.perPage);
  const size_t nTotal = app.wallpaperGalleryPaths.size();
  const size_t nSlots =
      (nTotal <= gallOff) ? static_cast<size_t>(0) : std::min(static_cast<size_t>(wl.perPage), nTotal - gallOff);
  for (size_t ii = 0; ii < nSlots; ++ii) {
    const std::string& fp = app.wallpaperGalleryPaths[gallOff + ii];
    if (app.wallpaperThumbs.find(fp) == app.wallpaperThumbs.end()) return true;
  }
  return eh::wallpaper::WallpaperThumbnailService::instance().has_pending_completed();
}

// Layout helpers.

WallpaperVerticalMetrics wallpaper_vertical_metrics(double contentX, double contentW, bool viewOpen) {
  WP_SCOPE();
  WallpaperVerticalMetrics m;
  m.subTabY = kContentTop + 96;
  // Full-width hero.
  m.heroX = contentX + kCardPad;
  m.heroY = m.subTabY + 52;
  m.heroW = static_cast<int>(contentW - 2.0 * static_cast<double>(kCardPad));
  m.heroH = kHeroH;

  // Floating toolbar overlaid at the hero bottom.
  m.toolBarH = kToolBarH;
  m.toolBarW = m.heroW - 24;
  m.toolBarX = static_cast<int>(m.heroX) + 12;
  m.toolBarY = m.heroY + kHeroH - 12 - kToolBarH;
  m.toolIconY = m.toolBarY + (kToolBarH - kToolBtnS) / 2;
  m.toolPrevX = m.toolBarX + 8;
  m.toolNextX = m.toolPrevX + kToolBtnS + 8;
  m.toolDeleteX = m.toolBarX + m.toolBarW - 8 - kToolBtnS;
  m.toolFolderX = m.toolDeleteX - 8 - kToolBtnS;
  m.toolTuneX = m.toolFolderX - 8 - kToolBtnS;
  m.toolComboW = kToolComboW;
  m.toolComboH = kToolComboH;
  m.toolComboX = m.toolTuneX - 8 - kToolComboW;
  m.toolComboY = m.toolBarY + (kToolBarH - kToolComboH) / 2;
  m.toolTextX = m.toolNextX + kToolBtnS + 12;
  m.toolTextW = std::max(40, m.toolComboX - 8 - m.toolTextX);

  // Primary button + gallery toolbar row.
  m.pickerY = m.heroY + kHeroH + 12;
  m.galleryToolbarY = m.pickerY + kPickerH + 16;
  m.sortPillsY = m.galleryToolbarY + (kGalleryBarH - 28) / 2;
  // Right-aligned: Name(72) Oldest(88) Newest(88) + tune(40), 8px gaps.
  m.sortPillsX = static_cast<int>(m.heroX) + m.heroW - (72 + 8 + 88 + 8 + 88 + 8 + 40);
  m.galleryTuneX = m.sortPillsX + 72 + 8 + 88 + 8 + 88 + 8;

  // Collapsible view-options panel.
  m.viewPanelY = m.galleryToolbarY + kGalleryBarH + 12;
  m.viewPanelH = viewOpen ? kViewPanelH : 0;
  m.galleryGridTop = m.viewPanelY + (viewOpen ? m.viewPanelH + 12 : 12);

  // Compat fields.
  m.galleryHeaderY = m.galleryToolbarY;
  m.modeRowY = m.toolComboY;
  m.ctrlY = m.pickerY;
  m.ctrlW = m.heroW;
  return m;
}

// Geometry of one view-panel stepper cell (shared by paint + hit test).
static void wallpaper_view_cell_geom(const WallpaperVerticalMetrics& wm, int idx,
                                     int* outX, int* outY, int* outW, int* outH,
                                     int* outMinusX, int* outPlusX, int* outBtnY) {
  const int panelX = static_cast<int>(wm.heroX);
  const int cellW = (wm.heroW - 24 - 3 * 12) / 4;
  const int cx = panelX + 12 + idx * (cellW + 12);
  const int cy = wm.viewPanelY;
  if (outX) *outX = cx;
  if (outY) *outY = cy;
  if (outW) *outW = cellW;
  if (outH) *outH = kViewPanelH;
  if (outMinusX) *outMinusX = cx;
  if (outPlusX) *outPlusX = cx + cellW - 44;
  if (outBtnY) *outBtnY = cy + 96;
}

void wallpaper_mode_combo_geom(int contentX, int contentW, int modeRowY,
                               int* outX, int* outY, int* outW, int* outH) {
  WP_SCOPE();
  (void)modeRowY;
  const WallpaperVerticalMetrics m =
      wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
  *outX = m.toolComboX;
  *outY = m.toolComboY;
  *outW = m.toolComboW;
  *outH = m.toolComboH;
}

// Paint function.
void paint_wallpaper_tab(App& app, cairo_t* cr, int contentX, int contentW,
                         double cardX, double cardW, double glassOv,
                         double dockMatA, double paintPointerYOffset) {
  WP_SCOPE();
  WP_LOG("subTab=%d contentX=%d contentW=%d", app.wallpaperUiSubTab, contentX, contentW);
  const double pyH = app.pointerY + paintPointerYOffset;

  const WallpaperVerticalMetrics WM =
      wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW),
                                 app.wallpaperViewOptionsOpen);
  const int cardInsetXi = contentX + 8;
  // In paint we still call (cheap when valid) because visible thumb requests + precache logic live here.
  // Heavy rescan is avoided by the valid flag inside ensure.
  ensure_wallpaper_gallery(app);
  const WallpaperTabLayout WL =
      wallpaper_tab_layout(app, cardInsetXi, contentW, WM.galleryGridTop);
  wallpaper_clamp_page(app, WL.perPage);

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  const double wpTop = static_cast<double>(kContentTop);
  const double wpFillHView =
      static_cast<double>(app.height) - static_cast<double>(kContentTop) - static_cast<double>(kSpacingXL);
  const int bottomPx =
      app.wallpaperUiSubTab == 1
          ? (WM.subTabY + 48 + 88 + 28 + 12 + 5 * 48 + 8 + 24)
          : (WL.showNav ? (WL.navY + WallpaperTabLayout::kNavH + 20)
                        : (WL.galleryTop + WL.rows * (WL.thumbH + kWpThumbLabelH + 6) + 28));
  const double cardLogicalH =
      std::max(wpFillHView, static_cast<double>(bottomPx - kContentTop) + 12.0);

  settings_card(app, cr, cardX, wpTop, cardW, cardLogicalH, glassOv);

  // Title
  settings_show_text(cr, contentX + kCardPad, wpTop + 36, "Wallpaper", 17, 600, t_r, t_g, t_b, 0.95f);

  // Toggle
  const int wpTopI = kContentTop;
  (void)settings_toggle(app, cr, static_cast<int>(cardX), wpTopI, static_cast<int>(cardW), wpTop,
                        static_cast<double>(kWallpaperToggleBandH), app.settings.wallpaperEnabled,
                        dockMatA);

  // Sub-tab bar (underline style).
  {
    const int stX = contentX + kCardPad;
    const int stY = WM.subTabY;
    const int stH = 48;

    // Bottom border line
    cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.25);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, stX, stY + stH - 0.5);
    cairo_line_to(cr, stX + contentW - 2 * kCardPad, stY + stH - 0.5);
    cairo_stroke(cr);

    // Wallpaper tab text
    settings_show_text(cr, stX + 16, stY + stH / 2 + 6, "Wallpaper", 16, 500, t_r, t_g, t_b,
                       app.wallpaperUiSubTab == 0 ? 0.95f : 0.5f);
    if (app.wallpaperUiSubTab == 0) {
      cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.9);
      cairo_round_rect(cr, stX + 16, stY + stH - 3, 100, 3, 1.5);
      cairo_fill(cr);
    }

    // Gallery settings tab text
    const int tab1X = stX + 100 + 24;
    settings_show_text(cr, tab1X + 16, stY + stH / 2 + 6, "Gallery settings", 16, 500, t_r, t_g, t_b,
                       app.wallpaperUiSubTab == 1 ? 0.95f : 0.5f);
    if (app.wallpaperUiSubTab == 1) {
      cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.9);
      cairo_round_rect(cr, tab1X + 16, stY + stH - 3, 160, 3, 1.5);
      cairo_fill(cr);
    }
  }

  if (app.wallpaperUiSubTab == 1) {
    // Gallery Settings tab.
    const int gsX = contentX + kCardPad;
    const int gsW = std::min(640, contentW - 2 * kCardPad);
    const int gsY = WM.subTabY + 60;

    // Glass card
    {
      m3::Box glassCard;
      glassCard.setColor(1, 1, 1, 0.07f);
      glassCard.setRadius(24.0f);
      glassCard.setGeometry(gsX, gsY, gsW, 392);
      glassCard.setGlassy(true);
      glassCard.paint(cr);
      cairo_round_rect(cr, gsX, gsY, gsW, 392, 24.0);
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.25);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    // Heading
    settings_show_text(cr, gsX + 32, gsY + 36, "Grid Layout", 18, 600, t_r, t_g, t_b, 1.0f);
    settings_show_text(cr, gsX + 32, gsY + 56, "Session only \u2014 quick options also on the Wallpaper tab.", 12, 400, t_r, t_g, t_b, 0.55f);

    // Folder picker
    const int galPickerY = gsY + 88;
    const int galPickerH = 28;
    {
      const int pbx = gsX + 32;
      const int pbw = gsW - 64;
      const bool phov = point_in_rect(app.pointerX, pyH, pbx, galPickerY, pbw, galPickerH);
      m3::Button pickerBtn;
      pickerBtn.setMinSize(0, 0);
      pickerBtn.setLabel("Folder dialog (native / kdialog / \u2026)");
      pickerBtn.setGeometry(pbx, galPickerY, pbw, galPickerH);
      pickerBtn.setStyle(m3::Button::Style::Outlined);
      pickerBtn.setSize(m3::Button::Size::XS);
      pickerBtn.setAccentColor(a_r, a_g, a_b);
      pickerBtn.setOutlineColor(o_r, o_g, o_b);
      pickerBtn.setHovered(phov);
      pickerBtn.paint(cr);
    }

    // Settings rows
    const int row0 = galPickerY + galPickerH + 20;
    const int rowPitch = 48;
    const int btnW = 36;
    const int btnH = 28;
    const int minusX = gsX + gsW - 32 - 2 * btnW - 10;
    const int plusX = gsX + gsW - 32 - btnW;

    auto paint_gs_row = [&](int row, const char* label, const char* valStr) {
      const int ry = row0 + row * rowPitch;
      settings_show_text(cr, gsX + 32, ry + 20, label, 13, 500, t_r, t_g, t_b, 1.0f);
      settings_show_text(cr, gsX + 240, ry + 20, valStr, 13, 500, t_r, t_g, t_b, 1.0f);
      const bool hm = point_in_rect(app.pointerX, pyH, minusX, ry + 4, btnW, btnH);
      const bool hp = point_in_rect(app.pointerX, pyH, plusX, ry + 4, btnW, btnH);
      m3::Button mBtn;
      mBtn.setMinSize(0, 0);
      mBtn.setLabel("\u2212");
      mBtn.setGeometry(minusX, ry + 4, btnW, btnH);
      mBtn.setStyle(m3::Button::Style::Outlined);
      mBtn.setSize(m3::Button::Size::M);
      mBtn.setAccentColor(a_r, a_g, a_b);
      mBtn.setOutlineColor(o_r, o_g, o_b);
      mBtn.setHovered(hm);
      mBtn.paint(cr);
      m3::Button pBtn;
      pBtn.setMinSize(0, 0);
      pBtn.setLabel("+");
      pBtn.setGeometry(plusX, ry + 4, btnW, btnH);
      pBtn.setStyle(m3::Button::Style::Outlined);
      pBtn.setSize(m3::Button::Size::M);
      pBtn.setAccentColor(a_r, a_g, a_b);
      pBtn.setOutlineColor(o_r, o_g, o_b);
      pBtn.setHovered(hp);
      pBtn.paint(cr);
    };
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", app.wallpaperGalleryColumns);
    paint_gs_row(0, "Columns", buf);
    std::snprintf(buf, sizeof(buf), "%d", app.wallpaperGalleryRows);
    paint_gs_row(1, "Rows", buf);
    std::snprintf(buf, sizeof(buf), "%d%%", app.wallpaperGalleryScalePct);
    paint_gs_row(2, "Thumbnail scale", buf);
    std::snprintf(buf, sizeof(buf), "%d px", app.wallpaperGalleryThumbRadiusPx);
    paint_gs_row(3, "Corner radius", buf);

    // Opacity slider
    {
      const int sRow = 4;
      const int ry = row0 + sRow * rowPitch;
      const int trackH = 6;
      const int thumbR = 10;
      const int trackY = ry + rowPitch / 2 - trackH / 2;
      const int trackX = gsX + 240;
      const int trackW = gsW - 64 - 240;

      settings_show_text(cr, gsX + 32, ry + 20, "Window opacity", 13, 500, t_r, t_g, t_b, 1.0f);

      cairo_round_rect(cr, trackX, trackY, trackW, trackH, 3.0);
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.3);
      cairo_fill(cr);

      const int fillW = static_cast<int>(static_cast<double>(trackW) * static_cast<double>(app.wallpaperUiOpacityPct) / 100.0);
      if (fillW > 2) {
        cairo_round_rect(cr, trackX, trackY, fillW, trackH, 3.0);
        cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.85);
        cairo_fill(cr);
      }

      const int thumbCx = trackX + fillW;
      const int thumbCy = trackY + trackH / 2;
      cairo_set_source_rgba(cr, a_r, a_g, a_b, 1.0);
      cairo_arc(cr, thumbCx, thumbCy, thumbR, 0, 2.0 * M_PI);
      cairo_fill(cr);

      char pct_buf[8];
      std::snprintf(pct_buf, sizeof(pct_buf), "%d%%", app.wallpaperUiOpacityPct);
      settings_show_text(cr, gsX + gsW - 32 - 40, ry + 20, pct_buf, 13, 500, t_r, t_g, t_b, 1.0f);
    }
  } else {
    // Wallpaper tab: full-width hero, floating toolbar, gallery.
    wallpaper_ensure_hero_surface(app);

    const int heroX = static_cast<int>(WM.heroX);
    const int heroY = WM.heroY;
    const int heroW = WM.heroW;
    const int heroH = kHeroH;

    // Current-image index for the toolbar counter.
    size_t curIdx = 0;
    bool curFound = false;
    if (!app.settings.wallpaperImage.empty() && !app.wallpaperGalleryPaths.empty()) {
      auto it = std::find(app.wallpaperGalleryPaths.begin(), app.wallpaperGalleryPaths.end(),
                          app.settings.wallpaperImage);
      if (it != app.wallpaperGalleryPaths.end()) {
        curIdx = static_cast<size_t>(it - app.wallpaperGalleryPaths.begin());
        curFound = true;
      }
    }

    // ═══ HERO AREA (full width) ═══
    cairo_save(cr);
    cairo_round_rect(cr, heroX, heroY, heroW, heroH, kHeroRadius);
    cairo_clip(cr);
    paint_src_bg(app, cr, 0.94);
    cairo_paint(cr);
    if (app.wallpaperHeroSurf && cairo_surface_status(app.wallpaperHeroSurf) == CAIRO_STATUS_SUCCESS) {
      cairo_surface_flush(app.wallpaperHeroSurf);
      const int iw = cairo_image_surface_get_width(app.wallpaperHeroSurf);
      const int ih = cairo_image_surface_get_height(app.wallpaperHeroSurf);
      if (iw > 0 && ih > 0) {
        const double sc = std::max(heroW / static_cast<double>(iw), heroH / static_cast<double>(ih));
        const double dispW = static_cast<double>(iw) * sc;
        const double dispH = static_cast<double>(ih) * sc;
        const double ox = heroX + (heroW - dispW) * 0.5;
        const double oy = heroY + (heroH - dispH) * 0.5;
        cairo_translate(cr, ox, oy);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, app.wallpaperHeroSurf, 0, 0);
        cairo_paint(cr);
      }
    }
    cairo_restore(cr);

    // Hero edges: bright inner rim + faint outer hairline.
    {
      cairo_pattern_t* rim = cairo_pattern_create_linear(0, heroY, 0, heroY + heroH);
      cairo_pattern_add_color_stop_rgba(rim, 0.0, 1.0, 1.0, 1.0, 0.38 * glassOv);
      cairo_pattern_add_color_stop_rgba(rim, 0.35, 1.0, 1.0, 1.0, 0.14 * glassOv);
      cairo_pattern_add_color_stop_rgba(rim, 0.7, 1.0, 1.0, 1.0, 0.05 * glassOv);
      cairo_pattern_add_color_stop_rgba(rim, 1.0, 1.0, 1.0, 1.0, 0.12 * glassOv);
      cairo_set_source(cr, rim);
      cairo_set_line_width(cr, 1.0);
      cairo_round_rect(cr, heroX + 1, heroY + 1, heroW - 2, heroH - 2, kHeroRadius - 1.0);
      cairo_stroke(cr);
      cairo_pattern_destroy(rim);
    }
    cairo_round_rect(cr, heroX + 0.5, heroY + 0.5, heroW - 1, heroH - 1, kHeroRadius);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // Gradient overlays
    cairo_pattern_t* gTop = cairo_pattern_create_linear(heroX, heroY, heroX, heroY + 64);
    cairo_pattern_add_color_stop_rgba(gTop, 0, 0, 0, 0, 0.5);
    cairo_pattern_add_color_stop_rgba(gTop, 1, 0, 0, 0, 0);
    cairo_set_source(cr, gTop);
    cairo_rectangle(cr, heroX, heroY, heroW, 64);
    cairo_fill(cr);
    cairo_pattern_destroy(gTop);

    cairo_pattern_t* gBot = cairo_pattern_create_linear(heroX, heroY + heroH - 72, heroX, heroY + heroH);
    cairo_pattern_add_color_stop_rgba(gBot, 0, 0, 0, 0, 0);
    cairo_pattern_add_color_stop_rgba(gBot, 1, 0, 0, 0, 0.55);
    cairo_set_source(cr, gBot);
    cairo_rectangle(cr, heroX, heroY + heroH - 72, heroW, 72);
    cairo_fill(cr);
    cairo_pattern_destroy(gBot);

    // "In use" badge, top-left over the hero.
    if (!app.settings.wallpaperImage.empty()) {
      m3::Box heroBadge;
      heroBadge.setColor(s_r, s_g, s_b, 0.35f);
      heroBadge.setRadius(9.0f);
      heroBadge.setGeometry(static_cast<float>(heroX + 12), static_cast<float>(heroY + 12), 62.0f, 24.0f);
      heroBadge.setGlassy(true);
      heroBadge.paint(cr);
      settings_show_text(cr, heroX + 20, heroY + 29, "In use", 10, 500, 1, 1, 1, 1.0f);
    }

    // ═══ FLOATING TOOLBAR (glass bar overlaid at hero bottom) ═══
    {
      m3::Box toolBar;
      toolBar.setColor(0, 0, 0, 0.45f);
      toolBar.setRadius(14.0f);
      toolBar.setGeometry(WM.toolBarX, WM.toolBarY, WM.toolBarW, WM.toolBarH);
      toolBar.setGlassy(true);
      toolBar.paint(cr);

      auto paint_tool_icon = [&](int bx, const char* glyph, bool filled, bool hov) {
        m3::Button b;
        b.setMinSize(0, 0);
        b.setGlyph(glyph);
        b.setGeometry(bx, WM.toolIconY, kToolBtnS, kToolBtnS);
        b.setStyle(filled ? m3::Button::Style::Filled : m3::Button::Style::Outlined);
        b.setSize(m3::Button::Size::M);
        b.setAccentColor(a_r, a_g, a_b);
        b.setOutlineColor(1, 1, 1);
        b.setHovered(hov);
        b.paint(cr);
      };

      const bool hPrev = point_in_rect(app.pointerX, pyH, WM.toolPrevX, WM.toolIconY, kToolBtnS, kToolBtnS);
      const bool hNext = point_in_rect(app.pointerX, pyH, WM.toolNextX, WM.toolIconY, kToolBtnS, kToolBtnS);
      paint_tool_icon(WM.toolPrevX, "chevron_left", false, hPrev);
      paint_tool_icon(WM.toolNextX, "chevron_right", false, hNext);

      // Current file + position.
      {
        std::string name = app.settings.wallpaperImage.empty()
                               ? "No wallpaper selected"
                               : wallpaper_file_basename(app.settings.wallpaperImage);
        const int maxC = std::clamp(WM.toolTextW / 7, 12, 64);
        name = wallpaper_truncate_visual(name, static_cast<size_t>(maxC));
        settings_show_text(cr, WM.toolTextX, WM.toolBarY + 24, name.c_str(), 14, 600, 1, 1, 1, 0.95f);
        if (curFound && !app.wallpaperGalleryPaths.empty()) {
          char pos[48];
          std::snprintf(pos, sizeof(pos), "%zu / %zu", curIdx + 1, app.wallpaperGalleryPaths.size());
          settings_show_text(cr, WM.toolTextX, WM.toolBarY + 42, pos, 12, 400, 1, 1, 1, 0.65f);
        } else if (!app.wallpaperGalleryPaths.empty()) {
          char pos[48];
          std::snprintf(pos, sizeof(pos), "%zu wallpapers", app.wallpaperGalleryPaths.size());
          settings_show_text(cr, WM.toolTextX, WM.toolBarY + 42, pos, 12, 400, 1, 1, 1, 0.65f);
        }
      }

      // Fill-mode combo.
      {
        const int mm = std::clamp(app.settings.wallpaperMode, 0, 4);
        settings_paint_combo_closed(app, cr, WM.toolComboX, WM.toolComboY, WM.toolComboW, WM.toolComboH,
                                    glassOv, kWallpaperModeLabels[mm], app.wallpaperModeDropdownOpen,
                                    settings_scroll_px_int(app));
      }

      const bool hTune = point_in_rect(app.pointerX, pyH, WM.toolTuneX, WM.toolIconY, kToolBtnS, kToolBtnS);
      const bool hFolder = point_in_rect(app.pointerX, pyH, WM.toolFolderX, WM.toolIconY, kToolBtnS, kToolBtnS);
      const bool hDel = point_in_rect(app.pointerX, pyH, WM.toolDeleteX, WM.toolIconY, kToolBtnS, kToolBtnS);
      paint_tool_icon(WM.toolTuneX, "tune", app.wallpaperViewOptionsOpen, hTune);
      paint_tool_icon(WM.toolFolderX, "folder", false, hFolder);
      paint_tool_icon(WM.toolDeleteX, "delete", false, hDel);
    }

    // Full-width primary action under the hero.
    {
      const bool phov = point_in_rect(app.pointerX, pyH, heroX, WM.pickerY, heroW, kPickerH);
      m3::Button pickerBtn;
      pickerBtn.setMinSize(0, 0);
      pickerBtn.setLabel("Open Picker\u2026");
      pickerBtn.setGeometry(heroX, WM.pickerY, heroW, kPickerH);
      pickerBtn.setStyle(m3::Button::Style::Filled);
      pickerBtn.setSize(m3::Button::Size::M);
      pickerBtn.setAccentColor(a_r, a_g, a_b);
      pickerBtn.setOutlineColor(o_r, o_g, o_b);
      pickerBtn.setHovered(phov);
      pickerBtn.paint(cr);
    }

    // ═══ GALLERY SECTION ═══
    // Single toolbar row: folder path + count on the left, sort pills + view toggle right.
    {
      std::ostringstream hd;
      if (app.settings.wallpaperFolder.empty()) {
        hd << "No folder";
      } else {
        hd << app.settings.wallpaperFolder;
      }
      hd << "  \u00b7  " << app.wallpaperGalleryPaths.size() << " wallpapers";
      const int maxC = std::clamp((WM.sortPillsX - 12 - heroX) / 7, 16, 120);
      const std::string hds = wallpaper_truncate_visual(hd.str(), static_cast<size_t>(maxC));
      settings_show_text(cr, heroX, WM.galleryToolbarY + 21, hds.c_str(), 13, 500, t_r, t_g, t_b, 0.7f);
    }

    // Sort pills + view-options toggle (right aligned).
    {
      const int pillY = WM.sortPillsY;
      const int pillH = 28;
      const char* plab[] = {"Name", "Oldest", "Newest"};
      const int pww[] = {72, 88, 88};
      int px = WM.sortPillsX;
      for (int pi = 0; pi < 3; ++pi) {
        const bool sel = app.wallpaperGallerySortMode == pi;
        const bool ph = point_in_rect(app.pointerX, pyH, px, pillY, pww[pi], pillH);
        m3::Button pillBtn;
        pillBtn.setMinSize(0, 0);
        pillBtn.setLabel(plab[pi]);
        pillBtn.setGeometry(px, pillY, pww[pi], pillH);
        pillBtn.setStyle(sel ? m3::Button::Style::Filled : m3::Button::Style::Outlined);
        pillBtn.setSize(m3::Button::Size::XS);
        pillBtn.setAccentColor(a_r, a_g, a_b);
        pillBtn.setOutlineColor(o_r, o_g, o_b);
        pillBtn.setHovered(ph);
        pillBtn.paint(cr);
        px += pww[pi] + 8;
      }
      const bool hTune = point_in_rect(app.pointerX, pyH, WM.galleryTuneX, pillY, 40, pillH);
      m3::Button tuneBtn;
      tuneBtn.setMinSize(0, 0);
      tuneBtn.setGlyph("tune");
      tuneBtn.setGeometry(WM.galleryTuneX, pillY, 40, pillH);
      tuneBtn.setStyle(app.wallpaperViewOptionsOpen ? m3::Button::Style::Filled
                                                    : m3::Button::Style::Outlined);
      tuneBtn.setSize(m3::Button::Size::XS);
      tuneBtn.setAccentColor(a_r, a_g, a_b);
      tuneBtn.setOutlineColor(o_r, o_g, o_b);
      tuneBtn.setHovered(hTune);
      tuneBtn.paint(cr);
    }

    // Collapsible view-options panel.
    if (app.wallpaperViewOptionsOpen) {
      const int panelX = heroX;
      const int panelY = WM.viewPanelY;
      const int panelW = heroW;
      m3::Box panel;
      panel.setColor(1, 1, 1, 0.07f);
      panel.setRadius(20.0f);
      panel.setGeometry(panelX, panelY, panelW, kViewPanelH);
      panel.setGlassy(true);
      panel.paint(cr);
      cairo_round_rect(cr, panelX, panelY, panelW, kViewPanelH, 20.0);
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.25);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);

      settings_show_text(cr, panelX + 20, panelY + 30, "View options", 14, 600, t_r, t_g, t_b, 0.95f);
      settings_show_text(cr, panelX + panelW - 20 - 118, panelY + 30, "All settings \u2192", 12, 500,
                         a_r, a_g, a_b, 0.9f);

      static const char* kViewLabels[] = {"Columns", "Rows", "Scale", "Radius"};
      for (int vi = 0; vi < 4; ++vi) {
        int cx, cy, cw, ch, minusX, plusX, btnY;
        wallpaper_view_cell_geom(WM, vi, &cx, &cy, &cw, &ch, &minusX, &plusX, &btnY);
        settings_show_text(cr, cx, cy + 62, kViewLabels[vi], 12, 400, t_r, t_g, t_b, 0.6f);
        char valStr[16];
        if (vi == 0)
          std::snprintf(valStr, sizeof(valStr), "%d", app.wallpaperGalleryColumns);
        else if (vi == 1)
          std::snprintf(valStr, sizeof(valStr), "%d", app.wallpaperGalleryRows);
        else if (vi == 2)
          std::snprintf(valStr, sizeof(valStr), "%d%%", app.wallpaperGalleryScalePct);
        else
          std::snprintf(valStr, sizeof(valStr), "%d px", app.wallpaperGalleryThumbRadiusPx);
        const int valW = cw - 2 * 44 - 16;
        settings_show_text(cr, cx + 44 + 8, cy + 92, valStr, 15, 600, t_r, t_g, t_b, 1.0f);
        (void)valW;
        const bool hm = point_in_rect(app.pointerX, pyH, minusX, btnY, 44, 32);
        const bool hp = point_in_rect(app.pointerX, pyH, plusX, btnY, 44, 32);
        m3::Button mBtn;
        mBtn.setMinSize(0, 0);
        mBtn.setLabel("\u2212");
        mBtn.setGeometry(minusX, btnY, 44, 32);
        mBtn.setStyle(m3::Button::Style::Outlined);
        mBtn.setSize(m3::Button::Size::M);
        mBtn.setAccentColor(a_r, a_g, a_b);
        mBtn.setOutlineColor(o_r, o_g, o_b);
        mBtn.setHovered(hm);
        mBtn.paint(cr);
        m3::Button pBtn;
        pBtn.setMinSize(0, 0);
        pBtn.setLabel("+");
        pBtn.setGeometry(plusX, btnY, 44, 32);
        pBtn.setStyle(m3::Button::Style::Outlined);
        pBtn.setSize(m3::Button::Size::M);
        pBtn.setAccentColor(a_r, a_g, a_b);
        pBtn.setOutlineColor(o_r, o_g, o_b);
        pBtn.setHovered(hp);
        pBtn.paint(cr);
      }
    }

    // Thumbnail grid.
    {
      const double gx0d = static_cast<double>(cardInsetXi + kCardPad);
      const size_t gallOff = static_cast<size_t>(app.wallpaperGalleryPage * WL.perPage);
      const std::string& preferImg = app.settings.wallpaperImage;
      const int decodePx = std::clamp(std::max(WL.thumb, WL.thumbH), 64, 1600);

      if (!preferImg.empty() && app.wallpaperThumbs.find(preferImg) == app.wallpaperThumbs.end()) {
        eh::wallpaper::WallpaperThumbnailService::instance().request(preferImg, decodePx);
      }

      const size_t nSlots =
          (app.wallpaperGalleryPaths.size() <= gallOff)
              ? static_cast<size_t>(0)
              : std::min(static_cast<size_t>(WL.perPage), app.wallpaperGalleryPaths.size() - gallOff);

      const int rowStride = WL.thumbH + 6;
      const double tileRad = static_cast<double>(std::clamp(app.wallpaperGalleryThumbRadiusPx, 0, 48));

      WP_LOG("gallery_draw: page=%d cols=%d rows=%d nSlots=%zu thumbSize=%dx%d", app.wallpaperGalleryPage, WL.cols, WL.rows, nSlots, WL.thumb, WL.thumbH);
      for (size_t ii = 0; ii < nSlots; ++ii) {
        const size_t gi = gallOff + ii;
        const std::string& fp = app.wallpaperGalleryPaths[gi];
        const int row = static_cast<int>(ii / static_cast<size_t>(WL.cols));
        const int col = static_cast<int>(ii % static_cast<size_t>(WL.cols));
        const double gx = gx0d + static_cast<double>(col * (WL.thumb + WL.gap));
        const double gy = static_cast<double>(WL.galleryTop) + static_cast<double>(row * rowStride);

        cairo_surface_t* surf = nullptr;
        const auto thIt = app.wallpaperThumbs.find(fp);
        if (thIt != app.wallpaperThumbs.end()) surf = thIt->second;
        else
          eh::wallpaper::WallpaperThumbnailService::instance().request(fp, decodePx);

        const bool hovered = static_cast<int>(ii) == app.wallpaperHoveredSlot;
        const double hs = hovered ? static_cast<double>(app.wallpaperHoverScale) : 1.0;

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
          m3::Box badgeBox;
          badgeBox.setColor(s_r, s_g, s_b, 0.35f);
          badgeBox.setRadius(6.0f);
          badgeBox.setGeometry(static_cast<float>(gx + 6), static_cast<float>(gy + 6), 54.0f, 18.0f);
          badgeBox.setGlassy(true);
          badgeBox.paint(cr);
          settings_show_text(cr, gx + 10, gy + 19, "In use", 9, 400, t_r, t_g, t_b, 1.0f);
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

      // Page navigation
      if (WL.showNav) {
        const bool hPrev = point_in_rect(app.pointerX, pyH, WL.prevX, WL.navY, WL.kNavW, WL.kNavH);
        const bool hNext = point_in_rect(app.pointerX, pyH, WL.nextX, WL.navY, WL.kNavW, WL.kNavH);

        m3::Button prevBtn;
        prevBtn.setMinSize(0, 0);
        prevBtn.setLabel("Prev");
        prevBtn.setGeometry(WL.prevX, WL.navY, WL.kNavW, WL.kNavH);
        prevBtn.setStyle(m3::Button::Style::Outlined);
        prevBtn.setSize(m3::Button::Size::XS);
        prevBtn.setAccentColor(a_r, a_g, a_b);
        prevBtn.setOutlineColor(o_r, o_g, o_b);
        prevBtn.setHovered(hPrev);
        prevBtn.paint(cr);

        m3::Button nextBtn;
        nextBtn.setMinSize(0, 0);
        nextBtn.setLabel("Next");
        nextBtn.setGeometry(WL.nextX, WL.navY, WL.kNavW, WL.kNavH);
        nextBtn.setStyle(m3::Button::Style::Outlined);
        nextBtn.setSize(m3::Button::Size::XS);
        nextBtn.setAccentColor(a_r, a_g, a_b);
        nextBtn.setOutlineColor(o_r, o_g, o_b);
        nextBtn.setHovered(hNext);
        nextBtn.paint(cr);

        std::ostringstream os;
        const int pages = std::max(
            1, static_cast<int>((app.wallpaperGalleryPaths.size() + static_cast<size_t>(WL.perPage) - 1) /
                                static_cast<size_t>(WL.perPage)));
        os << static_cast<long>(app.wallpaperGalleryPage + 1) << " / " << pages;
        const std::string ps = os.str();
        settings_show_text(cr, cardX + cardW / 2.0, WL.navY + 18, ps.c_str(), 12, 400, t_r, t_g, t_b, 0.6f);
      }
    }
  }

  // Mode dropdown popup
  if (app.wallpaperUiSubTab == 0 && app.wallpaperModeDropdownOpen) {
    int ddCx, ddCy, ddCw, ddCh;
    wallpaper_mode_combo_geom(static_cast<int>(contentX), static_cast<int>(contentW), 0, &ddCx, &ddCy, &ddCw, &ddCh);
    const int ddLy = ddCy + ddCh + 2;
    settings_paint_combo_list_popup(app, cr, ddCx, ddLy, ddCw, kSettingsDdRowH, 5, kWallpaperModeLabels,
                                    app.settings.wallpaperMode, app.wallpaperModeDropdownHoverRow, glassOv, 0, 0, false);
  }
}

// Pointer-down handler.
bool settings_wallpaper_consume_pointer_down(App& app, int contentX, int contentW) {
  WP_SCOPE();
  WP_LOG("subTab=%d x=%f y=%f", app.wallpaperUiSubTab, app.pointerX, app.pointerY);
  const int cardInsetX = contentX + 8;
  const WallpaperVerticalMetrics wm =
      wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW),
                                 app.wallpaperViewOptionsOpen);
  const double pyL = app.pointerY + settings_scroll_px(app);

  const int heroX = static_cast<int>(wm.heroX);
  const int heroW = wm.heroW;

  // Sub-tab toggle
  {
    const int stX = contentX + kCardPad;
    const int stY = wm.subTabY;
    const int stH = 48;
    if (point_in_rect(app.pointerX, pyL, stX + 16, stY, 100, stH)) {
      app.wallpaperUiSubTab = 0;
      draw(app);
      return true;
    }
    if (point_in_rect(app.pointerX, pyL, stX + 100 + 24 + 16, stY, 160, stH)) {
      app.wallpaperUiSubTab = 1;
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

  if (app.wallpaperUiSubTab == 1) {
    // Gallery settings
    const int gsX = contentX + kCardPad;
    const int gsW = std::min(640, contentW - 2 * kCardPad);
    const int gsY = wm.subTabY + 60;
    const int galPickerY = gsY + 88;
    constexpr int kGalPickerBtnH = 28;
    if (point_in_rect(app.pointerX, pyL, gsX + 32, galPickerY, gsW - 64, kGalPickerBtnH)) {
      settings_close_mode_dropdowns(app);
      app.wallpaperFolderPickerOpen = true;
      draw(app);
      return true;
    }
    const int row0 = galPickerY + kGalPickerBtnH + 20;
    const int rowPitch = 48;
    const int btnW = 36;
    const int btnH = 28;
    const int minusX = gsX + gsW - 32 - 2 * btnW - 10;
    const int plusX = gsX + gsW - 32 - btnW;
    for (int r = 0; r < 4; ++r) {
      const int ry = row0 + r * rowPitch;
      if (point_in_rect(app.pointerX, pyL, minusX, ry + 4, btnW, btnH)) {
        if (r == 0)
          app.wallpaperGalleryColumns = std::max(1, app.wallpaperGalleryColumns - 1);
        else if (r == 1)
          app.wallpaperGalleryRows = std::max(1, app.wallpaperGalleryRows - 1);
        else if (r == 2)
          app.wallpaperGalleryScalePct = std::max(50, app.wallpaperGalleryScalePct - 5);
        else
          app.wallpaperGalleryThumbRadiusPx = std::max(0, app.wallpaperGalleryThumbRadiusPx - 1);
        draw(app);
        return true;
      }
      if (point_in_rect(app.pointerX, pyL, plusX, ry + 4, btnW, btnH)) {
        if (r == 0)
          app.wallpaperGalleryColumns = std::min(10, app.wallpaperGalleryColumns + 1);
        else if (r == 1)
          app.wallpaperGalleryRows = std::min(10, app.wallpaperGalleryRows + 1);
        else if (r == 2)
          app.wallpaperGalleryScalePct = std::min(200, app.wallpaperGalleryScalePct + 5);
        else
          app.wallpaperGalleryThumbRadiusPx = std::min(32, app.wallpaperGalleryThumbRadiusPx + 1);
        draw(app);
        return true;
      }
    }

    // Opacity slider drag init
    {
      constexpr int trackH = 6;
      const int sRy = row0 + 4 * rowPitch;
      const int sTrackX = gsX + 240;
      const int sTrackW = gsW - 64 - 240;
      const int sTrackY = sRy + rowPitch / 2 - trackH / 2;
      const int hitPad = 16;
      if (point_in_rect(app.pointerX, pyL, sTrackX - hitPad, sTrackY - hitPad, sTrackW + hitPad * 2, trackH + hitPad * 2)) {
        app.sliderDrag = 220;
        const double nt = slider_norm_from_x(app.pointerX, sTrackX, sTrackW);
        app.wallpaperUiOpacityPct = std::clamp(static_cast<int>(nt * 100.0 + 0.5), 10, 100);
        app.settingsSliderDragNormT = nt;
        draw(app);
        return true;
      }
    }

    return true;
  }

  // Toolbar: prev / next chevrons.
  if (point_in_rect(app.pointerX, pyL, wm.toolPrevX, wm.toolIconY, kToolBtnS, kToolBtnS)) {
    wallpaper_cycle_selection(app, -1);
    wallpaper_invalidate_hero(app);
    save_settings(app.settings);
    draw(app);
    return true;
  }
  if (point_in_rect(app.pointerX, pyL, wm.toolNextX, wm.toolIconY, kToolBtnS, kToolBtnS)) {
    wallpaper_cycle_selection(app, 1);
    wallpaper_invalidate_hero(app);
    save_settings(app.settings);
    draw(app);
    return true;
  }

  // Toolbar: fill-mode combo.
  {
    int comboX, comboY, comboW, comboH;
    wallpaper_mode_combo_geom(contentX, contentW, 0, &comboX, &comboY, &comboW, &comboH);
    if (point_in_rect(app.pointerX, pyL, comboX, comboY, comboW, comboH)) {
      settings_close_mode_dropdowns(app);
      app.wallpaperModeDropdownOpen = true;
      draw(app);
      return true;
    }
  }

  // Toolbar: tune / folder / delete.
  if (point_in_rect(app.pointerX, pyL, wm.toolTuneX, wm.toolIconY, kToolBtnS, kToolBtnS)) {
    app.wallpaperViewOptionsOpen = !app.wallpaperViewOptionsOpen;
    draw(app);
    return true;
  }
  if (point_in_rect(app.pointerX, pyL, wm.toolFolderX, wm.toolIconY, kToolBtnS, kToolBtnS)) {
    std::string picked{};
    if (eh::wallpaper::pick_folder(&picked, app.settings.wallpaperFolderPickerMode)) {
      app.settings.wallpaperFolder = std::move(picked);
      app.wallpaperGalleryFolderSynced.clear();
      app.wallpaperGalleryValid = false;
      ensure_wallpaper_gallery(app);
      WallpaperTabLayout wlF = wallpaper_tab_layout(app, cardInsetX, contentW, wm.galleryGridTop);
      wallpaper_clamp_page(app, wlF.perPage);
      save_settings(app.settings);
      draw(app);
    }
    return true;
  }
  if (point_in_rect(app.pointerX, pyL, wm.toolDeleteX, wm.toolIconY, kToolBtnS, kToolBtnS)) {
    app.settings.wallpaperImage.clear();
    app.settings.wallpaperEnabled = false;
    wallpaper_invalidate_hero(app);
    save_settings(app.settings);
    wallpaper_apply_if_digest_changed(app.settings);
    eh::config::shell_config_reload_from_disk_now();
    app.settings = load_settings();
    draw(app);
    return true;
  }

  // Full-width Open Picker button.
  if (point_in_rect(app.pointerX, pyL, heroX, wm.pickerY, heroW, kPickerH)) {
    settings_close_mode_dropdowns(app);
    app.wallpaperFolderPickerOpen = true;
    draw(app);
    return true;
  }

  // Gallery toolbar: view-options toggle.
  if (point_in_rect(app.pointerX, pyL, wm.galleryTuneX, wm.sortPillsY, 40, 28)) {
    app.wallpaperViewOptionsOpen = !app.wallpaperViewOptionsOpen;
    draw(app);
    return true;
  }

  // View-options panel: steppers + "All settings" link.
  if (app.wallpaperViewOptionsOpen) {
    const int linkX = heroX + heroW - 20 - 118;
    if (point_in_rect(app.pointerX, pyL, linkX, wm.viewPanelY + 8, 118, 26)) {
      app.wallpaperUiSubTab = 1;
      draw(app);
      return true;
    }
    for (int vi = 0; vi < 4; ++vi) {
      int cx, cy, cw, ch, minusX, plusX, btnY;
      wallpaper_view_cell_geom(wm, vi, &cx, &cy, &cw, &ch, &minusX, &plusX, &btnY);
      if (point_in_rect(app.pointerX, pyL, minusX, btnY, 44, 32)) {
        if (vi == 0)
          app.wallpaperGalleryColumns = std::max(1, app.wallpaperGalleryColumns - 1);
        else if (vi == 1)
          app.wallpaperGalleryRows = std::max(1, app.wallpaperGalleryRows - 1);
        else if (vi == 2)
          app.wallpaperGalleryScalePct = std::max(50, app.wallpaperGalleryScalePct - 5);
        else
          app.wallpaperGalleryThumbRadiusPx = std::max(0, app.wallpaperGalleryThumbRadiusPx - 1);
        draw(app);
        return true;
      }
      if (point_in_rect(app.pointerX, pyL, plusX, btnY, 44, 32)) {
        if (vi == 0)
          app.wallpaperGalleryColumns = std::min(10, app.wallpaperGalleryColumns + 1);
        else if (vi == 1)
          app.wallpaperGalleryRows = std::min(10, app.wallpaperGalleryRows + 1);
        else if (vi == 2)
          app.wallpaperGalleryScalePct = std::min(200, app.wallpaperGalleryScalePct + 5);
        else
          app.wallpaperGalleryThumbRadiusPx = std::min(32, app.wallpaperGalleryThumbRadiusPx + 1);
        draw(app);
        return true;
      }
    }
  }

  // Sort pills (right aligned in the gallery toolbar row).
  {
    const int pillY = wm.sortPillsY;
    const int pillH = 28;
    const int pww[] = {72, 88, 88};
    int px = wm.sortPillsX;
    for (int pi = 0; pi < 3; ++pi) {
      if (point_in_rect(app.pointerX, pyL, px, pillY, pww[pi], pillH)) {
        if (app.wallpaperGallerySortMode != pi) {
          app.wallpaperGallerySortMode = pi;
          app.wallpaperGalleryPage = 0;
          app.wallpaperGalleryValid = false;
          ensure_wallpaper_gallery(app);
        }
        draw(app);
        return true;
      }
      px += pww[pi] + 8;
    }
  }

  // Gallery thumb selection
  if (!app.wallpaperGalleryValid || app.wallpaperGalleryPaths.empty()) {
    ensure_wallpaper_gallery(app);
  }
  WallpaperTabLayout wlHit = wallpaper_tab_layout(app, cardInsetX, contentW, wm.galleryGridTop);
  wallpaper_clamp_page(app, wlHit.perPage);

  const double gx0Hit = static_cast<double>(cardInsetX + kCardPad);
  const size_t gallOffHit = static_cast<size_t>(app.wallpaperGalleryPage * wlHit.perPage);
  const int rowStrideHit = wlHit.thumbH + kWpThumbLabelH + 6;

  if (!app.wallpaperGalleryPaths.empty()) {
    for (size_t ii = 0; ii < static_cast<size_t>(wlHit.perPage); ii++) {
      const size_t gi = gallOffHit + ii;
      if (gi >= app.wallpaperGalleryPaths.size()) break;
      const int row = static_cast<int>(ii / static_cast<size_t>(wlHit.cols));
      const int col = static_cast<int>(ii % static_cast<size_t>(wlHit.cols));
      const double gx = gx0Hit + static_cast<double>(col * (wlHit.thumb + wlHit.gap));
      const double gy = static_cast<double>(wlHit.galleryTop) + static_cast<double>(row * rowStrideHit);
      const std::string& fp = app.wallpaperGalleryPaths[gi];
      if (point_in_rect(app.pointerX, pyL, static_cast<int>(std::floor(gx)), static_cast<int>(std::floor(gy)),
                        wlHit.thumb, wlHit.thumbH)) {
        WP_LOG("thumb_selected: idx=%zu path=%s", gi, fp.c_str());
        app.settings.wallpaperImage = fp;
        app.settings.wallpaperEnabled = true;
        wallpaper_invalidate_hero(app);
        save_settings(app.settings);
        draw(app);  // instant feedback before apply (spawn + possible matugen) and reload
        wallpaper_apply_if_digest_changed(app.settings);
        eh::config::shell_config_reload_from_disk_now();
        app.settings = load_settings();
        return true;
      }
    }
  }

  // Prev/Next navigation
  if (wlHit.showNav) {
    if (point_in_rect(app.pointerX, pyL, wlHit.prevX, wlHit.navY, WallpaperTabLayout::kNavW,
                      WallpaperTabLayout::kNavH)) {
      if (app.wallpaperGalleryPage > 0) {
        --app.wallpaperGalleryPage;
        draw(app);
      }
      return true;
    }
    if (point_in_rect(app.pointerX, pyL, wlHit.nextX, wlHit.navY, WallpaperTabLayout::kNavW,
                      WallpaperTabLayout::kNavH)) {
      const int pages = std::max(
          1, static_cast<int>((app.wallpaperGalleryPaths.size() + static_cast<size_t>(wlHit.perPage) - 1) /
                              static_cast<size_t>(wlHit.perPage)));
      if (app.wallpaperGalleryPage + 1 < pages) {
        ++app.wallpaperGalleryPage;
        draw(app);
      }
      return true;
    }
  }

  return false;
}
