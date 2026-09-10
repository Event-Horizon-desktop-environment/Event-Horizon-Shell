#include "desktop_shell/dock/launch/dock_launch_feedback.hpp"

#include "desktop_shell/dock/core/dock_bar.h"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/pinned/dock_pin_identity.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"

#include <cmath>

namespace {

float dock_launch_bounce_sample(float u, float ampPx) {
  constexpr float kPi = 3.14159265f;
  const float damp = std::exp(-2.2f * u);
  constexpr float angleOffset = -1.5707963f;
  return damp * std::sin(u * kPi * 5.25f + angleOffset) * ampPx;
}

}

double dock_launch_bounce_extra_lift_y(DockApp& app, const bool is_app_slot, const std::string& slot_key) {
   
  if (!is_app_slot || app.dockLaunchBounceAnchorNorm.empty()) return 0.0;
  const std::string nk = eh::shell::paths::normalize_desktop_app_id(slot_key);
  if (!dock_pin_identity_norm_matches_anchor(app, app.dockLaunchBounceAnchorNorm, nk)) return 0.0;
  return static_cast<double>(app.dockLaunchBounceLiftPx);
}

void dock_start_launch_bounce(DockApp& app, const std::string& app_key_raw, const bool cold_start) {
   
  if (app_key_raw.empty()) return;
  const std::string anchor = eh::shell::paths::normalize_desktop_app_id(app_key_raw);
  if (anchor.empty() || anchor == "unknown") return;

  if (app.dockLaunchBounceAnimId != 0) app.shellAnim.cancel(app.dockLaunchBounceAnimId);
  app.dockLaunchBounceAnimId = 0;
  app.dockLaunchBounceLiftPx = 0.f;
  app.dockLaunchBounceAnchorNorm = anchor;

  const float dur = cold_start ? 520.f : 300.f;
  const float amp = cold_start ? 12.f : 5.f;

  app.dockLaunchBounceAnimId = app.shellAnim.animate(
      0.f, 1.f, dur, eh::shell::Easing::Linear,
      [&app, amp](const float u) { app.dockLaunchBounceLiftPx = dock_launch_bounce_sample(u, amp); },
      [&app]() {
        app.dockLaunchBounceLiftPx = 0.f;
        app.dockLaunchBounceAnchorNorm.clear();
        app.dockLaunchBounceAnimId = 0;
        dock_schedule_frame(app);
      });
  dock_schedule_frame(app);
}
