#pragma once

// PearCenter compact layout config. Values come from
// [widget.<cc-instance-id>] settings via widget_setting(); absent keys fall
// back to the defaults below so existing installs are unaffected.

#include <string>

#include "desktop_shell/widgets/shared/widget_settings.hpp"

namespace eh::shell::dock::control_center {

struct PearCenterConfig {
  int scalePct = 100; // compact `scale` (100 = 360px wide)
  bool transparency = false;
  int isDarkTheme = -1; // -1 = auto from theme luminance
  std::string mainIconName = "configure";
  int mainIconHeight = 24;
  std::string lightTheme = "Light";
  std::string darkTheme = "Dark";
  bool playVolumeFeedback = true;
  bool showDeviceLink = false; // default false
  std::string deviceLinkCmd;
  std::string deviceLinkTitle = "Device Link";
  std::string deviceLinkIcon = "smartphone";
  bool showNightColor = true;
  bool showColorSwitcher = true;
  bool showDnd = true;
  bool showVolume = true;
  bool showInput = true;
  bool showBrightness = true;
  bool showMediaPlayer = true;
  bool showCamera = true;
  std::string cameraCmd;
  bool showCmd1 = false;
  bool showCmd2 = false;
  bool showPercentage = false;
  std::string cmdRun1;
  std::string cmdTitle1 = "Command Button";
  std::string cmdIcon1 = "system-run-symbolic";
  std::string cmdRun2;
  std::string cmdTitle2 = "Command Button";
  std::string cmdIcon2 = "system-run-symbolic";
};

inline PearCenterConfig pear_center_config(const eh::config::ShellConfig& sc,
                                                        const std::string& widgetId) {
  PearCenterConfig c;
  auto get = [&](const char* key, const std::string& fb) -> std::string {
    const std::string v = eh::widgets::widget_setting(sc, widgetId, key);
    return v.empty() ? fb : v;
  };
  auto getBool = [&](const char* key, bool fb) -> bool {
    const std::string v = eh::widgets::widget_setting(sc, widgetId, key);
    if (v.empty()) return fb;
    return v == "1" || v == "true" || v == "yes" || v == "on";
  };
  auto getInt = [&](const char* key, int fb) -> int {
    const std::string v = eh::widgets::widget_setting(sc, widgetId, key);
    if (v.empty()) return fb;
    try {
      return std::stoi(v);
    } catch (...) {
      return fb;
    }
  };
  c.scalePct = getInt("pear_scale", 100);
  c.transparency = getBool("pear_transparency", false);
  c.isDarkTheme = getInt("pear_is_dark_theme", -1);
  c.mainIconName = get("pear_main_icon", "configure");
  c.mainIconHeight = getInt("pear_main_icon_height", 24);
  c.lightTheme = get("pear_light_theme", "Light");
  c.darkTheme = get("pear_dark_theme", "Dark");
  c.playVolumeFeedback = getBool("pear_play_volume_feedback", true);
  c.showDeviceLink = getBool("pear_show_device_link", false);
  c.deviceLinkCmd = get("pear_device_link_cmd", "");
  c.deviceLinkTitle = get("pear_device_link_title", "Device Link");
  c.deviceLinkIcon = get("pear_device_link_icon", "smartphone");
  c.showNightColor = getBool("pear_show_nightcolor", true);
  c.showColorSwitcher = getBool("pear_show_colorswitcher", true);
  c.showDnd = getBool("pear_show_dnd", true);
  c.showVolume = getBool("pear_show_volume", true);
  c.showInput = getBool("pear_show_input", true);
  c.showBrightness = getBool("pear_show_brightness", true);
  c.showMediaPlayer = getBool("pear_show_mediaplayer", true);
  c.showCamera = getBool("pear_show_camera", true);
  c.cameraCmd = get("pear_camera_cmd", "");
  c.showCmd1 = getBool("pear_show_cmd1", false);
  c.showCmd2 = getBool("pear_show_cmd2", false);
  c.showPercentage = getBool("pear_show_percentage", false);
  c.cmdRun1 = get("pear_cmd1_run", "");
  c.cmdTitle1 = get("pear_cmd1_title", "Command Button");
  c.cmdIcon1 = get("pear_cmd1_icon", "system-run-symbolic");
  c.cmdRun2 = get("pear_cmd2_run", "");
  c.cmdTitle2 = get("pear_cmd2_title", "Command Button");
  c.cmdIcon2 = get("pear_cmd2_icon", "system-run-symbolic");
  if (c.scalePct < 50) c.scalePct = 50;
  if (c.scalePct > 200) c.scalePct = 200;
  return c;
}

inline double pear_center_scale(const PearCenterConfig& c) {
  return static_cast<double>(c.scalePct) * 0.01;
}

// PearCenter compact layout mode. Explicit per-instance opt-in/out via
// [widget.<id>] pear_layout="1"/"0". With no explicit setting, the design
// follows the widget type so legacy Control Center and PearCenter stay
// separate designs: `pear_center` -> compact, `control_center` -> legacy.
inline bool pear_layout_enabled(const eh::config::ShellConfig& sc, const std::string& widgetId) {
  const std::string v = eh::widgets::widget_setting(sc, widgetId, "pear_layout");
  if (!v.empty()) return !(v == "0" || v == "false" || v == "no" || v == "off");
  const auto it = sc.widgets.find(widgetId);
  const std::string raw =
      (it != sc.widgets.end() && !it->second.type.empty()) ? it->second.type : widgetId;
  return raw == "pear_center";
}

} // namespace eh::shell::dock::control_center
