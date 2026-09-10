#include "desktop_shell/launchpad/layer/launchpad_layer.hpp"

#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_modal.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/widgets/app_drawer/overlay/app_drawer_overlay.hpp"

#include <wayland-client.h>
#include <pango/pangocairo.h>

#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "m3/core/primitives/box.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

constexpr double kGridTopMargin = 24.0;
constexpr double kSearchBarH = 44.0;
constexpr double kSearchBarGapBelow = 16.0;
constexpr double kFooterH = 48.0;
constexpr double kBottomMargin = 28.0;

namespace {

void rr(cairo_t* cr, double rx, double ry, double rw, double rh, double rad) {
   
  cairo_new_path(cr);
  const double r = std::min({rad, rw * 0.5, rh * 0.5});
  const double x0 = rx, y0 = ry;
  const double x1 = rx + rw, y1 = ry + rh;
  cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0);
  cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI_2);
  cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

// Treat whitespace, parentheses, equals, and camelCase transitions as word boundaries
// so that "ImageMagick (color depth=q16)" counts as ≈7 words and gets truncated early.
bool is_word_boundary(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0 || c == '(' || c == ')' || c == '=';
}
bool is_camel_boundary(char prev, char curr) {
  return std::islower(static_cast<unsigned char>(prev)) && std::isupper(static_cast<unsigned char>(curr));
}

// Collapse an app name to at most `maxWords` words, appending an ellipsis if anything
// was dropped. Applied before width-fitting so a long multi-word name (e.g. "Microsoft
// Edge (dev)") never grows into a second line and overlaps the row below it.
std::string truncate_label_words(const std::string& name, size_t maxWords = 3) {
  size_t wordCount = 0;
  bool inWord = false;
  size_t cut = std::string::npos;
  for (size_t i = 0; i < name.size(); ++i) {
    const bool sep = is_word_boundary(name[i]);
    // camelCase transition (lower→upper) also starts a new word
    const bool camel = !sep && i > 0 && is_camel_boundary(name[i-1], name[i]);
    if (!sep && !inWord) {
      inWord = true;
      ++wordCount;
      if (wordCount == maxWords + 1) { cut = i; break; }
    } else if (sep || camel) {
      if (inWord && camel) {
        // camelCase boundary: current char starts the next word
        inWord = true;
        ++wordCount;
        if (wordCount == maxWords + 1) { cut = i; break; }
      } else {
        inWord = false;
      }
    }
  }
  if (cut == std::string::npos) return name;
  std::string t = name.substr(0, cut);
  while (!t.empty() && is_word_boundary(t.back())) t.pop_back();
  return t + "\u2026";
}

int count_words(const std::string& s) {
  int n = 0;
  bool inWord = false;
  for (size_t i = 0; i < s.size(); ++i) {
    const bool sep = is_word_boundary(s[i]);
    const bool camel = !sep && i > 0 && is_camel_boundary(s[i-1], s[i]);
    if (!sep && !inWord) {
      inWord = true;
      ++n;
    } else if (sep || camel) {
      inWord = false;
    }
  }
  return n;
}

}


LaunchpadLayout compute_launchpad_layout(const LaunchpadPaintModel& m, double W, double H) {
   
  LaunchpadLayout L;
  L.W = W;
  L.H = H;

  const eh::config::ShellAppearance& ap = eh::config::shell_config_snapshot().appearance;
  // "Columns" controls how many items sit across each row, "Rows" controls how many
  // rows are visible per page — matches the labels shown in Settings > Launcher.
  L.gridCols = std::clamp(ap.launchpadGridColumns, 4, 12);
  L.gridRows = std::clamp(ap.launchpadGridRows, 3, 10);
  const int iconFillPct = std::clamp(ap.launchpadIconFillPct, 30, 95);
  const int layoutScalePct = std::clamp(ap.launchpadLayoutScalePct, 70, 150);
  L.itemsPerPage = L.gridCols * L.gridRows;
  if (L.itemsPerPage < 1) L.itemsPerPage = 1;

  const double baseUs = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double us = baseUs * (ap.launchpadDpiScalePct / 100.0);

  const double layoutScale = static_cast<double>(layoutScalePct) / 100.0;
  const double fill = static_cast<double>(iconFillPct) / 100.0;

  // Icon size driven by DPI (us) + user layout/icon-fill sliders so size goes up with DPI.
  double baseIcon = 64.0 * us;
  L.iconSz = baseIcon * layoutScale * (0.80 + 0.55 * fill);
  L.iconSz = std::clamp(L.iconSz, 26.0 * us, 168.0 * us);
  L.labelFontPx = std::clamp(L.iconSz * 0.215, 8.0 * us, 24.0 * us);

  // Desired gap (spacing between items / tracks) driven by DPI so "when DPI goes up the spacing goes up".
  // The user's "Cell gap" setting is treated as logical and also scaled by the DPI factor for uniformity.
  // Matches the target HTML: generous baseline + explicit gap in a grid-cols-N layout that spreads across the padded area.
  double desiredGap = std::max(12.0 * us, static_cast<double>(std::clamp(ap.launchpadCellGapPx, 0, 24)) * us);

  // Search / organize (scale with DPI)
  L.searchBarH = kSearchBarH * us;
  L.searchBarW = std::clamp(520.0 * us, 240.0 * us, 620.0 * us);
  L.searchBarX = (W - L.searchBarW) * 0.5;
  L.searchBarY = kGridTopMargin * us;
  L.organizeBtnW = L.searchBarH * 0.82;
  L.organizeBtnH = L.organizeBtnW;

  // Horizontal: spread like the HTML grid (px-12 sides + grid-cols-N + gap).
  // Divide the padded area into gridCols tracks (like 1fr columns). Place one icon/folder per track, centered in the track.
  // This spreads the items across the full width (not a tight centered pack).
  // The explicit gutter between tracks is the DPI-scaled desiredGap.
  const double sidePad = 48.0 * us;  // ~ px-12 equivalent, scaled for uniform DPI
  double usableW = std::max(100.0, W - 2.0 * sidePad);
  L.gridLeft = sidePad;
  L.gridW = usableW;

  double gutter = desiredGap;
  // If even with desired sizes the pack wouldn't fit the padded area, uniformly scale icon+gap to fit (uniformity preserved).
  double minNeeded = L.gridCols * L.iconSz + (L.gridCols > 1 ? (L.gridCols-1) * gutter : 0.0);
  if (L.gridCols > 1 && minNeeded > usableW) {
    double fit = usableW / minNeeded;
    L.iconSz *= fit;
    L.labelFontPx = std::clamp(L.iconSz * 0.215, 8.0 * us, 24.0 * us);
    gutter *= fit;
  }

  double contentForTracks = usableW - (L.gridCols > 1 ? (L.gridCols - 1) * gutter : 0.0);
  double trackW = (L.gridCols > 0 ? contentForTracks / L.gridCols : usableW);

  L.cellW = trackW;          // each "column track"
  L.gapPx = static_cast<int>(gutter + 0.5);  // gutter between tracks (the declared gap)

  // Cap icon inside the track so there is some breathing room inside the column (prevents icons touching track edges).
  double maxIconInTrack = trackW - 10.0 * us;
  L.iconSz = std::min(L.iconSz, maxIconInTrack);
  L.iconSz = std::max(L.iconSz, 22.0 * us);
  L.labelFontPx = std::clamp(L.iconSz * 0.215, 8.0 * us, 24.0 * us);

  // Vertical: content-start (top-anchored under search, like the HTML grid).
  // Row "slot" height grows with the final icon + label. Inter-row uses the (DPI-scaled) gap.
  // Items flow downward from under the search with controlled gaps; extra space at bottom if the row count doesn't fill the screen.
  const double cellTopPad = 9.0 * us;
  const double cellBottomPad = 5.0 * us;
  const double labelGap = 8.0 * us;
  const double labelReserve = L.labelFontPx + 5.0 * us;
  L.cellH = cellTopPad + L.iconSz + labelGap + labelReserve + cellBottomPad;
  L.cellH = std::max(L.cellH, L.iconSz + 32.0 * us);

  double vGutter = static_cast<double>(L.gapPx);
  const double naturalRowsH = L.gridRows * L.cellH + (L.gridRows > 1 ? (L.gridRows - 1) * vGutter : 0.0);

  L.gridTop = L.searchBarY + L.searchBarH + kSearchBarGapBelow * us;

  // If the natural rows would be too tall, scale the per-row height (keeping the gap) so it fits.
  const double maxGridH = H - L.gridTop - (kFooterH * us + kBottomMargin * us);
  if (L.gridRows > 1 && naturalRowsH > maxGridH && maxGridH > 10.0) {
    double vFit = maxGridH / naturalRowsH;
    L.cellH *= vFit;
    L.cellH = std::max(L.cellH, L.iconSz + 20.0 * us);
  }

  const double finalRowsH = L.gridRows * L.cellH + (L.gridRows > 1 ? (L.gridRows - 1) * vGutter : 0.0);
  L.gridBottom = L.gridTop + finalRowsH;

  // Organize button at the right edge of the spread grid area (matches the padded full-width tracks).
  L.organizeBtnX = L.gridLeft + L.gridW - L.organizeBtnW - 8.0 * us;
  L.organizeBtnY = L.searchBarY + (L.searchBarH - L.organizeBtnH) * 0.5;

  const int n = m.hits ? static_cast<int>(m.hits->size()) : 0;
  L.pageCount = n <= 0 ? 1 : (n + L.itemsPerPage - 1) / L.itemsPerPage;
  return L;
}

