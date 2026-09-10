#pragma once

#include <functional>
#include <string>
#include <vector>

#include <cairo/cairo.h>

namespace eh::config {
struct ShellConfig;
}

namespace eh::shell::dock::control_center {

struct ControlCenterWeatherState {
  struct ForecastDay {
    std::string day{};
    int wcode = 0;
    int hi = 0;
    int lo = 0;
    std::string icon{};
  };

  bool available = false;
  std::string location{};
  int temp = 0;
  int hi = 0;
  int lo = 0;
  int feels_like = 0;
  int humidity_pct = 0;
  int wind_kmh = 0;
  int visibility_m = 0;
  std::string condition{};
  std::string icon{};
  bool is_day = true;
  bool fahrenheit = false;
  std::vector<ForecastDay> forecast{};
  std::string status_text{};
};

[[nodiscard]] ControlCenterWeatherState control_center_weather_state(const eh::config::ShellConfig& sc,
                                                                     std::string_view instance_id);

void control_center_weather_async_init();
void control_center_weather_drive_curl_multi();
[[nodiscard]] int control_center_weather_async_wake_fd() noexcept;
void control_center_weather_async_handle_wake();
void control_center_weather_async_register_redraw(std::function<void()> redraw);
void control_center_weather_service_start();
void control_center_weather_service_stop();
void control_center_weather_startup_dock(void* app_ctx, void (*redraw_fn)(void* ctx));

} // namespace eh::shell::dock::control_center
