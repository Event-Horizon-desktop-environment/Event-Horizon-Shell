#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "m3/core/primitives/box.hpp"
#include "configuration/shell_config.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/settings_tab_dock/settings_tab_dock.hpp"
#include "ux/settings/settings_tab_taskbar/settings_tab_taskbar.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/settings_serialize.hpp"

// Display name.
std::string widget_display_title(const std::string& id) {
   
  if (id == "pinned_apps") return "Pinned Apps";
  if (id == "running_apps") return "Running Apps";
  if (eh::config::widget_token_is_system_tray(id)) return "System Tray";
  if (id == "settings_button") return "Settings";
  if (id == "distro_spotlight") return "Spotlight";
  if (id == "app_menu") return "App menu";
  if (id == "app_drawer") return "App drawer";
  if (eh::config::widget_implementation_type(id) == "smenu") return "Start Menu";
  if (id == "launchpad") return "Launchpad";
  if (id == "clock") return "Clock";
  if (id == "weather") return "Weather";
  if (id == "weather_fancy") return "Weather Fancy";
  if (id == "calendar") return "Calendar";
  if (id == "system_monitor") return "System Monitor";
  if (id == "media") return "Media";
  if (id == "media_compact") return "Media Compact";
  if (id == "workspaces") return "Workspaces";
  if (id == "world_clock") return "World Clock";
  if (id == "control_center") return "Control Center";
  if (id == "notifications") return "Notifications";
  if (id == "spacer" || eh::config::widget_implementation_type(id) == "spacer") return "Spacer";
  if (id == "trash" || eh::config::widget_implementation_type(id) == "trash") return "Trash";
  if (id == "volume_mixer" || eh::config::widget_implementation_type(id) == "volume_mixer") return "Volume Mixer";
  if (id == "vpn" || eh::config::widget_implementation_type(id) == "vpn") return "VPN";
  if (id == "battery" || eh::config::widget_implementation_type(id) == "battery") return "Battery";
  if (id == "bluetooth" || eh::config::widget_implementation_type(id) == "bluetooth") return "Bluetooth";
  return id;
}

// Section geometry helpers.
static int widget_section_list_pixel_height(int nwidgets) {
  if (nwidgets <= 0) return 0;
  return nwidgets * kWidgetRowH + (nwidgets - 1) * kWidgetRowGap;
}

static int widget_section_block_height_nominal(int nwidgets) {
  const int listH = widget_section_list_pixel_height(nwidgets);
  return kWidgetSectionPadT + kWidgetSectionTitleBlock + kWidgetListTopGap + listH +
         kWidgetListToAddGap + kWidgetAddButtonH + kWidgetSectionPadB;
}

int widget_section_block_height(int nwidgets) {
  return widget_section_block_height_nominal(nwidgets) + kWidgetSectionBackdropSlackH;
}

static int tab_idx_for_widgets(int activeTab) {
  if (activeTab == 1) return 1;
  if (activeTab == 11) return 11;
  return 0;
}

std::vector<std::string>* widgets_for_section(App& app, int section) {
  const int tabIdx = tab_idx_for_widgets(app.activeTab);
  if (tabIdx == 1) {
    if (section == 0) return &app.settings.panelLeftWidgets;
    if (section == 1) return &app.settings.panelCenterWidgets;
    return &app.settings.panelRightWidgets;
  }
  if (tabIdx == 11) {
    if (section == 0) return &app.settings.taskbarLeftWidgets;
    if (section == 1) return &app.settings.taskbarCenterWidgets;
    return &app.settings.taskbarRightWidgets;
  }
  if (section == 0) return &app.settings.leftWidgets;
  if (section == 1) return &app.settings.centerWidgets;
  return &app.settings.rightWidgets;
}

