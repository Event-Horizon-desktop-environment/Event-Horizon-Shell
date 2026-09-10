#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <cairo/cairo.h>

struct DesktopWidgetConfig;

namespace eh::mpris { class DockMpris; }

namespace eh::shell::desktop {

struct DesktopWidget;

class DesktopWidgetHost {
public:
  DesktopWidgetHost();
  ~DesktopWidgetHost();

  void set_configs(const std::vector<DesktopWidgetConfig>& configs);
  void set_mpris(eh::mpris::DockMpris* mpris);
  void on_second_tick();

  void paint_all(cairo_t* cr, int layerW, int layerH, const std::string& outputName = {});
  bool handle_click(double x, double y, const std::string& outputName = {});
  // Returns true if hover state changed (caller should repaint)
  bool handle_motion(double x, double y, const std::string& outputName = {});
  bool widget_click(int index, double x, double y);
  void widget_motion(int index, double x, double y);
  [[nodiscard]] int hovered_widget() const { return m_hoveredWidgetIdx; }

  [[nodiscard]] int hit_test(double x, double y, const std::string& outputName) const;
  void set_widget_position(int index, int x, int y);
  void set_widget_scale(int index, double scale);
  [[nodiscard]] double config_scale(int index) const;
  [[nodiscard]] const std::vector<DesktopWidgetConfig>& configs() const;
  void paint_resize_indicators(cairo_t* cr, int resizeIdx, const std::string& outputName) const;

private:
  struct WidgetInstance;
  std::vector<std::unique_ptr<WidgetInstance>> m_instances;
  mutable std::vector<DesktopWidgetConfig> m_configCache;
  eh::mpris::DockMpris* m_mpris = nullptr;
  int m_hoveredWidgetIdx = -1;
};

}
