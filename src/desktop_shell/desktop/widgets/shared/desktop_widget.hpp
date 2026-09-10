#pragma once

#include <cstdint>

#include <cairo/cairo.h>

namespace eh::config {
struct ShellConfig;
}

namespace eh::shell::desktop {

struct DesktopWidget {
  virtual ~DesktopWidget() = default;

  virtual void create() = 0;
  virtual void paint(cairo_t* cr, const eh::config::ShellConfig& sc) = 0;

  // Return true if the click was consumed (prevents drag/resize)
  [[nodiscard]] virtual bool on_click(double x, double y) { (void)x; (void)y; return false; }

  // Called on pointer motion; widgets can track hover state
  virtual void on_motion(double x, double y) { (void)x; (void)y; }

  [[nodiscard]] virtual bool wantsSecondTicks() const { return false; }
  // Animation cadence this widget currently needs, in ms; 0 = none. The host
  // aggregates the minimum so the standalone loop can arm/disarm its animation
  // timer instead of repainting at a fixed 30 Hz forever.
  [[nodiscard]] virtual uint32_t animIntervalMs() const { return 0; }
  [[nodiscard]] virtual int intrinsicWidth() const = 0;
  [[nodiscard]] virtual int intrinsicHeight() const = 0;
};

}