void eh_launchpad_clamp_page(LaunchpadPaintModel* m) {
   
  if (!m) return;
  const double W = static_cast<double>(m->popup_w);
  const double H = static_cast<double>(m->popup_h);
  const LaunchpadLayout lay = compute_launchpad_layout(*m, W, H);
  m->page = std::clamp(m->page, 0, lay.pageCount - 1);
}

void eh_launchpad_ensure_sel_visible(LaunchpadPaintModel* m) {
   
  if (!m) return;
  eh_launchpad_clamp_page(m);
  if (m->sel < 0) return;
  const double W = static_cast<double>(m->popup_w);
  const double H = static_cast<double>(m->popup_h);
  const LaunchpadLayout lay = compute_launchpad_layout(*m, W, H);
  if (lay.itemsPerPage <= 0) return;
  m->page = m->sel / lay.itemsPerPage;
  eh_launchpad_clamp_page(m);
}

AppDrawerHitZone eh_launchpad_hit_zone(const LaunchpadPaintModel& m, wl_surface* body_surface, wl_surface* pointer_surface,
                                        double lx, double ly) {
  
  const double W = static_cast<double>(m.popup_w);
  const double H = static_cast<double>(m.popup_h);
  LaunchpadLayout L;
  if (m.layout) {
    L = *m.layout;
  } else {
    L = compute_launchpad_layout(m, W, H);
  }

  if (pointer_surface != body_surface) return AppDrawerHitZone::None;

  if (lx >= L.searchBarX && lx < L.searchBarX + L.searchBarW && ly >= L.searchBarY &&
      ly < L.searchBarY + L.searchBarH) {
    return AppDrawerHitZone::SearchField;
  }

  if (lx >= L.organizeBtnX && lx < L.organizeBtnX + L.organizeBtnW &&
      ly >= L.organizeBtnY && ly < L.organizeBtnY + L.organizeBtnH) {
    return AppDrawerHitZone::OrganizeButton;
  }

  if (lx >= L.gridLeft && lx < L.gridLeft + L.gridW && ly >= L.gridTop && ly < L.gridBottom) {
    return AppDrawerHitZone::AppListRow;
  }
  return AppDrawerHitZone::None;
}

int eh_launchpad_pick_catalog_index(const LaunchpadPaintModel& m, wl_surface* body_surface, wl_surface* pointer_surface,
                                    double lx, double ly) {
   
  if (pointer_surface != body_surface || !m.hits) return -1;
  if (m.page_slide_t >= 0.f) return -1;
  const double W = static_cast<double>(m.popup_w);
  const double H = static_cast<double>(m.popup_h);

  LaunchpadLayout L;
  if (m.layout) {
    L = *m.layout;
  } else {
    L = compute_launchpad_layout(m, W, H);
  }
  if (lx >= L.searchBarX && lx < L.searchBarX + L.searchBarW && ly >= L.searchBarY && ly < L.searchBarY + L.searchBarH)
    return -1;
  if (lx < L.gridLeft || lx >= L.gridLeft + L.gridW || ly < L.gridTop || ly >= L.gridBottom) return -1;
  const double g = static_cast<double>(L.gapPx);
  const double pitchX = L.cellW + g;
  const double pitchY = L.cellH + g;
  const double relx = lx - L.gridLeft;
  const double rely = ly - L.gridTop;
  const int c = static_cast<int>(std::floor(relx / pitchX));
  const int r = static_cast<int>(std::floor(rely / pitchY));
  if (c < 0 || c >= L.gridCols || r < 0 || r >= L.gridRows) return -1;
  const double xIn = relx - static_cast<double>(c) * pitchX;
  const double yIn = rely - static_cast<double>(r) * pitchY;
  const double iconOffX = (L.cellW - L.iconSz) * 0.5;
  const auto& ap = eh::config::shell_config_snapshot().appearance;
  const double baseUs = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double us = baseUs * (ap.launchpadDpiScalePct / 100.0);
  if (xIn < iconOffX || xIn > iconOffX + L.iconSz || yIn < 6.0 * us || yIn > 6.0 * us + L.iconSz) return -1;
  const int local = r * L.gridCols + c;
  const int global = m.page * L.itemsPerPage + local;
  const int n = static_cast<int>(m.hits->size());
  if (global < 0 || global >= n) return -1;
  return global;
}

