#include "ux/Live_Wallpaper/app.hpp"
#include "ux/Live_Wallpaper/settings.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>

#include <sys/stat.h>

#include "wallpaper/apply/wallpaper_apply.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail_service.hpp"
// Cleaned for separation: no more main settings_tab_wallpaper or serialize headers.
#include "ux/Live_Wallpaper/ui/draw.hpp"

namespace eh::live_wallpaper {

static bool path_is_video(const std::string& p) {
  const size_t n = p.size();
  if (n < 4) return false;
  auto ci = [&](const char* suf) -> bool {
    size_t sn = 0;
    while (suf[sn]) ++sn;
    if (n < sn) return false;
    for (size_t i = 0; i < sn; ++i) {
      if (std::tolower(static_cast<unsigned char>(p[n - sn + i])) !=
          std::tolower(static_cast<unsigned char>(suf[i])))
        return false;
    }
    return true;
  };
  return ci(".mp4") || ci(".webm") || ci(".mkv") || ci(".mov") || ci(".avi");
}

// Pointer move.

void handle_pointer_move(AppState& app, int x, int y) {
  app.pointerX = static_cast<double>(x);
  app.pointerY = static_cast<double>(y);

  const LiveWallpaperLayout LW = compute_lw_layout(app.width, app.height);
  [[maybe_unused]] const int contentW = app.width - 2 * LW.marginX;

  if (app.liveWallpaperModeDropdownOpen && app.liveWallpaperUiSubTab == 0) {
    const int comboW = LW.comboW;
    const int comboH = LW.comboH;
    const int comboX = LW.ctrlX + LW.ctrlW - static_cast<int>(16.0 * LW.scale) - comboW;
    const int comboY = LW.contentStartY + (LW.fillCardH - comboH) / 2;
    const int ddLy = comboY + comboH + 2;
    const int popupH = 5 * kSettingsDdRowH;
    if (y >= ddLy && y < ddLy + popupH && x >= comboX && x < comboX + comboW) {
      const int nr = (y - ddLy) / kSettingsDdRowH;
      if (nr != app.liveWallpaperModeDropdownHoverRow) {
        app.liveWallpaperModeDropdownHoverRow = nr;
        draw(app);
      }
    } else if (app.liveWallpaperModeDropdownHoverRow != -1) {
      app.liveWallpaperModeDropdownHoverRow = -1;
      draw(app);
    }
    return;
  }

  // Gallery-thumb hover tracking (subtab 0).
  if (app.liveWallpaperUiSubTab == 0) {
    ensure_live_wallpaper_gallery(app);
    const int cardInsetXi = LW.marginX;

    // The original port computed a per-thumb grid from the LiveWallpaperLayout;
    // during the separation rework that math is stubbed out, so hover is left
    // cleared here and the main draw pipeline owns hover state.
    int approxThumb = 120;
    int approxPerPage = 20;
    (void)cardInsetXi; (void)approxThumb; (void)approxPerPage;

    int newHover = -1;
    std::string newHoverPath;
    size_t newHoverGalleryIdx = 0;

    if (newHover != app.liveWallpaperHoveredSlot || newHoverPath != app.liveWallpaperHoveredPath) {
      const bool wasHovering = app.liveWallpaperHoveredSlot >= 0;
      app.liveWallpaperHoveredSlot = newHover;
      app.liveWallpaperHoveredPath = newHoverPath;
      app.liveWallpaperHoverAnim.cancel_all();

      if (newHover >= 0 && path_is_video(newHoverPath))
        live_wallpaper_start_hover_video_preview(app, newHoverPath, newHoverGalleryIdx);
      else
        live_wallpaper_stop_hover_video_preview(app);

      if (newHover >= 0) {
        if (wasHovering) {
          app.liveWallpaperHoverScale = 1.10f;
        } else {
          app.liveWallpaperHoverAnim.animate(1.0f, 1.10f, 120.0f, eh::shell::Easing::EaseOutQuad,
              [&app](float v) { app.liveWallpaperHoverScale = v; },
              [&app]() {
                app.liveWallpaperHoverAnim.animate(1.10f, 1.0f, 180.0f, eh::shell::Easing::EaseOutCubic,
                    [&app](float v) { app.liveWallpaperHoverScale = v; }, nullptr);
              });
        }
      } else {
        app.liveWallpaperHoverAnim.animate(app.liveWallpaperHoverScale, 1.0f, 150.0f, eh::shell::Easing::EaseOutCubic,
            [&app](float v) { app.liveWallpaperHoverScale = v; }, nullptr);
      }
      draw(app);
    }
  }
}

// Click.

static void hit_test_gallery_thumb(AppState& app, int x, int y,
                                    const LiveWallpaperLayout& LW, int contentW,
                                    int& outIdx, std::string& outPath) {
  outIdx = -1;
  outPath.clear();
  if (app.liveWallpaperUiSubTab != 0) return;
  ensure_live_wallpaper_gallery(app);
  if (app.liveWallpaperGalleryPaths.empty()) return;

  const double s = std::min(static_cast<double>(app.width) / 1920.0,
                            static_cast<double>(app.height) / 1080.0);
  const double clampedS = std::clamp(s, 0.4, 2.5);
  const int gap = std::max(4, static_cast<int>(kWpThumbGap * clampedS));
  const int availW = contentW - 8 - 2 * kCardPad;

  const double scalePct = static_cast<double>(std::clamp(app.liveWallpaperGalleryScalePct, 50, 200)) / 100.0;
  int targetW = static_cast<int>(420.0 * scalePct);
  targetW = std::clamp(targetW, 60, 800);
  const int cols = std::max(1, (availW + gap) / (targetW + gap));
  int thumbW = (availW - gap * (cols - 1)) / cols;
  thumbW = std::max(40, thumbW);
  const int thumbH = (thumbW * 9 + 8) / 16;
  const int rowStride = thumbH + gap;
  const int galleryGridY = LW.galleryGridY;
  const int availH = app.height - galleryGridY - static_cast<int>(40.0 * clampedS);
  const int rows = std::max(1, availH / rowStride);
  const int perPage = cols * rows;

  const size_t gallOff = static_cast<size_t>(std::max(0, app.liveWallpaperGalleryPage)) * static_cast<size_t>(perPage);
  const size_t nTotal = app.liveWallpaperGalleryPaths.size();
  if (gallOff >= nTotal) return;
  const size_t nSlots = std::min(static_cast<size_t>(perPage), nTotal - gallOff);

  for (size_t ii = 0; ii < nSlots; ++ii) {
    const int row = static_cast<int>(ii / static_cast<size_t>(cols));
    const int col = static_cast<int>(ii % static_cast<size_t>(cols));
    const int gx = LW.marginX + col * (thumbW + gap);
    const int gy = galleryGridY + row * rowStride;
    if (point_in_rect(x, y, gx, gy, thumbW, thumbH)) {
      outIdx = static_cast<int>(ii);
      outPath = app.liveWallpaperGalleryPaths[gallOff + ii];
      return;
    }
  }
}

void handle_click(AppState& app, int x, int y, int button) {
  app.pointerX = static_cast<double>(x);
  app.pointerY = static_cast<double>(y);

  const LiveWallpaperLayout LW = compute_lw_layout(app.width, app.height);
  [[maybe_unused]] const int contentW = app.width - 2 * LW.marginX;

  // Right-click (button 273 = BTN_RIGHT).
  if (button == 273) {
    // If context menu is open, close it
    if (app.liveWallpaperContextMenuOpen) {
      app.liveWallpaperContextMenuOpen = false;
      draw(app);
      return;
    }
    // Hit-test gallery thumbs
    int hitIdx = -1;
    std::string hitPath;
    hit_test_gallery_thumb(app, x, y, LW, contentW, hitIdx, hitPath);
    if (!hitPath.empty()) {
      app.liveWallpaperContextMenuOpen = true;
      app.liveWallpaperContextMenuX = x;
      app.liveWallpaperContextMenuY = y;
      app.liveWallpaperContextMenuPath = hitPath;
      draw(app);
    }
    return;
  }

  // Left-click (button 272 = BTN_LEFT) or others.
  // Dismiss context menu if open
  if (app.liveWallpaperContextMenuOpen) {
    constexpr int kCtxW = 180;
    constexpr int kCtxH = 36;
    if (point_in_rect(x, y, app.liveWallpaperContextMenuX, app.liveWallpaperContextMenuY, kCtxW, kCtxH)) {
      const std::string& path = app.liveWallpaperContextMenuPath;
      bool& isAdult = app.liveWallpaperAdultFlags[path];
      isAdult = !isAdult;
      // Remove blurred disk cache so it's regenerated on next view
      lw_remove_blurred_thumb_cache(path);
      auto bit = app.liveWallpaperBlurredThumbs.find(path);
      if (bit != app.liveWallpaperBlurredThumbs.end()) {
        cairo_surface_destroy(bit->second);
        app.liveWallpaperBlurredThumbs.erase(bit);
      }
      lw_save_ui_config(app);
    }
    app.liveWallpaperContextMenuOpen = false;
    draw(app);
    return;
  }

  // Sub-tab toggle
  {
    if (point_in_rect(x, y, LW.marginX, LW.tabY, LW.tab0W, LW.tabH)) {
      app.liveWallpaperUiSubTab = 0;
      draw(app);
      return;
    }
    if (point_in_rect(x, y, LW.marginX + LW.tab0W, LW.tabY, LW.tab1W, LW.tabH)) {
      app.liveWallpaperUiSubTab = 1;
      draw(app);
      return;
    }
  }

  // Gallery settings sub-tab
  if (app.liveWallpaperUiSubTab == 1) {
    const int gsX = LW.marginX;
    const int gsW = std::min(640, contentW);
    const int gsY = LW.contentStartY;

    const int galPickerY = gsY + 88;
    constexpr int galPickerH = 28;
    if (point_in_rect(x, y, gsX + 32, galPickerY, gsW - 64, galPickerH)) {
      app.liveWallpaperModeDropdownOpen = false;
      std::string picked{};
      if (::eh::wallpaper::pick_folder(&picked, app.config.folderPickerMode)) {
        app.config.folder = std::move(picked);
        app.liveWallpaperGalleryFolderSynced.clear();
        ensure_live_wallpaper_gallery(app);
        struct LocalLayout { int perPage = 20; };
        LocalLayout wlF{};
        (void)wlF;
        lw_save_settings(app.config);
      }
      draw(app);
      return;
    }

    const int row0 = galPickerY + galPickerH + 20;
    const int rowPitch = 48;
    const int btnW = 36;
    const int btnH = 28;
    const int minusX = gsX + gsW - 32 - 2 * btnW - 10;
    const int plusX = gsX + gsW - 32 - btnW;
    constexpr int kGalSliderRow = 4;
    for (int r = 0; r < kGalSliderRow; ++r) {
      const int ry = row0 + r * rowPitch;
      if (point_in_rect(x, y, minusX, ry + 4, btnW, btnH)) {
        if (r == 0) app.liveWallpaperGalleryColumns = std::max(1, app.liveWallpaperGalleryColumns - 1);
        else if (r == 1) app.liveWallpaperGalleryRows = std::max(1, app.liveWallpaperGalleryRows - 1);
        else if (r == 2) app.liveWallpaperGalleryScalePct = std::max(50, app.liveWallpaperGalleryScalePct - 5);
        else app.liveWallpaperGalleryThumbRadiusPx = std::max(0, app.liveWallpaperGalleryThumbRadiusPx - 1);
        lw_save_ui_config(app);
        draw(app);
        return;
      }
      if (point_in_rect(x, y, plusX, ry + 4, btnW, btnH)) {
        if (r == 0) app.liveWallpaperGalleryColumns = std::min(10, app.liveWallpaperGalleryColumns + 1);
        else if (r == 1) app.liveWallpaperGalleryRows = std::min(10, app.liveWallpaperGalleryRows + 1);
        else if (r == 2) app.liveWallpaperGalleryScalePct = std::min(200, app.liveWallpaperGalleryScalePct + 5);
        else app.liveWallpaperGalleryThumbRadiusPx = std::min(32, app.liveWallpaperGalleryThumbRadiusPx + 1);
        lw_save_ui_config(app);
        draw(app);
        return;
      }
    }

    // Opacity slider (row 4)
    {
      const int ry = row0 + kGalSliderRow * rowPitch;
      const int trackX = gsX + 240;
      const int trackW = gsW - 64 - 240;
      const int trackH = 26;
      const int trackTop = ry + rowPitch / 2 - trackH / 2;
      if (point_in_rect(x, y, trackX, trackTop, trackW, trackH)) {
        const int clickOffset = x - trackX;
        int pct = static_cast<int>(static_cast<double>(clickOffset) * 100.0 / static_cast<double>(std::max(1, trackW)));
        app.liveWallpaperUiOpacityPct = std::clamp(pct, 0, 100);
        lw_save_ui_config(app);
        draw(app);
        return;
      }
    }
    return;
  }

  // Mode combo (inside Fill Mode card in controls panel)
  {
    const int comboX = LW.ctrlX + LW.ctrlW - static_cast<int>(16.0 * LW.scale) - LW.comboW;
    const int comboY = LW.contentStartY + (LW.fillCardH - LW.comboH) / 2;
    if (point_in_rect(x, y, comboX, comboY, LW.comboW, LW.comboH)) {
      app.liveWallpaperModeDropdownOpen = !app.liveWallpaperModeDropdownOpen;
      draw(app);
      return;
    }
  }

  // "Open Picker..." button in controls panel
  {
    const int btnY = LW.contentStartY + LW.fillCardH + static_cast<int>(12.0 * LW.scale);
    if (point_in_rect(x, y, LW.ctrlX, btnY, LW.ctrlW, LW.openPickerH)) {
      app.liveWallpaperModeDropdownOpen = false;
      std::string picked{};
      if (::eh::wallpaper::pick_folder(&picked, app.config.folderPickerMode)) {
        app.config.folder = std::move(picked);
        app.liveWallpaperGalleryFolderSynced.clear();
        ensure_live_wallpaper_gallery(app);
        struct LocalLayout { int perPage = 20; };
        LocalLayout wlF{};
        (void)wlF;
        lw_save_settings(app.config);
      }
      draw(app);
      return;
    }
  }

  // Action buttons (folder, grid_view, delete)
  {
    const int actY = LW.contentStartY + LW.fillCardH + static_cast<int>(12.0 * LW.scale) + LW.openPickerH + static_cast<int>(12.0 * LW.scale);
    const int actBtnW = (LW.ctrlW - static_cast<int>(8.0 * LW.scale)) / 3;
    for (int ai = 0; ai < 3; ++ai) {
      const int ax = LW.ctrlX + ai * (actBtnW + static_cast<int>(4.0 * LW.scale));
      if (point_in_rect(x, y, ax, actY, actBtnW, LW.actBtnH)) {
        if (ai == 0) {
          app.liveWallpaperModeDropdownOpen = false;
          std::string picked{};
          if (::eh::wallpaper::pick_folder(&picked, app.config.folderPickerMode)) {
            app.config.folder = std::move(picked);
            app.liveWallpaperGalleryFolderSynced.clear();
            ensure_live_wallpaper_gallery(app);
            lw_save_settings(app.config);
          }
        } else if (ai == 1) {
          // grid_view — no action defined yet
        } else if (ai == 2) {
          app.config.image.clear();
          app.config.enabled = false;
          live_wallpaper_invalidate_hero(app);
          live_wallpaper_clear_video_wallpaper();
          lw_save_settings(app.config);
          ::eh::wallpaper::apply_saved(app.config.enabled, app.config.image, app.config.mode);
        }
        draw(app);
        return;
      }
    }
  }

  // Hero nav arrows
  {
    const int navY = LW.contentStartY + LW.heroH - LW.heroNavSize - static_cast<int>(16.0 * LW.scale);
    const int navPad = static_cast<int>(8.0 * LW.scale);
    if (point_in_rect(x, y, LW.marginX + navPad, navY, LW.heroNavSize, LW.heroNavSize)) {
      live_wallpaper_cycle_selection(app, -1);
      live_wallpaper_invalidate_hero(app);
      lw_save_settings(app.config);
      draw(app);
      return;
    }
    if (point_in_rect(x, y, LW.marginX + LW.heroW - LW.heroNavSize - navPad, navY, LW.heroNavSize, LW.heroNavSize)) {
      live_wallpaper_cycle_selection(app, 1);
      live_wallpaper_invalidate_hero(app);
      lw_save_settings(app.config);
      draw(app);
      return;
    }
  }

  // Sort pills
  {
    constexpr const char* plab[] = {"Name", "Oldest", "Newest"};
    int px = LW.marginX;
    for (int pi = 0; pi < 3; ++pi) {
      const int textW = lw_text_width(plab[pi], 12.0f, 500);
      const int minPw = textW + 36;
      const int pww = std::max(minPw, static_cast<int>((pi == 0 ? 72.0 : 88.0) * LW.scale));
      if (point_in_rect(x, y, px, LW.sortPillsY, pww, LW.pillH)) {
        if (app.liveWallpaperGallerySortMode != pi) {
          app.liveWallpaperGallerySortMode = pi;
          app.liveWallpaperGalleryPage = 0;
          lw_save_ui_config(app);
          ensure_live_wallpaper_gallery(app);
        }
        draw(app);
        return;
      }
      px += pww + static_cast<int>(8.0 * LW.scale);
    }
  }

  // Gallery thumb selection
  ensure_live_wallpaper_gallery(app);
  if (!app.liveWallpaperGalleryPaths.empty() && app.liveWallpaperUiSubTab == 0) {
    // Hit-test individual thumbs
    int hitIdx = -1;
    std::string hitPath;
    hit_test_gallery_thumb(app, x, y, LW, contentW, hitIdx, hitPath);

    if (!hitPath.empty()) {
      app.config.image = hitPath;
      app.config.enabled = true;
      live_wallpaper_invalidate_hero(app);
      lw_save_settings(app.config);
      if (path_is_video(hitPath)) {
        live_wallpaper_apply_video_with_matugen(app, hitPath);
      } else {
        ::eh::wallpaper::apply_saved(app.config.enabled, app.config.image, app.config.mode);
      }
      draw(app);
      return;
    }

    // Hit-test Prev/Next navigation buttons
    // Recompute layout to get nav geometry
    const double s = std::min(static_cast<double>(app.width) / 1920.0,
                              static_cast<double>(app.height) / 1080.0);
    const double clampedS = std::clamp(s, 0.4, 2.5);
    const int gap = std::max(4, static_cast<int>(kWpThumbGap * clampedS));
    const int availW = contentW - 8 - 2 * kCardPad;
    const double scalePct = static_cast<double>(std::clamp(app.liveWallpaperGalleryScalePct, 50, 200)) / 100.0;
    int targetW = static_cast<int>(420.0 * scalePct);
    targetW = std::clamp(targetW, 60, 800);
    const int cols = std::max(1, (availW + gap) / (targetW + gap));
    int thumbW = (availW - gap * (cols - 1)) / cols;
    thumbW = std::max(40, thumbW);
    const int thumbH = (thumbW * 9 + 8) / 16;
    const int galleryGridY = LW.galleryGridY;
    const int availH = app.height - galleryGridY - static_cast<int>(40.0 * clampedS);
    const int rowStride = thumbH + gap;
    const int rows = std::max(1, availH / rowStride);
    const int perPage = cols * rows;
    const int navW = std::max(48, static_cast<int>(80.0 * clampedS));
    const int navH = std::max(24, static_cast<int>(32.0 * clampedS));
    const int gridBottom = galleryGridY + rows * rowStride;
    const int navY = gridBottom + static_cast<int>(14.0 * clampedS);
    const size_t n = app.liveWallpaperGalleryPaths.size();
    const bool showNav = n > static_cast<size_t>(perPage);
    if (showNav) {
      const int cardInsetXi = LW.marginX;
      const int prevX = cardInsetXi + kCardPad;
      const int nextX = cardInsetXi + contentW - kCardPad - navW;
      if (point_in_rect(x, y, prevX, navY, navW, navH)) {
        int maxPage = std::max(0, static_cast<int>((n + perPage - 1) / perPage) - 1);
        app.liveWallpaperGalleryPage = std::max(0, app.liveWallpaperGalleryPage - 1);
        app.liveWallpaperGalleryPage = std::min(app.liveWallpaperGalleryPage, maxPage);
        draw(app);
        return;
      }
      if (point_in_rect(x, y, nextX, navY, navW, navH)) {
        int maxPage = std::max(0, static_cast<int>((n + perPage - 1) / perPage) - 1);
        app.liveWallpaperGalleryPage = std::min(maxPage, app.liveWallpaperGalleryPage + 1);
        draw(app);
        return;
      }
    }
  }

  // Mode dropdown selection
  if (app.liveWallpaperUiSubTab == 0 && app.liveWallpaperModeDropdownOpen) {
    const int comboX = LW.ctrlX + LW.ctrlW - static_cast<int>(16.0 * LW.scale) - LW.comboW;
    const int comboY = LW.contentStartY + (LW.fillCardH - LW.comboH) / 2;
    const int ddLy = comboY + LW.comboH + 2;
    const int popupH = 5 * kSettingsDdRowH;
    if (x >= comboX && x < comboX + LW.comboW && y >= ddLy && y < ddLy + popupH) {
      const int idx = (y - ddLy) / kSettingsDdRowH;
      if (idx >= 0 && idx < 5) {
        app.config.mode = idx;
        lw_save_settings(app.config);
        ::eh::wallpaper::apply_saved(app.config.enabled, app.config.image, app.config.mode);
      }
    }
    app.liveWallpaperModeDropdownOpen = false;
    app.liveWallpaperModeDropdownHoverRow = -1;
    draw(app);
    return;
  }
}

// Scroll.

void handle_scroll(AppState& app, int x, int y, double dx, double dy) {
  (void)x;
  (void)y;
  (void)dx;
  app.liveWallpaperScrollPx += static_cast<int>(std::lround(dy));
  app.liveWallpaperScrollPx = std::max(0, app.liveWallpaperScrollPx);
  draw(app);
}

} // namespace eh::live_wallpaper
