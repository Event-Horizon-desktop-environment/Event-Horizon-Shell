#pragma once

#include <string>
#include <vector>

enum class DesktopWidgetType : int {
  Clock = 0,
  Weather = 1,
  SystemMonitor = 2,
  WeatherFancy = 3,
  Calendar = 4,
  MediaPlayer = 5,
  MediaCompact = 6,
  WorldClock = 7,
};

inline const char* desktop_widget_type_to_widget_id(DesktopWidgetType t) {
  switch (t) {
    case DesktopWidgetType::Clock: return "clock";
    case DesktopWidgetType::Weather: return "weather";
    case DesktopWidgetType::SystemMonitor: return "system_monitor";
    case DesktopWidgetType::WeatherFancy: return "weather_fancy";
    case DesktopWidgetType::Calendar: return "calendar";
    case DesktopWidgetType::MediaPlayer: return "media";
    case DesktopWidgetType::MediaCompact: return "media_compact";
    case DesktopWidgetType::WorldClock: return "world_clock";
  }
  return "";
}

inline DesktopWidgetType widget_id_to_desktop_widget_type(const std::string& wid) {
  if (wid == "clock") return DesktopWidgetType::Clock;
  if (wid == "weather") return DesktopWidgetType::Weather;
  if (wid == "media") return DesktopWidgetType::MediaPlayer;
  if (wid == "calendar") return DesktopWidgetType::Calendar;
  if (wid == "weather_fancy") return DesktopWidgetType::WeatherFancy;
  if (wid == "media_compact") return DesktopWidgetType::MediaCompact;
  if (wid == "system_monitor") return DesktopWidgetType::SystemMonitor;
  if (wid == "world_clock") return DesktopWidgetType::WorldClock;
  return DesktopWidgetType::Clock;
}

struct DesktopWidgetConfig {
  DesktopWidgetType type = DesktopWidgetType::Clock;
  std::string id;
  std::string widgetId;
  int posX = 100;
  int posY = 100;
  double scale = 1.0;
  std::string timeFormat;
  bool showSeconds = false;
  bool showDate = true;
  int fontSize = 48;
  std::string outputName;
};