static void paint_launchpad_grid_page(cairo_t* cr, DockApp& icon_host, const LaunchpadPaintModel& m,
                                       const LaunchpadLayout& L, int page_index, double bs, double primR, double primG,
                                        double primB, [[maybe_unused]] double outR, [[maybe_unused]] double outG,
                                        [[maybe_unused]] double outB, [[maybe_unused]] bool lpM,
                                        [[maybe_unused]] const eh::config::ChromePaintColors& mc) {
   
  const uint64_t t0 = eh_app_drawer_debug_level() >= 3 ? eh::shell::now_mono_ms() : 0;
  const auto& ap = eh::config::shell_config_snapshot().appearance;
  const double baseUs = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double us = baseUs * (ap.launchpadDpiScalePct / 100.0);
  auto lp_label_fg = [&]() {
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
  };

  const int n = m.hits ? static_cast<int>(m.hits->size()) : 0;
  const int start = page_index * L.itemsPerPage;
  const double g = static_cast<double>(L.gapPx);
  const double maxLabelW = L.cellW - 12.0 * us;

  // Shared Pango layout for wrapping labels (≤3‑word names)
  PangoLayout* pl = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_from_string("Inter");
  pango_font_description_set_absolute_size(desc, L.labelFontPx * PANGO_SCALE);
  pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
  pango_layout_set_font_description(pl, desc);
  pango_font_description_free(desc);
  pango_layout_set_wrap(pl, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_width(pl, static_cast<int>(maxLabelW * PANGO_SCALE));
  pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
  pango_layout_set_height(pl, -static_cast<int>(2.5 * L.labelFontPx * PANGO_SCALE));
  pango_layout_set_alignment(pl, PANGO_ALIGN_CENTER);

  for (int r = 0; r < L.gridRows; ++r) {
    for (int c = 0; c < L.gridCols; ++c) {
      const int gi = start + r * L.gridCols + c;
      if (gi >= n || !m.hits) continue;

      const double cellX = L.gridLeft + static_cast<double>(c) * (L.cellW + g);
      const double cellY = L.gridTop + static_cast<double>(r) * (L.cellH + g);
      const LaunchpadHit& hit = (*m.hits)[static_cast<size_t>(gi)];
      const bool selected = (gi == m.sel);
      const bool hovered = (gi == m.hover_idx && !selected);

      const double ix = cellX + (L.cellW - L.iconSz) * 0.5;
      const double iy = cellY + 10.0 * us;  // more top padding for macOS-like icon placement in cell
      const double kLabelGap = 12.0 * us;  // 12px spacing under icon/folder
      double iconBottom = iy + L.iconSz;

      bool isFolderItem = (hit.folder_def_idx >= 0);
      const double folderScale = std::clamp(ap.launchpadFolderSizePct, 50, 200) / 100.0;
      double effIconSz = L.iconSz;
      double effIconY = iy;
      if (isFolderItem) {
        effIconSz = L.iconSz * folderScale;
        effIconY = iy + (L.iconSz - effIconSz) * 0.5;  // keep centered within the normal icon slot
      }

      // Word‑boundary detection treats whitespace, `()`, `=`, and camelCase as separators,
      // so "ImageMagick (color depth=q16)" counts as several words and truncates early.
      // Names with ≤3  word‑tokens that fit the cell → wrap; anything longer → single‑line.
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, L.labelFontPx);
      cairo_text_extents_t te{};
      cairo_text_extents(cr, hit.name.c_str(), &te);
      const bool isLongName = (count_words(hit.name) >= 4 || te.x_advance > maxLabelW);
      std::string labelText;
      cairo_text_extents_t ex{};
      if (isLongName) {
        labelText = truncate_label_words(hit.name);
        cairo_text_extents(cr, labelText.c_str(), &ex);
      }

      if (hovered || selected) {
        const double hlH = isLongName
          ? (effIconSz + kLabelGap + L.labelFontPx + 4.0 * us)
          : (effIconSz + kLabelGap + L.labelFontPx * 2.0 + 6.0 * us);
        {
          m3::Box hl;
          if (hovered) {
            hl.setColor(static_cast<float>(primR), static_cast<float>(primG),
                        static_cast<float>(primB), 0.12f * static_cast<float>(bs));
          } else {
            hl.setColor(static_cast<float>(primR), static_cast<float>(primG),
                        static_cast<float>(primB), 0.16f * static_cast<float>(bs));
          }
          hl.setRadius(static_cast<float>(effIconSz * 0.22));
          hl.setGeometry(static_cast<float>(cellX + 3.0 * us), static_cast<float>(effIconY - 2.0 * us),
                         static_cast<float>(L.cellW - 6.0 * us), static_cast<float>(hlH));
          hl.setGlassy(true);
          hl.paint(cr);
          if (!hovered) {
            rr(cr, cellX + 3.0 * us, effIconY - 2.0 * us, L.cellW - 6.0 * us, hlH, effIconSz * 0.22);
            cairo_set_source_rgba(cr, primR, primG, primB, 0.42 * bs);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
          }
        }
      }

      if (hit.folder_def_idx >= 0) {
        const int fdi = hit.folder_def_idx;
        if (fdi >= 0 && m.launchpad_folders && fdi < static_cast<int>(m.launchpad_folders->size())) {
          const auto& folder = (*m.launchpad_folders)[static_cast<size_t>(fdi)];
          const double folderSz = effIconSz;
          const double fx = cellX + (L.cellW - folderSz) * 0.5;
          const double fy = effIconY;
          {
            m3::Box fcard;
            fcard.setColor(0.12f, 0.13f, 0.16f, static_cast<float>(std::min(1.0, bs * 0.95)));
            fcard.setRadius(static_cast<float>(folderSz * 0.22));
            fcard.setGeometry(static_cast<float>(fx), static_cast<float>(fy),
                              static_cast<float>(folderSz), static_cast<float>(folderSz));
            fcard.setGlassy(true);
            fcard.paint(cr);
          }
          cairo_save(cr);
          cairo_rectangle(cr, fx, fy, folderSz, folderSz);
          cairo_clip(cr);
          const double previewPad = 6.0 * us;
          const double previewGap = 3.0 * us;
          const double previewCell = (folderSz - previewPad * 2.0 - previewGap * 2.0) / 3.0;
          const double previewSz = previewCell * 0.88;
          const double previewOff = (previewCell - previewSz) * 0.5;
          if (previewSz > 3.0 * us) {
            const int nChildren = std::min(static_cast<int>(folder.appIds.size()), 9);
            const auto& catalog = eh::shell::dock::app_drawer::get_cached_entries();
            for (int ci = 0; ci < nChildren; ++ci) {
              const int col = ci % 3;
              const int row = ci / 3;
              const double pX = fx + previewPad + static_cast<double>(col) * (previewCell + previewGap) + previewOff;
              const double pY = fy + previewPad + static_cast<double>(row) * (previewCell + previewGap) + previewOff;
              auto resolved = find_desktop_file_for_appid(folder.appIds[static_cast<size_t>(ci)]);
              if (resolved) {
                bool childDrawn = false;
                for (const auto& entry : catalog) {
                  if (entry.path == *resolved) {
                    std::string iconKey = entry.icon.empty() ? std::string("application-x-executable") : entry.icon;
                    if (const eh::icons::IconEntry* ic2 = eh_app_drawer_resolve_catalog_icon(icon_host, entry.path, iconKey)) {
                      if (ic2->surface) {
                        cairo_save(cr);
                        cairo_translate(cr, pX, pY);
                        const double sc2 = previewSz / std::max(1.0, std::max(static_cast<double>(ic2->width), static_cast<double>(ic2->height)));
                        cairo_scale(cr, sc2, sc2);
                        cairo_set_source_surface(cr, ic2->surface, 0, 0);
                        cairo_paint(cr);
                        cairo_restore(cr);
                        childDrawn = true;
                      }
                    }
                    break;
                  }
                }
                if (!childDrawn) {
                  static std::unordered_set<std::string> logged;
                  if (logged.insert(folder.appIds[static_cast<size_t>(ci)]).second) {
                    debug_log("icons", "launchpad folder-child unresolved id=\"%s\"",
                              folder.appIds[static_cast<size_t>(ci)].c_str());
                  }
                  const double dotR = previewSz * 0.35;
                  cairo_arc(cr, pX + previewSz * 0.5, pY + previewSz * 0.5, dotR, 0, 2 * M_PI);
                  cairo_set_source_rgba(cr, primR, primG, primB, 0.7 * bs);
                  cairo_fill(cr);
                }
              }
            }
          }
          cairo_restore(cr);
        }
      } else {
        bool iconDrawn = false;
      if (const eh::icons::IconEntry* ic = eh_app_drawer_resolve_catalog_icon(icon_host, hit.path, hit.iconKey)) {
        if (ic->surface) {
          cairo_save(cr);
          const double hs = (hovered && m.hover_anim_scale > 1.001f) ? static_cast<double>(m.hover_anim_scale) : 1.0;
          if (hs > 1.001) {
            cairo_translate(cr, ix + L.iconSz * 0.5, iy + L.iconSz * 0.5);
            cairo_scale(cr, hs, hs);
            cairo_translate(cr, -ix - L.iconSz * 0.5, -iy - L.iconSz * 0.5);
          }
          cairo_translate(cr, ix, iy);
          const double iw = static_cast<double>(ic->width);
          const double ih = static_cast<double>(ic->height);
          const double sc = L.iconSz / std::max(1.0, std::max(iw, ih));
          cairo_scale(cr, sc, sc);
          cairo_set_source_surface(cr, ic->surface, 0, 0);
          cairo_paint(cr);
          cairo_restore(cr);
          iconDrawn = true;
        }
      }
      if (!iconDrawn) {
        cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, std::clamp(L.iconSz * 0.42, 12.0 * us, 28.0 * us));
        cairo_set_source_rgba(cr, primR, primG, primB, bs);
        const double hs2 = (hovered && m.hover_anim_scale > 1.001f) ? static_cast<double>(m.hover_anim_scale) : 1.0;
        if (hs2 > 1.001) {
          cairo_save(cr);
          cairo_translate(cr, ix + L.iconSz * 0.5, iy + L.iconSz * 0.5);
          cairo_scale(cr, hs2, hs2);
          cairo_translate(cr, -ix - L.iconSz * 0.5, -iy - L.iconSz * 0.5);
          std::string init = hit.name.empty() ? "?" : hit.name.substr(0, 1);
          cairo_move_to(cr, ix + L.iconSz * 0.35, iy + L.iconSz * 0.62);
          cairo_show_text(cr, init.c_str());
          cairo_restore(cr);
        } else {
          std::string init = hit.name.empty() ? "?" : hit.name.substr(0, 1);
          cairo_move_to(cr, ix + L.iconSz * 0.35, iy + L.iconSz * 0.62);
          cairo_show_text(cr, init.c_str());
        }
      }
    }

      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, L.labelFontPx);
      lp_label_fg();
      if (isLongName) {
        const double lxLabel = cellX + (L.cellW - ex.x_advance) * 0.5;
        const double lyLabel = iconBottom + kLabelGap;
        cairo_move_to(cr, lxLabel, lyLabel);
        cairo_show_text(cr, labelText.c_str());
      } else {
        pango_layout_set_text(pl, hit.name.c_str(), -1);
        const double lyLabel = iconBottom + kLabelGap;
        cairo_move_to(cr, cellX + 6.0 * us, lyLabel);
        pango_cairo_show_layout(cr, pl);
      }
    }
  }
  g_object_unref(pl);
  if (t0) {
    const auto dt = eh::shell::now_mono_ms() - t0;
    if (dt >= 1)
      eh::shell::dock::app_drawer::trace_line(3, "launchpad-bench",
          "grid_page pg=" + std::to_string(page_index) + " " + std::to_string(dt) + "ms");
  }
}

