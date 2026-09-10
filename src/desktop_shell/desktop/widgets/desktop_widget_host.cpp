#include "desktop/desktop_widget_host.hpp"
#include "desktop/desktop_widget.hpp"
#include "desktop/desktop_widget_clock.hpp"
#include "desktop/desktop_widget_weather.hpp"
#include "desktop/desktop_widget_system_monitor.hpp"
#include "desktop/desktop_widget_weather_fancy.hpp"
#include "desktop/desktop_widget_calendar.hpp"
#include "desktop/desktop_widget_media_player.hpp"
#include "configuration/shell_config.hpp"
#include "services/mpris/mpris_player.hpp"
#include "ux/settings/data/settings_desktop_widgets_data.hpp"

#include <algorithm>
#include <cmath>

namespace eh::shell::desktop {

struct DesktopWidgetHost::WidgetInstance {
  DesktopWidgetConfig config;
  std::unique_ptr<DesktopWidget> widget;
};

DesktopWidgetHost::DesktopWidgetHost() = default;
DesktopWidgetHost::~DesktopWidgetHost() = default;

static std::unique_ptr<DesktopWidget> make_widget(const DesktopWidgetConfig& cfg, eh::mpris::DockMpris* mpris = nullptr) {
   
  switch (cfg.type) {
    case DesktopWidgetType::Clock:
      return std::make_unique<DesktopClockWidget>(cfg.timeFormat, cfg.showSeconds, cfg.showDate, cfg.fontSize);
    case DesktopWidgetType::Weather:
      return std::make_unique<DesktopWeatherWidget>(cfg.id);
    case DesktopWidgetType::SystemMonitor:
      return std::make_unique<DesktopSystemMonitorWidget>(cfg.id);
    case DesktopWidgetType::WeatherFancy:
      return std::make_unique<DesktopWeatherFancyWidget>(cfg.id);
    case DesktopWidgetType::Calendar:
      return std::make_unique<DesktopCalendarWidget>();
    case DesktopWidgetType::MediaPlayer:
      return std::make_unique<DesktopMediaPlayerWidget>(mpris);
  }
  return nullptr;
}

void DesktopWidgetHost::set_mpris(eh::mpris::DockMpris* mpris) {
   
  m_mpris = mpris;
  // Recreate any existing media player widgets with the new pointer
  for (auto& inst : m_instances) {
    if (inst->widget && inst->config.type == DesktopWidgetType::MediaPlayer) {
      auto newWidget = make_widget(inst->config, m_mpris);
      if (newWidget) {
        newWidget->create();
        inst->widget = std::move(newWidget);
      }
    }
  }
}

void DesktopWidgetHost::set_configs(const std::vector<DesktopWidgetConfig>& configs) {
   
  std::erase_if(m_instances, [&](const auto& inst) {
    auto it = std::find_if(configs.begin(), configs.end(),
                           [&](const auto& c) { return c.id == inst->config.id; });
    return it == configs.end();
  });

  for (const auto& cfg : configs) {
    auto it = std::find_if(m_instances.begin(), m_instances.end(),
                           [&](const auto& inst) { return inst->config.id == cfg.id; });
    if (it == m_instances.end()) {
      auto widget = make_widget(cfg, m_mpris);
      if (!widget) continue;
      widget->create();
      auto inst = std::make_unique<WidgetInstance>();
      inst->config = cfg;
      inst->widget = std::move(widget);
      m_instances.push_back(std::move(inst));
    } else {
      auto& inst = **it;
      auto changed = inst.config.posX != cfg.posX || inst.config.posY != cfg.posY ||
                     inst.config.type != cfg.type || inst.config.timeFormat != cfg.timeFormat ||
                     inst.config.fontSize != cfg.fontSize || inst.config.showSeconds != cfg.showSeconds ||
                     inst.config.showDate != cfg.showDate || inst.config.scale != cfg.scale;
      inst.config = cfg;
      if (changed) {
        auto newWidget = make_widget(cfg, m_mpris);
        if (newWidget) {
          newWidget->create();
          inst.widget = std::move(newWidget);
        }
      }
    }
  }
}

void DesktopWidgetHost::on_second_tick() {
   
  for (auto& inst : m_instances) {
    if (inst->widget && inst->widget->wantsSecondTicks()) {
      inst->widget->create();
    }
  }
}

void DesktopWidgetHost::paint_all(cairo_t* cr, int layerW, int layerH, const std::string& outputName) {
   
  for (auto& inst : m_instances) {
    if (!inst->widget) continue;
    if (!inst->config.outputName.empty() && inst->config.outputName != outputName) continue;

    const int x = inst->config.posX;
    const int y = inst->config.posY;
    const double s = inst->config.scale;
    const int iw = inst->widget->intrinsicWidth();
    const int ih = inst->widget->intrinsicHeight();
    const int sw = static_cast<int>(std::round(static_cast<double>(iw) * s));
    const int sh = static_cast<int>(std::round(static_cast<double>(ih) * s));

    if (x + sw < 0 || x >= layerW || y + sh < 0 || y >= layerH) continue;

    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(x), static_cast<double>(y),
                    static_cast<double>(sw), static_cast<double>(sh));
    cairo_clip(cr);

    cairo_translate(cr, static_cast<double>(x), static_cast<double>(y));
    if (s != 1.0) cairo_scale(cr, s, s);
    inst->widget->paint(cr, eh::config::shell_config_snapshot());
    cairo_restore(cr);
  }
}