const std::vector<std::string>* widgets_for_section_for_tab(const App& app, int section,
                                                            int tabIdx) {
  if (tabIdx == 1) {
    if (section == 0) return &app.settings.panelLeftWidgets;
    if (section == 1) return &app.settings.panelCenterWidgets;
    return &app.settings.panelRightWidgets;
  }
  if (tabIdx == 11) {
    if (section == 0) return &app.settings.taskbarLeftWidgets;
    if (section == 1) return &app.settings.taskbarCenterWidgets;
    return &app.settings.taskbarRightWidgets;
  }
  if (section == 0) return &app.settings.leftWidgets;
  if (section == 1) return &app.settings.centerWidgets;
  return &app.settings.rightWidgets;
}

const std::vector<std::string>* widgets_for_section_const(const App& app, int section) {
  const int tabIdx = tab_idx_for_widgets(app.activeTab);
  return widgets_for_section_for_tab(app, section, tabIdx);
}

static int tb_section_stack_top() {
  constexpr int kTbVisToggleRows = 8;
  constexpr int kTbVisCardH = 52 + kDockVisRowPitch * kTbVisToggleRows + 24;
  return kContentTop + kTbVisCardH + kCardGap + kDockCardHeaderReserve;
}

static int widget_section_stack_top_for_tab(int tabIdx) {
  if (tabIdx == 1) {
    constexpr int kPanelModeCardTop = kContentTop + 80 + kCardGap;
    return kPanelModeCardTop + kSpacingL + 54 + 4 * kSliderRowH + kSpacingL + kCardGap +
           kDockCardHeaderReserve;
  }
  if (tabIdx == 11) return tb_section_stack_top();
  constexpr int kVisCardH = 52 + kDockVisRowPitch * kDockVisToggleRows + 24;
  return kContentTop + kVisCardH + kCardGap + kDockCardHeaderReserve;
}

int widget_section_y0_for_tab(const App& app, int sectionIdx, int tabIdx) {
  int y = widget_section_stack_top_for_tab(tabIdx);
  for (int s = 0; s < sectionIdx; ++s) {
    y += widget_section_block_height(
        static_cast<int>(widgets_for_section_for_tab(app, s, tabIdx)->size()));
    y += kWidgetSectionGapBetween;
  }
  return y;
}

int widget_section_y0(const App& app, int sectionIdx) {
  int tabIdx = app.activeTab == 1 ? 1 : (app.activeTab == 11 ? 11 : 0);
  if (app.activeTab == 0 && dock_m3_is_widgets_child_tab()) {
    // Widgets child tab: embedded toggle card + gap + dock card
    const int widgetsTop = kContentTop + kDockChildTabH + 12;
    const int dockCardTop = widgetsTop + kDockWidgetToggleCardH + kCardGap;
    int y = dockCardTop + kDockCardHeaderReserve;
    for (int s = 0; s < sectionIdx; ++s) {
      y += widget_section_block_height(
          static_cast<int>(widgets_for_section_for_tab(app, s, 0)->size()));
      y += kWidgetSectionGapBetween;
    }
    return y;
  }
  return widget_section_y0_for_tab(app, sectionIdx, tabIdx);
}

double settings_dock_widgets_card_fill_height_px(const App& app) {
  double cardTop;
  int stackBottom;
  if (dock_m3_is_widgets_child_tab()) {
    const int widgetsTop = kContentTop + kDockChildTabH + 12;
    cardTop = static_cast<double>(widgetsTop + kDockWidgetToggleCardH + kCardGap);
    const int base = static_cast<int>(cardTop) + kDockCardHeaderReserve;
    stackBottom = base;
    for (int s = 0; s < 3; ++s) {
      stackBottom += widget_section_block_height(
          static_cast<int>(widgets_for_section_for_tab(app, s, 0)->size()));
      stackBottom += kWidgetSectionGapBetween;
    }
  } else {
    constexpr int kVisCardH = 52 + kDockVisRowPitch * kDockVisToggleRows + 24;
    cardTop = static_cast<double>(kContentTop + kVisCardH + kCardGap);
    const auto* rhs = widgets_for_section_for_tab(app, 2, 0);
    stackBottom =
        widget_section_y0_for_tab(app, 2, 0) +
        widget_section_block_height(static_cast<int>(rhs->size()));
  }
  const double fromContent = static_cast<double>(stackBottom + kSpacingXL) - cardTop;
  const double fromViewport =
      static_cast<double>(app.height) - cardTop - static_cast<double>(kSpacingXL);
  constexpr double kMinH = 140.0;
  return std::max(kMinH, std::max(fromContent, fromViewport));
}

