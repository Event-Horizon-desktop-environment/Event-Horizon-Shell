#include "desktop_shell/shared/popup/caret/caret.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/shared/popup/surface_fwd.hpp"
#include "desktop_shell/common/time/text_caret.hpp"

#include <wayland-client.h>

namespace {

void dock_popup_surface_frame_done(void* data, wl_callback* cb, uint32_t compositor_time_ms);

}

namespace eh::shell::dock {

void dock_popup_destroy_caret_frame(DockApp& app) {
   
  if (app.popupCaretFrameCb) {
    wl_callback_destroy(app.popupCaretFrameCb);
    app.popupCaretFrameCb = nullptr;
  }
}

void dock_popup_queue_followup_frame(DockApp& app) {
   
  if (!app.popupOpen || !app.popupSurface || app.popupCaretFrameCb) return;
  const bool powerCountdown =
      (app.popupKind == DockApp::PopupKind::PowerConfirm && app.appMenuPowerConfirmOpen);
  const bool caretMenu =
      (dock_popup_kind_uses_app_drawer_ui(app.popupKind) && app.appMenuSearchFocused);
  const bool caretSpot = (app.popupKind == DockApp::PopupKind::Spotlight);
  const bool haveScrollAnim = dock_popup_kind_uses_app_drawer_ui(app.popupKind) &&
      (std::abs(app.appMenuScrollPxCurrent - app.appMenuScrollPx) > 0.5);
  if (!caretMenu && !caretSpot && !haveScrollAnim && !powerCountdown) return;
  static const wl_callback_listener kDockPopupSurfaceListener = {.done = dock_popup_surface_frame_done};
  app.popupCaretFrameCb = wl_surface_frame(app.popupSurface);
  wl_callback_add_listener(app.popupCaretFrameCb, &kDockPopupSurfaceListener, &app);
}

}

namespace {

static int output_refresh_hz(const DockApp& app) {
  for (const auto& slot : app.outputSlots) {
    if (slot && slot->current_mode_refresh_mHz > 0)
      return (slot->current_mode_refresh_mHz + 500) / 1000;
  }
  return 60;
}

void dock_popup_surface_frame_done(void* data, wl_callback* cb, uint32_t  ) {
    
  auto& app = *static_cast<DockApp*>(data);
  wl_callback_destroy(cb);
  app.popupCaretFrameCb = nullptr;
  if (!app.popupOpen || !app.popupSurface) return;

  const bool wantPower =
      (app.popupKind == DockApp::PopupKind::PowerConfirm && app.appMenuPowerConfirmOpen);
  const bool wantCaret =
      (dock_popup_kind_uses_app_drawer_ui(app.popupKind) && app.appMenuSearchFocused) ||
      (app.popupKind == DockApp::PopupKind::Spotlight);
  const bool wantScrollAnim = dock_popup_kind_uses_app_drawer_ui(app.popupKind) &&
      (std::abs(app.appMenuScrollPxCurrent - app.appMenuScrollPx) > 0.5);
  if (!wantCaret && !wantScrollAnim && !wantPower) return;

  const uint64_t ms = eh::shell::monotonic_ms();
  constexpr uint64_t kHalfMs = 530;
  const uint64_t half = ms / kHalfMs;
  if (wantPower) {
    constexpr uint64_t kCountdownIntervalMs = 250;
    const bool motionDirty = app.popupMotionDirty;
    app.popupMotionDirty = false;
    if (motionDirty || ms - app.popupLastDrawMs >= kCountdownIntervalMs) {
      popup_draw_surface(app);
    } else {
      eh::shell::dock::dock_popup_queue_followup_frame(app);
      wl_surface_commit(app.popupSurface);
    }
    return;
  }
  if (wantScrollAnim || half != app.popupCaretBlinkHalf) {
    app.popupCaretBlinkHalf = half;
    const uint64_t minIntervalMs = wantScrollAnim ? (1000u / std::max(1, output_refresh_hz(app))) : 0u;
    if (!wantScrollAnim || (ms - app.popupLastDrawMs) >= minIntervalMs) {
      popup_draw_surface(app);
    } else {
      eh::shell::dock::dock_popup_queue_followup_frame(app);
      wl_surface_commit(app.popupSurface);
    }
  } else {
    eh::shell::dock::dock_popup_queue_followup_frame(app);
  }
}

}
