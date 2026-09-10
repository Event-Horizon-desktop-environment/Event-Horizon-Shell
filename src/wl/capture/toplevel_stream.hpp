#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct wl_display;
struct wl_shm;
struct hyprland_toplevel_export_manager_v1;

namespace eh::wayland {

// A completed live frame of one toplevel. `bgra` holds 8-bit premultiplied
// BGRA (little-endian CAIRO_FORMAT_ARGB32 order), top-down.
struct ToplevelStreamFrame {
  uint32_t window_id = 0; // Hyprland window address as reported by j/clients
  int width = 0;
  int height = 0;
  std::vector<uint8_t> bgra;
};

// Continuously captures a set of toplevels via the toplevel export protocol.
// One frame is kept in flight per window; on completion `cb` fires on the main
// wayland dispatch and the next frame for that window is requested, so frames
// arrive at (up to) the compositor refresh rate. Capture targets are
// identified by their window address (the `handle` integer accepted by
// hyprland_toplevel_export_manager_v1.capture_toplevel). Frames are delivered
// downscaled to at most `max_dim` on the longest side.
class ToplevelStream {
public:
  using Callback = std::function<void(ToplevelStreamFrame&&)>;

  // One capture session per toplevel. Implementation detail shared with the
  // helper functions in the .cpp; the struct body is defined there.
  struct Session;

  ToplevelStream();
  ToplevelStream(const ToplevelStream&) = delete;
  ToplevelStream& operator=(const ToplevelStream&) = delete;
  ~ToplevelStream();

  bool start(wl_display* display, hyprland_toplevel_export_manager_v1* mgr, wl_shm* shm,
             Callback cb);

  // (Re)set the set of windows to capture, adding and removing sessions to
  // match `window_ids`. Safe to call from any main-loop callback.
  void sync_windows(const std::vector<uint32_t>& window_ids, int max_dim = 1024);

  // One-shot mode: when enabled (before the next sync_windows), every session
  // captures a single frame and then stops instead of continuously re-requesting
  // frames. Used for static per-window snapshots so a background workspace card
  // gets a real window image without an ongoing stream.
  void set_one_shot(bool one_shot);

  // Releases any capture that was rate-limited (deferred because its window
  // finished its previous copy faster than the shell can paint) and whose
  // deferral interval has now elapsed. Call once per paint frame (the shell
  // already runs a vsync-paced frame loop, so this piggybacks on that
  // instead of needing its own timer) so captures stay paced to what the UI
  // can actually keep up with, rather than free-running and starving the
  // main thread of time to handle input/repaint during interactions like a
  // window drag.
  void tick();

  void stop();

  [[nodiscard]] bool active() const { return display_ != nullptr; }

private:
  std::vector<std::unique_ptr<Session>> sessions_;
  Callback cb_;
  wl_display* display_ = nullptr;
  wl_shm* shm_ = nullptr;
  hyprland_toplevel_export_manager_v1* mgr_ = nullptr;
  bool one_shot_ = false;
};

} // namespace eh::wayland
