#include "desktop_shell/dock/widgets/dock_widget_tokens.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "configuration/shell_config.hpp"

std::string dock_first_control_center_widget_id(const DockSettings& s) {
   
  for (const auto& w : s.leftWidgets) {
    if (eh::config::widget_implementation_type(w) == "control_center") return w;
  }
  for (const auto& w : s.centerWidgets) {
    if (eh::config::widget_implementation_type(w) == "control_center") return w;
  }
  for (const auto& w : s.rightWidgets) {
    if (eh::config::widget_implementation_type(w) == "control_center") return w;
  }
  return {};
}

std::string dock_active_control_center_widget_id(const DockApp& app) {
  if (!app.ccWidgetId.empty()) return app.ccWidgetId;
  return dock_first_control_center_widget_id(app.settings);
}
