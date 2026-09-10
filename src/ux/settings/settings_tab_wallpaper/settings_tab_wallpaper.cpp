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
static constexpr int kHeroH = 220;

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
      wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
  const int cardInsetXi = contentX + 8;
  const WallpaperTabLayout wl = wallpaper_tab_layout(app, cardInsetXi, contentW, wm.galleryGridTop);
  if (app.wallpaperUiSubTab == 1) {
    return wm.subTabY + 48 + 88 + 28 + 12 + 5 * 48 + 8 + 24 + 36 + 28 + 24;
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
  const WallpaperVerticalMetrics vm = wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
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

WallpaperVerticalMetrics wallpaper_vertical_metrics(double contentX, double contentW) {
  WP_SCOPE();
  WallpaperVerticalMetrics m;
  m.subTabY = kContentTop + 96;
  m.heroX = static_cast<double>(contentX + kCardPad);
  m.heroY = m.subTabY + 52;
  m.heroW = (contentW - 2.0 * static_cast<double>(kCardPad) - 12.0) * 8.0 / 12.0;
  m.heroW = std::max(m.heroW, 400.0);
  m.ctrlW = static_cast<int>(contentW - 2 * kCardPad - 12 - static_cast<int>(m.heroW));
  m.ctrlY = m.heroY;
  m.modeRowY = m.ctrlY + (kCtrlCardH - 28) / 2;
  m.galleryHeaderY = m.heroY + kHeroH + 12;
  m.sortPillsY = m.galleryHeaderY + 20;
  m.galleryGridTop = m.sortPillsY + 28 + 36;
  return m;
}

void wallpaper_mode_combo_geom(int contentX, int contentW, int modeRowY,
                              int* outX, int* outY, int* outW, int* outH) {
  WP_SCOPE();
  (void)modeRowY;
  const double heroW = (contentW - 2.0 * kCardPad - 12.0) * 8.0 / 12.0;
  const int ctrlX = contentX + kCardPad + static_cast<int>(heroW) + 12;
  const int ctrlW = contentW - 2 * kCardPad - 12 - static_cast<int>(heroW);
  const int comboW = std::min(110, ctrlW - 32);
  const int comboH = 28;
  *outX = ctrlX + ctrlW - 16 - comboW;
  *outY = kContentTop + 96 + 52 + (kCtrlCardH - comboH) / 2;
  *outW = comboW;
  *outH = comboH;
}

// Paint function.
void paint_wallpaper_tab(App& app, cairo_t* cr, int contentX, int contentW,
                         double cardX, double cardW, double glassOv,
                         double dockMatA, double paintPointerYOffset) {
  WP_SCOPE();
  WP_LOG("subTab=%d contentX=%d contentW=%d", app.wallpaperUiSubTab, contentX, contentW);
  const double pyH = app.pointerY + paintPointerYOffset;

  const WallpaperVerticalMetrics WM =
      wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
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
          ? (WM.subTabY + 48 + 88 + 28 + 12 + 5 * 48 + 8 + 24 + 36 + 28 + 24)
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
      glassCard.setGeometry(gsX, gsY, gsW, 480);
      glassCard.setGlassy(true);
      glassCard.paint(cr);
      cairo_round_rect(cr, gsX, gsY, gsW, 480, 24.0);
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.25);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    // Heading
    settings_show_text(cr, gsX + 32, gsY + 36, "Grid Layout", 18, 600, t_r, t_g, t_b, 1.0f);
    settings_show_text(cr, gsX + 32, gsY + 56, "Session only \u2014 not written to shell config.", 12, 400, t_r, t_g, t_b, 0.55f);

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

    // Separator + folder dialog section
    {
      const int sepY = row0 + 5 * rowPitch + 8;
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.2);
      cairo_move_to(cr, gsX + 32, sepY);
      cairo_line_to(cr, gsX + gsW - 32, sepY);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);

      const int fdgY = sepY + 24;
      settings_show_text(cr, gsX + 32, fdgY + 14, "Folder dialog", 12, 400, t_r, t_g, t_b, 0.55f);

      const int fdgBoxY = fdgY + 28;
      const int fdgBoxH = 36;
      m3::Box fdgBox;
      fdgBox.setColor(0, 0, 0, 0.25f);
      fdgBox.setRadius(14.0f);
      fdgBox.setGeometry(gsX + 32, fdgBoxY, gsW - 64, fdgBoxH);
      fdgBox.setGlassy(true);
      fdgBox.paint(cr);
      cairo_round_rect(cr, gsX + 32, fdgBoxY, gsW - 64, fdgBoxH, 14.0);
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.3);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
      settings_show_text(cr, gsX + 48, fdgBoxY + 22, "Folder dialog (native / kdialog / \u2026)", 12, 400, t_r, t_g, t_b, 0.5f);
    }
  } else {
    // Wallpaper tab (hero, controls, gallery).
    wallpaper_ensure_hero_surface(app);

    const int heroX = static_cast<int>(WM.heroX);
    const int heroY = WM.heroY;
    const int heroW = static_cast<int>(WM.heroW);
    const int heroH = kHeroH;
    const int ctrlY = WM.ctrlY;
    const int ctrlX = static_cast<int>(contentX + kCardPad + WM.heroW + 12);
    const int ctrlW = contentW - 2 * kCardPad - 12 - static_cast<int>(WM.heroW);

    // ═══ HERO AREA (left) ═══
    cairo_save(cr);
    cairo_round_rect(cr, heroX, heroY, heroW, heroH, 16.0);
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

    // Hero border
    cairo_round_rect(cr, heroX, heroY, heroW, heroH, 16.0);
    cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.25);
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

    // Hero nav arrows
    {
      const int navSize = 36;
      const int nOff = 16;
      const int navY = heroY + heroH - navSize - nOff;
      const int nPad = 8;
      const bool hL = point_in_rect(app.pointerX, pyH, heroX + nPad, navY, navSize, navSize);
      const bool hR = point_in_rect(app.pointerX, pyH, heroX + heroW - navSize - nPad, navY, navSize, navSize);

      cairo_round_rect(cr, heroX + nPad, navY, navSize, navSize, 10.0);
      cairo_set_source_rgba(cr, 0, 0, 0, hL ? 0.7 : 0.5);
      cairo_fill(cr);
      material_symbols_draw_glyph(cr, heroX + nPad + navSize / 2.0, navY + navSize / 2.0,
                                  18, "chevron_left", 1, 1, 1, 0.85);

      cairo_round_rect(cr, heroX + heroW - navSize - nPad, navY, navSize, navSize, 10.0);
      cairo_set_source_rgba(cr, 0, 0, 0, hR ? 0.7 : 0.5);
      cairo_fill(cr);
      material_symbols_draw_glyph(cr, heroX + heroW - navSize - nPad + navSize / 2.0, navY + navSize / 2.0,
                                  18, "chevron_right", 1, 1, 1, 0.85);
    }

    // ═══ CONTROLS PANEL (right) ═══
    // Fill Mode card
    {
      m3::Box fillCard;
      fillCard.setColor(1, 1, 1, 0.07f);
      fillCard.setRadius(kCtrlCardRad);
      fillCard.setGeometry(ctrlX, ctrlY, ctrlW, kCtrlCardH);
      fillCard.setGlassy(true);
      fillCard.paint(cr);

      cairo_round_rect(cr, ctrlX, ctrlY, ctrlW, kCtrlCardH, kCtrlCardRad);
      cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.25);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);

      settings_show_text(cr, ctrlX + 16, ctrlY + 22, "Fill Mode", 13, 400, t_r, t_g, t_b, 0.55f);

      const int comboW = std::min(110, ctrlW - 32);
      const int comboH = 28;
      const int comboX = ctrlX + ctrlW - 16 - comboW;
      const int comboY = ctrlY + (kCtrlCardH - comboH) / 2;
      const int mm = std::clamp(app.settings.wallpaperMode, 0, 4);
      settings_paint_combo_closed(app, cr, comboX, comboY, comboW, comboH, glassOv,
                                  kWallpaperModeLabels[mm], app.wallpaperModeDropdownOpen,
                                  settings_scroll_px_int(app));
    }

    // Open Picker button (Filled style)
    {
      const int gap = 12;
      const int btnY = ctrlY + kCtrlCardH + gap;
      const bool phov = point_in_rect(app.pointerX, pyH, ctrlX, btnY, ctrlW, kOpenPickerH);
      m3::Button pickerBtn;
      pickerBtn.setMinSize(0, 0);
      pickerBtn.setLabel("Open Picker...");
      pickerBtn.setGeometry(ctrlX, btnY, ctrlW, kOpenPickerH);
      pickerBtn.setStyle(m3::Button::Style::Filled);
      pickerBtn.setSize(m3::Button::Size::M);
      pickerBtn.setAccentColor(a_r, a_g, a_b);
      pickerBtn.setOutlineColor(o_r, o_g, o_b);
      pickerBtn.setHovered(phov);
      pickerBtn.paint(cr);
    }

    // Action buttons (folder, grid_view, delete)
    {
      const int gap = 12;
      const int actY = ctrlY + kCtrlCardH + gap + kOpenPickerH + gap;
      const int actBtnW = (ctrlW - 8) / 3;
      static const char* kActGlyphs[] = {"folder", "grid_view", "delete"};
      for (int ai = 0; ai < 3; ++ai) {
        const int ax = ctrlX + ai * (actBtnW + 4);
        const bool ahov = point_in_rect(app.pointerX, pyH, ax, actY, actBtnW, kActBtnH);
        m3::Button actBtn;
        actBtn.setMinSize(0, 0);
        actBtn.setGlyph(kActGlyphs[ai]);
        actBtn.setGeometry(ax, actY, actBtnW, kActBtnH);
        actBtn.setStyle(m3::Button::Style::Outlined);
        actBtn.setSize(m3::Button::Size::M);
        actBtn.setAccentColor(a_r, a_g, a_b);
        actBtn.setOutlineColor(o_r, o_g, o_b);
        actBtn.setHovered(ahov);
        actBtn.paint(cr);
      }
    }

    // ═══ GALLERY SECTION ═══
    const int galleryY = WM.galleryHeaderY;

    // Gallery header: full folder path + count
    {
      std::ostringstream hd;
      if (app.settings.wallpaperFolder.empty()) {
        hd << "No folder";
      } else {
        hd << app.settings.wallpaperFolder;
      }
      hd << "  \u00b7  " << app.wallpaperGalleryPaths.size() << " wallpapers";
      const std::string hds = hd.str();
      settings_show_text(cr, contentX + kCardPad, galleryY + 4, hds.c_str(), 13, 400, t_r, t_g, t_b, 0.55f);
    }

    // Sort pills
    {
      const int pillY = WM.sortPillsY;
      const int pillH = 28;
      const char* plab[] = {"Name", "Oldest", "Newest"};
      int px = contentX + kCardPad;
      for (int pi = 0; pi < 3; ++pi) {
        const int pww = (pi == 0 ? 72 : 88);
        const bool sel = app.wallpaperGallerySortMode == pi;
        const bool ph = point_in_rect(app.pointerX, pyH, px, pillY, pww, pillH);
        m3::Button pillBtn;
        pillBtn.setMinSize(0, 0);
        pillBtn.setLabel(plab[pi]);
        pillBtn.setGeometry(px, pillY, pww, pillH);
        pillBtn.setStyle(sel ? m3::Button::Style::Filled : m3::Button::Style::Outlined);
        pillBtn.setSize(m3::Button::Size::XS);
        pillBtn.setAccentColor(a_r, a_g, a_b);
        pillBtn.setOutlineColor(o_r, o_g, o_b);
        pillBtn.setHovered(ph);
        pillBtn.paint(cr);
        px += pww + 8;
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
      wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
  const double pyL = app.pointerY + settings_scroll_px(app);

  const int heroW = static_cast<int>(wm.heroW);
  const int ctrlX = static_cast<int>(contentX + kCardPad + wm.heroW + 12);
  const int ctrlW = contentW - 2 * kCardPad - 12 - static_cast<int>(wm.heroW);

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

  // Fill Mode combo
  {
    const int comboW = std::min(110, ctrlW - 32);
    const int comboH = 28;
    const int comboX = ctrlX + ctrlW - 16 - comboW;
    const int comboY = wm.ctrlY + (kCtrlCardH - comboH) / 2;
    if (point_in_rect(app.pointerX, pyL, comboX, comboY, comboW, comboH)) {
      settings_close_mode_dropdowns(app);
      app.wallpaperModeDropdownOpen = true;
      draw(app);
      return true;
    }
  }

  // Open Picker button
  {
    const int gap = 12;
    const int btnY = wm.ctrlY + kCtrlCardH + gap;
    if (point_in_rect(app.pointerX, pyL, ctrlX, btnY, ctrlW, kOpenPickerH)) {
      settings_close_mode_dropdowns(app);
      app.wallpaperFolderPickerOpen = true;
      draw(app);
      return true;
    }
  }

  // Action buttons
  {
    const int gap = 12;
    const int actY = wm.ctrlY + kCtrlCardH + gap + kOpenPickerH + gap;
    const int actBtnW = (ctrlW - 8) / 3;
    for (int ai = 0; ai < 3; ++ai) {
      const int ax = ctrlX + ai * (actBtnW + 4);
      if (point_in_rect(app.pointerX, pyL, ax, actY, actBtnW, kActBtnH)) {
        if (ai == 0) {
          std::string picked{};
          if (eh::wallpaper::pick_folder(&picked, app.settings.wallpaperFolderPickerMode)) {
            app.settings.wallpaperFolder = std::move(picked);
            app.wallpaperGalleryFolderSynced.clear();
            app.wallpaperGalleryValid = false;
            ensure_wallpaper_gallery(app);
            WallpaperTabLayout wlF =
                wallpaper_tab_layout(app, cardInsetX, contentW, wm.galleryGridTop);
            wallpaper_clamp_page(app, wlF.perPage);
            save_settings(app.settings);
            draw(app);
          }
        } else if (ai == 1) {
          settings_close_mode_dropdowns(app);
          app.wallpaperFolderPickerOpen = true;
          draw(app);
        } else if (ai == 2) {
          app.settings.wallpaperImage.clear();
          app.settings.wallpaperEnabled = false;
          wallpaper_invalidate_hero(app);
          save_settings(app.settings);
          wallpaper_apply_if_digest_changed(app.settings);
          eh::config::shell_config_reload_from_disk_now();
          app.settings = load_settings();
          draw(app);
        }
        return true;
      }
    }
  }

  // Hero nav arrows
  {
    const int heroX = static_cast<int>(wm.heroX);
    const int heroY = wm.heroY;
    const int heroH = kHeroH;
    const int navSize = 36;
    const int nOff = 16;
    const int navY = heroY + heroH - navSize - nOff;
    const int nPad = 8;
    if (point_in_rect(app.pointerX, pyL, heroX + nPad, navY, navSize, navSize)) {
      WP_LOG("hero_nav: delta=%d", -1);
      wallpaper_cycle_selection(app, -1);
      wallpaper_invalidate_hero(app);
      save_settings(app.settings);
      draw(app);  // early for responsiveness; apply follows in some paths
      return true;
    }
    if (point_in_rect(app.pointerX, pyL, heroX + heroW - navSize - nPad, navY, navSize, navSize)) {
      WP_LOG("hero_nav: delta=%d", 1);
      wallpaper_cycle_selection(app, 1);
      wallpaper_invalidate_hero(app);
      save_settings(app.settings);
      draw(app);  // early for responsiveness; apply follows in some paths
      return true;
    }
  }

  // Sort pills
  {
    const int pillY = wm.sortPillsY;
    const int pillH = 28;
    int px = contentX + kCardPad;
    for (int pi = 0; pi < 3; ++pi) {
      const int pww = (pi == 0 ? 72 : 88);
      if (point_in_rect(app.pointerX, pyL, px, pillY, pww, pillH)) {
        if (app.wallpaperGallerySortMode != pi) {
          app.wallpaperGallerySortMode = pi;
          app.wallpaperGalleryPage = 0;
          app.wallpaperGalleryValid = false;
          ensure_wallpaper_gallery(app);
        }
        draw(app);
        return true;
      }
      px += pww + 8;
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