void eh_launchpad_paint_grid_hover_overlays(cairo_t* cr, DockApp& icon_host, const LaunchpadPaintModel& m,
                                             const LaunchpadLayout& L, double bs, double primR, double primG,
                                             double primB, [[maybe_unused]] double outR, [[maybe_unused]] double outG, [[maybe_unused]] double outB) {
  const auto& ap = eh::config::shell_config_snapshot().appearance;
  const double baseUs = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double us = baseUs * (ap.launchpadDpiScalePct / 100.0);
  const int n = m.hits ? static_cast<int>(m.hits->size()) : 0;
  const int start = m.page * L.itemsPerPage;
  const double g = static_cast<double>(L.gapPx);
  const double kLabelGap = 12.0 * us;  // 12px spacing under icon/folder

  for (int r = 0; r < L.gridRows; ++r) {
    for (int c = 0; c < L.gridCols; ++c) {
      const int gi = start + r * L.gridCols + c;
      if (gi >= n || !m.hits) continue;
      if (gi != m.hover_idx && gi != m.sel) continue;

      const double cellX = L.gridLeft + static_cast<double>(c) * (L.cellW + g);
      const double cellY = L.gridTop + static_cast<double>(r) * (L.cellH + g);
      const LaunchpadHit& hit = (*m.hits)[static_cast<size_t>(gi)];
      const bool selected = (gi == m.sel);
      const bool hovered = (gi == m.hover_idx && !selected);

      const double ix = cellX + (L.cellW - L.iconSz) * 0.5;
      const double iy = cellY + 10.0 * us;  // more top padding for macOS-like icon placement in cell

      bool isFolderItem = (hit.folder_def_idx >= 0);
      const double folderScale = std::clamp(ap.launchpadFolderSizePct, 50, 200) / 100.0;
      double effIconSz = L.iconSz;
      double effIconY = iy;
      if (isFolderItem) {
        effIconSz = L.iconSz * folderScale;
        effIconY = iy + (L.iconSz - effIconSz) * 0.5;  // keep centered within the normal icon slot
      }

      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, L.labelFontPx);
      cairo_text_extents_t te{};
      cairo_text_extents(cr, hit.name.c_str(), &te);
      const double maxLabelW = L.cellW - 12.0 * us;
      const bool isLongName = (count_words(hit.name) >= 4 || te.x_advance > maxLabelW);
      const double hlH = isLongName
        ? (effIconSz + kLabelGap + L.labelFontPx + 4.0 * us)
        : (effIconSz + kLabelGap + L.labelFontPx * 2.0 + 6.0 * us);

      {
        m3::Box hl;
        if (hovered) {
          hl.setColor(static_cast<float>(primR), static_cast<float>(primG),
                      static_cast<float>(primB), 0.12f * static_cast<float>(bs));
        } else {
          hl.setColor(static_cast<float>(primR), static_cast<float>(primG),
                      static_cast<float>(primB), 0.16f * static_cast<float>(bs));
        }
        hl.setRadius(static_cast<float>(effIconSz * 0.22));
        hl.setGeometry(static_cast<float>(cellX + 3.0 * us), static_cast<float>(effIconY - 2.0 * us),
                       static_cast<float>(L.cellW - 6.0 * us), static_cast<float>(hlH));
        hl.setGlassy(true);
        hl.paint(cr);
        if (!hovered) {
          rr(cr, cellX + 3.0 * us, effIconY - 2.0 * us, L.cellW - 6.0 * us, hlH, effIconSz * 0.22);
          cairo_set_source_rgba(cr, primR, primG, primB, 0.42 * bs);
          cairo_set_line_width(cr, 1.0);
          cairo_stroke(cr);
        }
      }

      const double hs = (hovered && m.hover_anim_scale > 1.001f) ? static_cast<double>(m.hover_anim_scale) : 1.0;
      if (hs > 1.001f || hovered) {
        if (hit.folder_def_idx >= 0) {
          // Folder card — just re-draw the face; scaling is minimal so skip inner tiles.
          const double folderSz = effIconSz;
          const double fx = cellX + (L.cellW - folderSz) * 0.5;
          const double fy = effIconY;
          {
            m3::Box fcard;
            fcard.setColor(0.12f, 0.13f, 0.16f, static_cast<float>(std::min(1.0, bs * 0.95)));
            fcard.setRadius(static_cast<float>(folderSz * 0.22));
            fcard.setGeometry(static_cast<float>(fx), static_cast<float>(fy),
                              static_cast<float>(folderSz), static_cast<float>(folderSz));
            fcard.setGlassy(true);
            fcard.paint(cr);
          }
        } else {
          bool iconDrawn = false;
          if (const eh::icons::IconEntry* ic = eh_app_drawer_resolve_catalog_icon(icon_host, hit.path, hit.iconKey)) {
            if (ic->surface) {
              cairo_save(cr);
              if (hs > 1.001f) {
                cairo_translate(cr, ix + L.iconSz * 0.5, iy + L.iconSz * 0.5);
                cairo_scale(cr, hs, hs);
                cairo_translate(cr, -ix - L.iconSz * 0.5, -iy - L.iconSz * 0.5);
              }
              cairo_translate(cr, ix, iy);
              const double iw = static_cast<double>(ic->width);
              const double ih = static_cast<double>(ic->height);
              const double sc = L.iconSz / std::max(1.0, std::max(iw, ih));
              cairo_scale(cr, sc, sc);
              cairo_set_source_surface(cr, ic->surface, 0, 0);
              cairo_paint(cr);
              cairo_restore(cr);
              iconDrawn = true;
            }
          }
          if (!iconDrawn) {
            static std::unordered_set<std::string> logged;
            if (logged.insert(hit.path + "|" + hit.iconKey).second) {
              debug_log("icons", "launchpad icon unresolved path=\"%s\" key=\"%s\"",
                        hit.path.c_str(), hit.iconKey.c_str());
            }
            cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, std::clamp(L.iconSz * 0.42, 12.0 * us, 28.0 * us));
            cairo_set_source_rgba(cr, primR, primG, primB, bs);
            std::string init = hit.name.empty() ? "?" : hit.name.substr(0, 1);
            if (hs > 1.001f) {
              cairo_save(cr);
              cairo_translate(cr, ix + L.iconSz * 0.5, iy + L.iconSz * 0.5);
              cairo_scale(cr, hs, hs);
              cairo_translate(cr, -ix - L.iconSz * 0.5, -iy - L.iconSz * 0.5);
              cairo_move_to(cr, ix + L.iconSz * 0.35, iy + L.iconSz * 0.62);
              cairo_show_text(cr, init.c_str());
              cairo_restore(cr);
            } else {
              cairo_move_to(cr, ix + L.iconSz * 0.35, iy + L.iconSz * 0.62);
              cairo_show_text(cr, init.c_str());
            }
          }
        }
      }
    }
  }
}