void widget_section_row_inner_geometry(const App& app, double* rowX, double* rowW) {
  const int contentX = 16 + 240 + 16;
  const int contentW = app.width - contentX - 16;
  const double cardX = static_cast<double>(contentX + 8);
  const double cardW = static_cast<double>(contentW - 16);
  *rowX = cardX + 24.0;
  *rowW = cardW - 48.0;
}

static int widget_section_list_y0(const App& app, int sectionIdx) {
  return widget_section_y0(app, sectionIdx) + kWidgetSectionPadT + kWidgetSectionTitleBlock +
         kWidgetListTopGap;
}

// Layout and hit-testing.
void layout_section_widget_rows(const App& app, int sectionIdx,
                                const std::vector<std::string>& widgets,
                                std::optional<size_t> skipIndex,
                                std::vector<WidgetRowGeom>& out) {
  out.clear();
  double rowX = 0;
  double rowW = 0;
  widget_section_row_inner_geometry(app, &rowX, &rowW);
  const int listY0 = widget_section_list_y0(app, sectionIdx);
  double yCursor = static_cast<double>(listY0);
  for (size_t i = 0; i < widgets.size(); ++i) {
    if (skipIndex.has_value() && *skipIndex == i) continue;
    WidgetRowGeom g;
    g.index = i;
    g.x = rowX;
    g.y = yCursor;
    g.w = rowW;
    g.h = kWidgetRowH;
    g.toggleW = kWidgetRowToggleW;
    g.toggleH = kWidgetRowToggleH;
    g.removeW = kWidgetRowRemoveHit;
    g.removeH = kWidgetRowRemoveHit;
    g.removeX = rowX + rowW - kWidgetRowRightPad - g.removeW;
    g.removeY = yCursor + static_cast<double>(kWidgetRowH) * 0.5 - g.removeH * 0.5;
    g.toggleX = g.removeX - kWidgetRowToggleRemoveGap - g.toggleW;
    g.toggleY = yCursor + static_cast<double>(kWidgetRowH) * 0.5 - g.toggleH * 0.5;
    out.push_back(g);
    yCursor += static_cast<double>(kWidgetRowH + kWidgetRowGap);
  }
}

static size_t insert_before_row_geoms(double px, double py,
                                      const std::vector<WidgetRowGeom>& geoms,
                                      size_t maxInsert) {
  if (geoms.empty()) return 0;
  for (size_t i = 0; i < geoms.size(); i++) {
    const auto& g = geoms[i];
    if (px >= g.x && px < g.x + g.w && py >= g.y && py < g.y + g.h) {
      const double midY = g.y + g.h * 0.5;
      return (py < midY) ? i : (i + 1);
    }
  }
  size_t best = 0;
  double bestDy = 1e18;
  for (size_t i = 0; i < geoms.size(); i++) {
    const auto& g = geoms[i];
    const double cy = g.y + g.h * 0.5;
    const double dy = std::abs(py - cy);
    if (dy < bestDy) {
      bestDy = dy;
      best = i;
    }
  }
  const auto& g = geoms[best];
  const double midY = g.y + g.h * 0.5;
  size_t ins = (py < midY) ? best : (best + 1);
  if (ins > maxInsert) ins = maxInsert;
  return ins;
}

double settings_logical_content_y_tab01(const App& app) {
  if (app.activeTab == 0 || app.activeTab == 1 || app.activeTab == 11)
    return app.pointerY + settings_scroll_px(app);
  return app.pointerY;
}

static bool widget_pointer_in_section_list_card(const App& app, int sectionIdx, double,
                                                double logicalPy) {
  const int y0 = widget_section_y0(app, sectionIdx);
  const auto* vw = widgets_for_section_const(app, sectionIdx);

  const int minH =
      kWidgetSectionPadT + kWidgetSectionTitleBlock + kWidgetListTopGap + kWidgetRowH;
  const int blkH =
      std::max(minH, widget_section_block_height_nominal(static_cast<int>(vw->size())));
  return logicalPy >= static_cast<double>(y0) && logicalPy < static_cast<double>(y0 + blkH);
}