void DesktopWidgetHost::paint_resize_indicators(cairo_t* cr, int resizeIdx, const std::string& outputName) const {
   
  if (resizeIdx < 0 || static_cast<size_t>(resizeIdx) >= m_instances.size()) return;
  const auto& inst = *m_instances[static_cast<size_t>(resizeIdx)];
  if (!inst.widget) return;
  if (!inst.config.outputName.empty() && inst.config.outputName != outputName) return;
  const int x = inst.config.posX;
  const int y = inst.config.posY;
  const double s = inst.config.scale;
  const int iw = inst.widget->intrinsicWidth();
  const int ih = inst.widget->intrinsicHeight();
  const double rx = static_cast<double>(x) + static_cast<double>(iw) * s;
  const double ry = static_cast<double>(y) + static_cast<double>(ih) * s;

  cairo_save(cr);
  const double hs = 10.0;
  cairo_move_to(cr, rx, ry);
  cairo_line_to(cr, rx - hs, ry);
  cairo_line_to(cr, rx, ry - hs);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, 0.3, 0.7, 1.0, 0.85);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
  cairo_set_line_width(cr, 1.5);
  cairo_move_to(cr, rx, ry);
  cairo_line_to(cr, rx - hs, ry);
  cairo_move_to(cr, rx, ry);
  cairo_line_to(cr, rx, ry - hs);
  cairo_stroke(cr);
  cairo_restore(cr);
}

bool DesktopWidgetHost::handle_click(double x, double y, const std::string& outputName) {
   
  return hit_test(x, y, outputName) >= 0;
}

void DesktopWidgetHost::widget_motion(int index, double x, double y) {
   
  if (index < 0 || static_cast<size_t>(index) >= m_instances.size()) return;
  auto& inst = *m_instances[static_cast<size_t>(index)];
  if (!inst.widget) return;
  const double s = inst.config.scale;
  const double lx = (x - static_cast<double>(inst.config.posX)) / s;
  const double ly = (y - static_cast<double>(inst.config.posY)) / s;
  inst.widget->on_motion(lx, ly);
}

bool DesktopWidgetHost::widget_click(int index, double x, double y) {
   
  if (index < 0 || static_cast<size_t>(index) >= m_instances.size()) return false;
  auto& inst = *m_instances[static_cast<size_t>(index)];
  if (!inst.widget) return false;
  // Transform layer coordinates to widget-local coordinates (accounting for scale)
  const double s = inst.config.scale;
  const double lx = (x - static_cast<double>(inst.config.posX)) / s;
  const double ly = (y - static_cast<double>(inst.config.posY)) / s;
  return inst.widget->on_click(lx, ly);
}

bool DesktopWidgetHost::handle_motion(double x, double y, const std::string& outputName) {
   
  const int hit = hit_test(x, y, outputName);
  const bool changed = (hit != m_hoveredWidgetIdx);
  if (changed) {
    // Signal leave on previously hovered widget
    if (m_hoveredWidgetIdx >= 0) {
      auto& old = *m_instances[static_cast<size_t>(m_hoveredWidgetIdx)];
      if (old.widget) old.widget->on_motion(-1, -1);
    }
    m_hoveredWidgetIdx = hit;
  }
  if (hit >= 0) widget_motion(hit, x, y);
  return changed;
}

int DesktopWidgetHost::hit_test(double x, double y, const std::string& outputName) const {
   
  for (size_t i = 0; i < m_instances.size(); ++i) {
    const auto& inst = *m_instances[i];
    if (!inst.widget) continue;
    if (!inst.config.outputName.empty() && inst.config.outputName != outputName) continue;
    const int iw = inst.widget->intrinsicWidth();
    const int ih = inst.widget->intrinsicHeight();
    const double s = inst.config.scale;
    const int sw = static_cast<int>(std::round(static_cast<double>(iw) * s));
    const int sh = static_cast<int>(std::round(static_cast<double>(ih) * s));
    if (x >= inst.config.posX && x < inst.config.posX + sw &&
        y >= inst.config.posY && y < inst.config.posY + sh) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void DesktopWidgetHost::set_widget_position(int index, int x, int y) {
   
  if (index < 0 || static_cast<size_t>(index) >= m_instances.size()) return;
  m_instances[static_cast<size_t>(index)]->config.posX = x;
  m_instances[static_cast<size_t>(index)]->config.posY = y;
}

void DesktopWidgetHost::set_widget_scale(int index, double scale) {
   
  if (index < 0 || static_cast<size_t>(index) >= m_instances.size()) return;
  m_instances[static_cast<size_t>(index)]->config.scale = scale;
}

double DesktopWidgetHost::config_scale(int index) const {
   
  if (index < 0 || static_cast<size_t>(index) >= m_instances.size()) return 1.0;
  return m_instances[static_cast<size_t>(index)]->config.scale;
}

const std::vector<DesktopWidgetConfig>& DesktopWidgetHost::configs() const {
   
  m_configCache.clear();
  for (const auto& inst : m_instances)
    m_configCache.push_back(inst->config);
  return m_configCache;
}

}