void eh_launchpad_paint_body(DockApp& icon_host, cairo_t* cr, [[maybe_unused]] bool paintBackdrop, [[maybe_unused]] bool paintContent, float backdrop_alpha_scale,
                             const LaunchpadPaintModel& m) {
   
  const uint64_t t0 = eh_app_drawer_debug_level() >= 3 ? eh::shell::now_mono_ms() : 0;
  if (eh_app_drawer_debug_level() >= 2) {
    const int nh = m.hits ? static_cast<int>(m.hits->size()) : 0;
    eh::shell::dock::app_drawer::trace_line(2, "launchpad",
                               "paint_body W=" + std::to_string(m.popup_w) + " H=" + std::to_string(m.popup_h) +
                                   " page=" + std::to_string(m.page) + " hits=" + std::to_string(nh));
  }
  const eh::config::ShellConfig& scLp = eh::config::shell_config_snapshot();
  const eh::config::ShellAppearance& ap = scLp.appearance;
  const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(ap);
  const bool lpM = ap.anyPaletteActive();
  const double primR = mc.accentR, primG = mc.accentG, primB = mc.accentB;
  const double outR = mc.outlineR, outG = mc.outlineG, outB = mc.outlineB;
  const double W = static_cast<double>(m.popup_w);
  const double H = static_cast<double>(m.popup_h);
  const double bs = std::clamp(static_cast<double>(backdrop_alpha_scale), 0.0, 1.0);

  const double baseUs = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double us = baseUs * (ap.launchpadDpiScalePct / 100.0);

  LaunchpadLayout L;
  if (m.layout) {
    L = *m.layout;
  } else {
    L = compute_launchpad_layout(m, W, H);
  }

  cairo_save(cr);
  cairo_rectangle(cr, L.gridLeft, L.gridTop, L.gridW, std::max(0.0, L.gridBottom - L.gridTop));
  cairo_clip(cr);

  const int n = m.hits ? static_cast<int>(m.hits->size()) : 0;
  if (m.page_slide_t >= 0.f && m.page_slide_from != m.page_slide_to) {
    const double u = std::clamp(static_cast<double>(m.page_slide_t), 0.0, 1.0);
    const double slide = u * L.gridW;
    const bool forward = m.page_slide_to > m.page_slide_from;
    cairo_save(cr);
    if (forward) {
      cairo_translate(cr, -slide, 0);
      paint_launchpad_grid_page(cr, icon_host, m, L, m.page_slide_from, bs, primR, primG, primB, outR, outG, outB, lpM,
                                 mc);
      cairo_translate(cr, L.gridW, 0);
      paint_launchpad_grid_page(cr, icon_host, m, L, m.page_slide_to, bs, primR, primG, primB, outR, outG, outB, lpM,
                                mc);
    } else {
      cairo_translate(cr, slide, 0);
      paint_launchpad_grid_page(cr, icon_host, m, L, m.page_slide_from, bs, primR, primG, primB, outR, outG, outB, lpM,
                                 mc);
      cairo_translate(cr, -L.gridW, 0);
      paint_launchpad_grid_page(cr, icon_host, m, L, m.page_slide_to, bs, primR, primG, primB, outR, outG, outB, lpM,
                                mc);
    }
    cairo_restore(cr);
  } else {
    paint_launchpad_grid_page(cr, icon_host, m, L, m.page, bs, primR, primG, primB, outR, outG, outB, lpM, mc);
  }

  if (n == 0) {
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14.0 * us);
    cairo_set_source_rgba(cr, outR, outG, outB, bs);
    cairo_move_to(cr, L.gridLeft + L.gridW * 0.5 - 120.0 * us, L.gridTop + L.cellH * 2.0);
    cairo_show_text(cr, "No applications in catalog");
  }

  cairo_restore(cr);

  if (L.pageCount > 1) {
    const double spacing = 22.0 * us;
    const double activeW = 28.0 * us;
    const double activeH = 6.0 * us;
    const double inactiveR = 3.0 * us;
    const double cy = H - kBottomMargin * us - kFooterH * us * 0.5;
    const double span = static_cast<double>(L.pageCount - 1) * spacing;
    double ox = W * 0.5 - span * 0.5;
    for (int p = 0; p < L.pageCount; ++p) {
      const double cx = ox + static_cast<double>(p) * spacing;
      if (p == m.page) {
        m3::Box dot;
        dot.setColor(static_cast<float>(primR), static_cast<float>(primG),
                     static_cast<float>(primB), static_cast<float>(bs));
        dot.setRadius(static_cast<float>(activeH * 0.5));
        dot.setGeometry(static_cast<float>(cx - activeW * 0.5), static_cast<float>(cy - activeH * 0.5),
                        static_cast<float>(activeW), static_cast<float>(activeH));
        dot.setGlassy(true);
        dot.paint(cr);
      } else {
        cairo_arc(cr, cx, cy, inactiveR, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, outR, outG, outB, 0.45 * bs);
        cairo_fill(cr);
      }
    }
  }

  if (m.power_confirm_open && m.power_confirm_idx >= 1 && m.power_confirm_idx <= 3) {
    eh::appdrawer::paint_power_confirm_modal(cr, W, H, m.power_confirm_idx, m.pointer_x, m.pointer_y, mc);
  }

  if (t0) {
    const auto dt = eh::shell::now_mono_ms() - t0;
    if (dt >= 1)
      eh::shell::dock::app_drawer::trace_line(3, "launchpad-bench",
          "paint_body " + std::to_string(dt) + "ms");
  }
  if (eh_app_drawer_debug_level() >= 2) {
    eh::shell::dock::app_drawer::trace_line(2, "launchpad", "paint_body end");
  }
}