// Drag state management.
void widget_drag_update_insert(App& app) {
  if (app.activeTab != 0 && app.activeTab != 1 && app.activeTab != 11) return;
  if (app.widgetDragSection < 0 || app.widgetDragFromIndex < 0) return;

  // Dock / taskbar widgets child tab: three-column card layout.
  if ((app.activeTab == 0 && dock_m3_is_widgets_child_tab()) ||
      (app.activeTab == 11 && taskbar_m3_is_widgets_child_tab())) {
    constexpr int contentX = 16 + 240 + 16;
    const int contentW = app.width - contentX - 16;
    const double cX = static_cast<double>(contentX + 8);
    const double cW = static_cast<double>(contentW - 16);
    const double colGap = 8.0;
    const double colW = (cW - 2.0 * colGap) / 3.0;
    const int widgetsTop = kContentTop + kDockChildTabH + 12;
    const double colTop = static_cast<double>(widgetsTop + kDockWidgetToggleCardH + kCardGap + 12);
    const double listY = colTop + 26.0;
    constexpr double kCardH = 54.0;
    constexpr double kCardGapV = 8.0;
    const double px = app.pointerX;
    const double scroll = settings_scroll_px(app);
    const double py = app.pointerY + scroll;

    int targetSec = app.widgetDragTargetSection;
    if (targetSec < 0 || targetSec > 2) targetSec = app.widgetDragSection;

    // Find which column the pointer is in
    bool inAny = false;
    for (int s = 0; s < 3; ++s) {
      const double colX = cX + static_cast<double>(s) * (colW + colGap);
      if (px >= colX && px <= colX + colW + 1.0) {
        targetSec = s;
        inAny = true;
        break;
      }
    }

    if (!inAny) {
      // Find nearest column by horizontal distance to centre
      int nearest = app.widgetDragTargetSection;
      if (nearest < 0 || nearest > 2) nearest = app.widgetDragSection;
      double bestDx = 1e18;
      for (int s = 0; s < 3; ++s) {
        const double colX = cX + static_cast<double>(s) * (colW + colGap);
        const double cx = colX + colW * 0.5;
        const double dx = std::abs(px - cx);
        if (dx < bestDx) {
          bestDx = dx;
          nearest = s;
        }
      }
      targetSec = nearest;
    }
    if (targetSec < 0 || targetSec > 2) targetSec = app.widgetDragSection;
    app.widgetDragTargetSection = targetSec;

    // Find insertion index within the target column
    auto* vTarget = widgets_for_section(app, targetSec);
    const size_t n = vTarget->size();
    const size_t skip = (targetSec == app.widgetDragSection)
                            ? static_cast<size_t>(app.widgetDragFromIndex)
                            : static_cast<size_t>(-1);
    size_t ins = n;
    size_t visIdx = 0;
    for (size_t i = 0; i < n; ++i) {
      if (i == skip) continue;
      const double cy = listY + static_cast<double>(i) * (kCardH + kCardGapV);
      if (py < cy) {
        ins = visIdx;
        break;
      }
      if (py >= cy && py < cy + kCardH) {
        ins = (py < cy + kCardH * 0.5) ? visIdx : (visIdx + 1);
        break;
      }
      visIdx++;
    }
    app.widgetDragInsertBefore = ins;
    return;
  }

  const double px = app.pointerX;
  const double py = settings_logical_content_y_tab01(app);

  int targetSec = app.widgetDragTargetSection;
  if (targetSec < 0 || targetSec > 2) targetSec = app.widgetDragSection;

  bool inAny = false;
  for (int s = 0; s < 3; ++s) {
    if (widget_pointer_in_section_list_card(app, s, px, py)) {
      targetSec = s;
      inAny = true;
      break;
    }
  }
  if (!inAny) {
    int nearest = app.widgetDragTargetSection;
    if (nearest < 0 || nearest > 2) nearest = app.widgetDragSection;
    double bestDy = 1e18;
    for (int s = 0; s < 3; ++s) {
      const int sy0 = widget_section_y0(app, s);
      const auto* sv = widgets_for_section_const(app, s);
      const int minH =
          kWidgetSectionPadT + kWidgetSectionTitleBlock + kWidgetListTopGap + kWidgetRowH;
      const int blkH =
          std::max(minH, widget_section_block_height_nominal(static_cast<int>(sv->size())));
      const double scY = static_cast<double>(sy0) + static_cast<double>(blkH) * 0.5;
      const double dy = std::abs(py - scY);
      if (dy < bestDy) {
        bestDy = dy;
        nearest = s;
      }
    }
    targetSec = nearest;
  }
  if (targetSec < 0 || targetSec > 2) targetSec = app.widgetDragSection;
  app.widgetDragTargetSection = targetSec;

  auto* vTarget = widgets_for_section(app, targetSec);
  const std::optional<size_t> skip =
      (targetSec == app.widgetDragSection)
          ? std::optional<size_t>{static_cast<size_t>(app.widgetDragFromIndex)}
          : std::nullopt;

  std::vector<WidgetRowGeom> geoms;
  layout_section_widget_rows(app, targetSec, *vTarget, skip, geoms);
  app.widgetDragInsertBefore = insert_before_row_geoms(px, py, geoms, geoms.size());
}

