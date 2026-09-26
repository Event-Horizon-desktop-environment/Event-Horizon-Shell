#include "../../../dock/core/dock_app.h"
#include "desktop_shell/widgets/popup/media_player/media_player_popup.hpp"

#include <algorithm>
#include <cmath>

#include <cairo.h>

namespace eh::shell::dock::popup::media_player {

void dock_media_player_popup_handle_click(DockApp& app, double x, double y, uint32_t) {
  // Geometry comes from the shared paint/hit implementation in the .tpp
  // (single source; see Docs/hit-testing.md).
  if (!app.mpris) return;
  app.mpris->poll_refresh();
  const auto snap = app.mpris->snapshot();
  const bool realActive = snap.active && (!snap.title.empty() || !snap.artist.empty());
  if (!realActive) return;

  const Layout L = compute_layout();
  switch (compute_hover(x, y, L, realActive,
                        snap.can_go_previous, snap.can_go_next,
                        snap.can_play, snap.can_pause, snap.playback_status)) {
    case MediaHover::Play:
      app.mpris->play_pause();
      return;
    case MediaHover::Prev:
      if (snap.can_go_previous) app.mpris->previous();
      return;
    case MediaHover::Next:
      if (snap.can_go_next) app.mpris->next();
      return;
    case MediaHover::Shuffle:
      app.mpris->toggle_shuffle();
      return;
    case MediaHover::Repeat:
      app.mpris->cycle_loop_status();
      return;
    case MediaHover::Seekbar:
      if (snap.duration_us > 0) {
        const double pct = std::clamp((x - kSeekX0) / kSeekW, 0.0, 1.0);
        app.mpris->set_position(static_cast<int64_t>(pct * snap.duration_us));
      }
      return;
    case MediaHover::Close:
    case MediaHover::None:
      return;
  }
}

} // namespace eh::shell::dock::popup::media_player