void eh_launchpad_render_page(cairo_t* cr, DockApp& icon_host, const LaunchpadPaintModel& m,
                               const LaunchpadLayout& L, int page_index, float alpha_scale,
                               const eh::config::ChromePaintColors& mc) {
   
  const auto& ap = eh::config::shell_config_snapshot().appearance;
  const bool lpM = ap.anyPaletteActive();
  const double bs = std::clamp(static_cast<double>(alpha_scale), 0.0, 1.0);
  const double primR = mc.accentR, primG = mc.accentG, primB = mc.accentB;
  const double outR = mc.outlineR, outG = mc.outlineG, outB = mc.outlineB;
  LaunchpadPaintModel mNoOv = m;
  mNoOv.hover_idx = -1;
  mNoOv.sel = -1;
  paint_launchpad_grid_page(cr, icon_host, mNoOv, L, page_index, bs, primR, primG, primB, outR, outG, outB, lpM, mc);
}

void eh_launchpad_set_content_input_region(wl_compositor* compositor, wl_surface* content_surface,
                                           const LaunchpadPaintModel& m) {
   
  if (!compositor || !content_surface || m.popup_w <= 0 || m.popup_h <= 0) return;
  LaunchpadLayout L;
  if (m.layout) {
    L = *m.layout;
  } else {
    const double W = static_cast<double>(m.popup_w);
    const double H = static_cast<double>(m.popup_h);
    L = compute_launchpad_layout(m, W, H);
  }

  wl_region* r = wl_compositor_create_region(compositor);
  if (!r) return;

  const int sbx = static_cast<int>(std::floor(L.searchBarX));
  const int sby = static_cast<int>(std::floor(L.searchBarY));
  const int sbw = static_cast<int>(std::ceil(L.searchBarW));
  const int sbh = static_cast<int>(std::ceil(L.searchBarH));
  wl_region_add(r, sbx, sby, std::max(1, sbw), std::max(1, sbh));

  const int gx = static_cast<int>(std::floor(L.gridLeft));
  const int gy = static_cast<int>(std::floor(L.gridTop));
  const int gw = static_cast<int>(std::ceil(L.gridW));
  const int gh = static_cast<int>(std::ceil(std::max(0.0, L.gridBottom - L.gridTop)));
  wl_region_add(r, gx, gy, std::max(1, gw), std::max(1, gh));

  if (L.pageCount > 1) {
    const int y0 = static_cast<int>(std::floor(L.gridBottom));
    if (y0 < m.popup_h) {
      wl_region_add(r, 0, y0, m.popup_w, std::max(1, m.popup_h - y0));
    }
  }

  if (m.power_confirm_open && m.power_confirm_idx >= 1 && m.power_confirm_idx <= 3) {
    wl_region_add(r, 0, 0, m.popup_w, m.popup_h);
  }

  wl_surface_set_input_region(content_surface, r);
  wl_region_destroy(r);
}

// Folder card 3x3 square layout helper.
struct FolderCardLayout {
  double cardX, cardY, cardSide;
  double pad;
  double titleAreaY, titleAreaH;
  double gridX, gridY, gridCellSz;
  double iconSz, gap;
  int pageCount;
  int itemsPerPage = 9;
  int innerCols = 3;
  int innerRows = 3;
};

static FolderCardLayout compute_folder_card_layout(const LaunchpadPaintModel& m, double us) {
    
  FolderCardLayout l;
  const double W = static_cast<double>(m.popup_w);
  const double H = static_cast<double>(m.popup_h);
  l.pad = 20.0 * us;
  l.titleAreaH = 52.0 * us;

  const eh::config::ShellAppearance& ap = eh::config::shell_config_snapshot().appearance;
  // Folders show icons within in 3 rows x 3 (fixed).
  // Size the overlay card to fit the 3x3 so content is not stretched (matches the main grid folder/icon size for proper scaling).
  l.innerCols = 3;
  l.innerRows = 3;
  l.itemsPerPage = 9;

  const double folderGapPx = std::clamp(ap.launchpadFolderGapPx, 4, 48);
  l.gap = folderGapPx * us;

  const double maxSide = std::min(H - 40.0 * us, W - 40.0 * us);

  // Use the main grid's icon size as base for the folder overlay content (3x3 of child icons).
  // This makes the large view scale with the launchpad UI settings and avoids stretching.
  LaunchpadLayout mainL = compute_launchpad_layout(m, W, H);
  l.iconSz = mainL.iconSz;
  const double cellPad = 6.0 * us;
  // Estimate cell size to fit icon + label/padding (labels sized from cell in paint).
  // Use a generous estimate so ≤3‑word names that wrap to 2 lines still fit without overflow.
  double labelEst = 44.0 * us;
  l.gridCellSz = l.iconSz + cellPad * 2.0 + labelEst + 4.0 * us;

  // Compute cardSide to exactly fit the 3x3 + title + pads (square-ish), clamped.
  const double gridW = 3 * l.gridCellSz + 2 * l.gap;
  const double gridH = 3 * l.gridCellSz + 2 * l.gap;
  l.cardSide = 2.0 * l.pad + l.titleAreaH + l.gap + gridH + l.pad;
  l.cardSide = std::max(l.cardSide, 2.0 * l.pad + gridW + 2.0 * l.pad);
  l.cardSide = std::min(l.cardSide, maxSide);

  l.cardX = (W - l.cardSide) * 0.5;
  l.cardY = (H - l.cardSide) * 0.5;
  l.titleAreaY = l.cardY + l.pad;
  const double totalGapW = (l.innerCols > 1 ? (l.innerCols - 1) * l.gap : 0.0);
  const double gridTotalW = l.innerCols * l.gridCellSz + totalGapW;
  const double availW = l.cardSide - 2.0 * l.pad;
  l.gridX = l.cardX + l.pad + (availW - gridTotalW) * 0.5;
  l.gridY = l.titleAreaY + l.titleAreaH + l.gap;
  const int n = m.folder_children ? static_cast<int>(m.folder_children->size()) : 0;
  l.pageCount = std::max(1, (n + l.itemsPerPage - 1) / l.itemsPerPage);
  return l;
}