void widget_drag_apply(App& app) {
  if (app.widgetDragSection < 0 || app.widgetDragFromIndex < 0) return;
  const int srcSec = app.widgetDragSection;
  int tgtSec = app.widgetDragTargetSection;
  if (tgtSec < 0 || tgtSec > 2) tgtSec = srcSec;

  auto* vSrc = widgets_for_section(app, srcSec);
  const size_t from = static_cast<size_t>(app.widgetDragFromIndex);
  if (from >= vSrc->size()) return;

  const std::string item = (*vSrc)[from];

  if (srcSec == tgtSec) {
    std::vector<std::string> tmp = *vSrc;
    tmp.erase(tmp.begin() + static_cast<long>(from));
    size_t ins = app.widgetDragInsertBefore;
    if (ins > tmp.size()) ins = tmp.size();
    tmp.insert(tmp.begin() + static_cast<long>(ins), item);
    *vSrc = std::move(tmp);
  } else {
    auto* vTgt = widgets_for_section(app, tgtSec);
    std::vector<std::string> src = *vSrc;
    src.erase(src.begin() + static_cast<long>(from));
    *vSrc = std::move(src);

    std::vector<std::string> tgt = *vTgt;
    size_t ins = app.widgetDragInsertBefore;
    if (ins > tgt.size()) ins = tgt.size();
    tgt.insert(tgt.begin() + static_cast<long>(ins), item);
    *vTgt = std::move(tgt);
  }
  save_settings(app.settings);
}

// Hit helpers.
bool hit_widget_row_toggle_hit(const App& app, int section, size_t& outIdx) {
  const auto* v = widgets_for_section_const(app, section);
  std::vector<WidgetRowGeom> geoms;
  layout_section_widget_rows(app, section, *v, std::nullopt, geoms);
  const double py = settings_logical_content_y_tab01(app);
  for (const auto& g : geoms) {
    if (app.pointerX >= g.removeX && app.pointerX < g.removeX + g.removeW && py >= g.removeY &&
        py < g.removeY + g.removeH)
      continue;
    if (app.pointerX >= g.toggleX && app.pointerX < g.toggleX + g.toggleW && py >= g.toggleY &&
        py < g.toggleY + g.toggleH) {
      outIdx = g.index;
      return true;
    }
  }
  return false;
}

bool hit_widget_row_remove_hit(const App& app, int section, size_t& outIdx) {
  const auto* v = widgets_for_section_const(app, section);
  std::vector<WidgetRowGeom> geoms;
  layout_section_widget_rows(app, section, *v, std::nullopt, geoms);
  const double py = settings_logical_content_y_tab01(app);
  for (const auto& g : geoms) {
    if (app.pointerX >= g.removeX && app.pointerX < g.removeX + g.removeW && py >= g.removeY &&
        py < g.removeY + g.removeH) {
      outIdx = g.index;
      return true;
    }
  }
  return false;
}

bool hit_widget_row_drag_hit(const App& app, int section, size_t& outIdx) {
  const auto* v = widgets_for_section_const(app, section);
  std::vector<WidgetRowGeom> geoms;
  layout_section_widget_rows(app, section, *v, std::nullopt, geoms);
  const double py = settings_logical_content_y_tab01(app);
  for (const auto& g : geoms) {
    if (app.pointerX >= g.x && app.pointerX < g.x + kWidgetRowDragHandleW && py >= g.y &&
        py < g.y + g.h) {
      outIdx = g.index;
      return true;
    }
  }
  return false;
}

void widget_section_add_button_rect(const App& app, int sectionIdx, double* bx, double* by,
                                    double* bw, double* bh) {
  const int contentX = 16 + 240 + 16;
  const double cardX = contentX + 8;
  const int y0 = widget_section_y0(app, sectionIdx);
  const auto* vw = widgets_for_section_const(app, sectionIdx);
  const int bhNom = widget_section_block_height_nominal(static_cast<int>(vw->size()));
  const int addBtnW = 168;
  *bx = cardX + 24.0;
  *by = static_cast<double>(y0 + bhNom - kWidgetSectionPadB - kWidgetAddButtonH);
  *bw = static_cast<double>(addBtnW);
  *bh = static_cast<double>(kWidgetAddButtonH);
}

// Drawing helpers.
void settings_draw_drag_handle_row(App& app, cairo_t* cr, double cx, double cyMid,
                                   double alphaMul) {
  if (app.drawChromeMatugen)
    cairo_set_source_rgba(cr, app.drawChrome.outlineR, app.drawChrome.outlineG,
                          app.drawChrome.outlineB, 0.55 * alphaMul);
  else
    cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.38 * alphaMul);
  constexpr double dotR = 2.0;
  const double oy0 = cyMid - 8.0;
  for (int col = 0; col < 2; ++col)
    for (int row = 0; row < 3; ++row) {
      cairo_arc(cr, cx + static_cast<double>(col) * 8.0, oy0 + static_cast<double>(row) * 8.0,
                dotR, 0, 2 * M_PI);
      cairo_fill(cr);
    }
}

void settings_draw_widget_accent_toggle(App& app, cairo_t* cr, double swX, double swY,
                                        bool hovered, double glassAlpha, bool knobRight) {
  (void)app;
  (void)glassAlpha;
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  m3::Toggle toggle;
  toggle.setGeometry(static_cast<float>(swX), static_cast<float>(swY),
                     static_cast<float>(kWidgetRowToggleW),
                     static_cast<float>(kWidgetRowToggleH));
  toggle.setOn(knobRight);
  toggle.setAccentColor(a_r, a_g, a_b);
  toggle.setSurfaceColor(s_r, s_g, s_b);
  toggle.setTextColor(t_r, t_g, t_b);
  toggle.setOutlineColor(o_r, o_g, o_b);
  toggle.setHovered(hovered);
  toggle.paint(cr);
}

void settings_draw_row_remove_button(App& app, cairo_t* cr, double x, double y, double sz,
                                     bool hover, double glassAlpha) {
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
    box.setColor(r, g, b, static_cast<float>(glassAlpha * (hover ? 0.96 : 0.90)));
    box.setRadius(8.0f);
    box.setGeometry(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(sz), static_cast<float>(sz));
    box.setGlassy(true);
    box.paint(cr);
  }
  // Draw the X on top (glass effect now comes from the Box)
  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB,
                        (hover ? 0.9 : 0.52) * glassAlpha);
  cairo_set_line_width(cr, 2.0);
  const double inset = 8.0;
  cairo_move_to(cr, x + inset, y + inset);
  cairo_line_to(cr, x + sz - inset, y + sz - inset);
  cairo_move_to(cr, x + sz - inset, y + inset);
  cairo_line_to(cr, x + inset, y + sz - inset);
  cairo_stroke(cr);
}