int eh_launchpad_pick_folder_child(const LaunchpadPaintModel& m, double local_x, double local_y) {
   
  if (!m.folder_open || !m.folder_children || m.folder_children->empty()) return -1;
  const double W = static_cast<double>(m.popup_w);
  const double H = static_cast<double>(m.popup_h);
  if (W <= 0 || H <= 0) return -1;
  const auto& ap = eh::config::shell_config_snapshot().appearance;
  const double baseUs = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double us = baseUs * (ap.launchpadDpiScalePct / 100.0);
  const FolderCardLayout lay = compute_folder_card_layout(m, us);
  const int n = static_cast<int>(m.folder_children->size());
  const int start = m.folder_page * lay.itemsPerPage;
  const int end = std::min(start + lay.itemsPerPage, n);
  for (int gi = start; gi < end; ++gi) {
    const int local = gi - start;
    const int col = local % lay.innerCols;
    const int row = local / lay.innerCols;
    const double ix = lay.gridX + static_cast<double>(col) * (lay.gridCellSz + lay.gap);
    const double iy = lay.gridY + static_cast<double>(row) * (lay.gridCellSz + lay.gap);
    if (local_x >= ix && local_x < ix + lay.gridCellSz && local_y >= iy && local_y < iy + lay.gridCellSz)
      return gi;
  }
  return -1;
}

bool eh_launchpad_pick_folder_title(const LaunchpadPaintModel& m, double local_x, double local_y) {
   
  if (!m.folder_open) return false;
  const double W = static_cast<double>(m.popup_w);
  const double H = static_cast<double>(m.popup_h);
  if (W <= 0 || H <= 0) return false;
  const auto& ap = eh::config::shell_config_snapshot().appearance;
  const double baseUs = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double us = baseUs * (ap.launchpadDpiScalePct / 100.0);
  const FolderCardLayout lay = compute_folder_card_layout(m, us);
  return local_x >= lay.cardX + 4.0 * us && local_x <= lay.cardX + lay.cardSide - 4.0 * us &&
         local_y >= lay.titleAreaY && local_y <= lay.titleAreaY + lay.titleAreaH;
}

bool eh_launchpad_pick_folder_card(const LaunchpadPaintModel& m, double local_x, double local_y) {
   
  if (!m.folder_open) return false;
  const double W = static_cast<double>(m.popup_w);
  const double H = static_cast<double>(m.popup_h);
  if (W <= 0 || H <= 0) return false;
  const auto& ap = eh::config::shell_config_snapshot().appearance;
  const double baseUs = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double us = baseUs * (ap.launchpadDpiScalePct / 100.0);
  const FolderCardLayout lay = compute_folder_card_layout(m, us);
  return local_x >= lay.cardX + 4.0 * us && local_x <= lay.cardX + lay.cardSide - 4.0 * us &&
         local_y >= lay.cardY + 4.0 * us && local_y <= lay.cardY + lay.cardSide - 4.0 * us;
}

// Folder overlay paint.

void eh_launchpad_paint_folder_overlay(cairo_t* cr, DockApp& icon_host, const LaunchpadPaintModel& m, double bs,
                                       double primR, double primG, double primB,
                                       double outR, double outG, double outB, double us,
                                       [[maybe_unused]] const eh::config::ChromePaintColors& mc) {
   
  if (!m.folder_open || !m.folder_children) return;
  const double W = static_cast<double>(m.popup_w);
  const double H = static_cast<double>(m.popup_h);
  if (W <= 0 || H <= 0) return;
  const FolderCardLayout lay = compute_folder_card_layout(m, us);
  const double radius = 18.0 * us;

  // Card background - glassy Tahoe style
  {
    m3::Box card;
    card.setColor(0.12f, 0.13f, 0.16f, static_cast<float>(std::min(1.0, bs * 1.1)));
    card.setRadius(static_cast<float>(radius));
    card.setGeometry(static_cast<float>(lay.cardX + 4.0 * us), static_cast<float>(lay.cardY + 4.0 * us),
                     static_cast<float>(lay.cardSide - 8.0 * us), static_cast<float>(lay.cardSide - 8.0 * us));
    card.setGlassy(true);
    card.paint(cr);
  }

  // Card border (subtle)
  rr(cr, lay.cardX + 4.0 * us, lay.cardY + 4.0 * us, lay.cardSide - 8.0 * us, lay.cardSide - 8.0 * us, radius);
  cairo_set_source_rgba(cr, outR, outG, outB, 0.35 * bs);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Title.
  const double titleFontPx = std::clamp(lay.cardSide * 0.028, 14.0 * us, 28.0 * us);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, titleFontPx);

  std::string title_text;
  if (m.folder_editing) {
    title_text = m.folder_edit_buffer;
  } else if (m.folder_open_idx >= 0 && m.hits && m.folder_open_idx < static_cast<int>(m.hits->size())) {
    const int fdi = (*m.hits)[static_cast<size_t>(m.folder_open_idx)].folder_def_idx;
    if (fdi >= 0 && m.launchpad_folders && fdi < static_cast<int>(m.launchpad_folders->size()))
      title_text = (*m.launchpad_folders)[static_cast<size_t>(fdi)].name;
  }

  if (!title_text.empty()) {
    cairo_text_extents_t te;
    cairo_text_extents(cr, title_text.c_str(), &te);
    const double btnSz = 36.0 * us;
    const double btnGap = 8.0 * us;
    const double groupW = te.x_advance + btnGap + btnSz;
    const double titleX = lay.cardX + (lay.cardSide - groupW) * 0.5;
    const double titleY = lay.titleAreaY + lay.titleAreaH * 0.62;

    cairo_set_source_rgba(cr, 0.92, 0.94, 0.96, bs);
    cairo_move_to(cr, titleX, titleY);
    if (m.folder_editing) {
      cairo_show_text(cr, (title_text + "|").c_str());
    } else {
      cairo_show_text(cr, title_text.c_str());
    }

    // Edit button - glassy
    const double btnX = titleX + te.x_advance + btnGap;
    const double btnY = lay.titleAreaY + (lay.titleAreaH - btnSz) * 0.5;
    const bool btnHovered = m.folder_title_hovered;
    {
      m3::Box btn;
      btn.setColor(1.0f, 1.0f, 1.0f, btnHovered ? 0.15f : 0.05f);
      btn.setRadius(static_cast<float>(btnSz * 0.2));
      btn.setGeometry(static_cast<float>(btnX), static_cast<float>(btnY),
                      static_cast<float>(btnSz), static_cast<float>(btnSz));
      btn.setGlassy(true);
      btn.paint(cr);
    }

    const double glyphPx = btnSz * 0.6;
    eh::shell::draw_material_glyph(cr, btnX + btnSz * 0.5, btnY + btnSz * 0.5, glyphPx, "edit",
                                    1.0, 1.0, 1.0, btnHovered ? 0.9 : 0.5);
  }

  // Children grid (obeys launchpad grid rows/cols from settings).
  const int n = static_cast<int>(m.folder_children->size());
  if (n == 0) return;
  const int start = m.folder_page * lay.itemsPerPage;
  const int end = std::min(start + lay.itemsPerPage, n);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  // Clip grid to card bounds so the edge-to-edge grid doesn't overflow
  cairo_save(cr);
  rr(cr, lay.cardX + 4.0 * us, lay.cardY + 4.0 * us, lay.cardSide - 8.0 * us, lay.cardSide - 8.0 * us, radius);
  cairo_clip(cr);

  for (int gi = start; gi < end; ++gi) {
    const int local = gi - start;
    const int col = local % lay.innerCols;
    const int row = local / lay.innerCols;
    const double ix = lay.gridX + static_cast<double>(col) * (lay.gridCellSz + lay.gap);
    const double iy = lay.gridY + static_cast<double>(row) * (lay.gridCellSz + lay.gap);
    const bool hovered = (gi == m.folder_hover_child);

    const auto& child = (*m.folder_children)[static_cast<size_t>(gi)];

    const double labelFontPx = std::clamp(lay.iconSz * 0.215, 8.0 * us, 22.0 * us);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, labelFontPx);
    const double maxLabelW = lay.gridCellSz - 4.0 * us;
    cairo_text_extents_t te{};
    cairo_text_extents(cr, child.name.c_str(), &te);
    const bool isLongName = (count_words(child.name) >= 4 || te.x_advance > maxLabelW);
    std::string labelText;
    cairo_text_extents_t le{};
    if (isLongName) {
      labelText = truncate_label_words(child.name);
      cairo_text_extents(cr, labelText.c_str(), &le);
    }
    const double labelGap = 12.0 * us;  // 12px spacing under icon/folder
    const double lineH = isLongName ? (labelFontPx * 1.2) : (labelFontPx * 2.0 + 6.0 * us);
    // Center icon + label vertically in the cell
    const double contentH = lay.iconSz + labelGap + lineH;
    const double contentY = iy + (lay.gridCellSz - contentH) * 0.5;
    const double iconX = ix + (lay.gridCellSz - lay.iconSz) * 0.5;
    const double iconY = contentY;
    const double lxLabel = isLongName
      ? (ix + (lay.gridCellSz - le.x_advance) * 0.5)
      : (ix + 2.0 * us);
    const double lyLabel = contentY + lay.iconSz + labelGap
      + (isLongName ? (-le.y_bearing) : 0.0);
    // Note: cairo_show_text treats y as baseline, so we add -y_bearing (ascender);
    // pango_cairo_show_layout treats y as the top of the layout, so no offset is needed.

    // Hover highlight - glassy
    if (hovered) {
      {
        m3::Box hl;
        hl.setColor(static_cast<float>(primR), static_cast<float>(primG),
                    static_cast<float>(primB), 0.12f * static_cast<float>(bs));
        hl.setRadius(static_cast<float>(lay.iconSz * 0.2));
        hl.setGeometry(static_cast<float>(ix - 2.0 * us), static_cast<float>(iy - 2.0 * us),
                       static_cast<float>(lay.gridCellSz + 4.0 * us), static_cast<float>(lay.gridCellSz + 4.0 * us));
        hl.setGlassy(true);
        hl.paint(cr);
      }
      rr(cr, ix - 2.0 * us, iy - 2.0 * us, lay.gridCellSz + 4.0 * us, lay.gridCellSz + 4.0 * us, lay.iconSz * 0.2);
      cairo_set_source_rgba(cr, primR, primG, primB, 0.25 * bs);
      cairo_set_line_width(cr, 1.5 * us);
      cairo_stroke(cr);
    }

    // Icon
    bool iconDrawn = false;
    if (const eh::icons::IconEntry* ic = eh_app_drawer_resolve_catalog_icon(icon_host, child.path, child.iconKey)) {
      if (ic->surface) {
        cairo_save(cr);
        const double hs = (hovered && m.hover_anim_scale > 1.001f) ? static_cast<double>(m.hover_anim_scale) : 1.0;
        if (hs > 1.001) {
          cairo_translate(cr, iconX + lay.iconSz * 0.5, iconY + lay.iconSz * 0.5);
          cairo_scale(cr, hs, hs);
          cairo_translate(cr, -iconX - lay.iconSz * 0.5, -iconY - lay.iconSz * 0.5);
        }
        cairo_translate(cr, iconX, iconY);
        const double iw = static_cast<double>(ic->width);
        const double ih = static_cast<double>(ic->height);
        const double sc = lay.iconSz / std::max(1.0, std::max(iw, ih));
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, ic->surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        iconDrawn = true;
      }
    }
    if (!iconDrawn) {
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, std::clamp(lay.iconSz * 0.42, 12.0 * us, 28.0 * us));
      cairo_set_source_rgba(cr, primR, primG, primB, bs);
      const double hs2 = (hovered && m.hover_anim_scale > 1.001f) ? static_cast<double>(m.hover_anim_scale) : 1.0;
      if (hs2 > 1.001) {
        cairo_save(cr);
        cairo_translate(cr, iconX + lay.iconSz * 0.5, iconY + lay.iconSz * 0.5);
        cairo_scale(cr, hs2, hs2);
        cairo_translate(cr, -iconX - lay.iconSz * 0.5, -iconY - lay.iconSz * 0.5);
        std::string init = child.name.empty() ? "?" : child.name.substr(0, 1);
        cairo_move_to(cr, iconX + lay.iconSz * 0.35, iconY + lay.iconSz * 0.62);
        cairo_show_text(cr, init.c_str());
        cairo_restore(cr);
      } else {
        std::string init = child.name.empty() ? "?" : child.name.substr(0, 1);
        cairo_move_to(cr, iconX + lay.iconSz * 0.35, iconY + lay.iconSz * 0.62);
        cairo_show_text(cr, init.c_str());
      }
    }

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, labelFontPx);
    cairo_set_source_rgba(cr, 0.88, 0.91, 0.94, bs);
    if (isLongName) {
      cairo_move_to(cr, lxLabel, lyLabel);
      cairo_show_text(cr, labelText.c_str());
    } else {
      PangoLayout* pl = pango_cairo_create_layout(cr);
      PangoFontDescription* desc = pango_font_description_from_string("Inter");
      pango_font_description_set_absolute_size(desc, labelFontPx * PANGO_SCALE);
      pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
      pango_layout_set_font_description(pl, desc);
      pango_font_description_free(desc);
      pango_layout_set_wrap(pl, PANGO_WRAP_WORD_CHAR);
      pango_layout_set_width(pl, static_cast<int>(maxLabelW * PANGO_SCALE));
      pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
      pango_layout_set_height(pl, -static_cast<int>(2.5 * labelFontPx * PANGO_SCALE));
      pango_layout_set_alignment(pl, PANGO_ALIGN_CENTER);
      pango_layout_set_text(pl, child.name.c_str(), -1);
      cairo_move_to(cr, lxLabel, lyLabel);
      pango_cairo_show_layout(cr, pl);
      g_object_unref(pl);
    }
  }
  cairo_restore(cr);

  // Page dots.
  if (lay.pageCount > 1) {
    const double dotSpacing = 16.0 * us;
    const double dotR = 4.0 * us;
    const double dotY = lay.cardY + lay.cardSide - 16.0 * us;
    const double span = static_cast<double>(lay.pageCount - 1) * dotSpacing;
    double ox = lay.cardX + lay.cardSide * 0.5 - span * 0.5;
    for (int p = 0; p < lay.pageCount; ++p) {
      const double cx = ox + static_cast<double>(p) * dotSpacing;
      if (p == m.folder_page) {
        m3::Box dot;
        dot.setColor(static_cast<float>(primR), static_cast<float>(primG),
                     static_cast<float>(primB), static_cast<float>(bs));
        dot.setRadius(3.0f);
        dot.setGeometry(static_cast<float>(cx - 6.0 * us), static_cast<float>(dotY - 3.0 * us),
                        12.0f, 6.0f);
        dot.setGlassy(true);
        dot.paint(cr);
      } else {
        cairo_arc(cr, cx, dotY, dotR, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, outR, outG, outB, 0.45 * bs);
        cairo_fill(cr);
      }
    }
  }
}
