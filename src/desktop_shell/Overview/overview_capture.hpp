#pragma once

#include "desktop_shell/Overview/overview_painter.hpp"
#include "wl/capture/toplevel_stream.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct DockApp;

namespace eh::wayland {
class WaylandConnection;
} // namespace eh::wayland

namespace eh::shell::overview {

class Host;

class Capture {
public:
  explicit Capture(Host& host);
  ~Capture() = default;

  Capture(const Capture&) = delete;
  Capture& operator=(const Capture&) = delete;

  void capture_workspaces(bool preserving = false);
  void sync_live_streams();
  void sync_snapshot_streams();
  void handle_live_frame(eh::wayland::ToplevelStreamFrame&& frame);
  [[nodiscard]] bool live_enabled() const noexcept;

  std::shared_ptr<const WorkspaceCapture> desktop_capture_;
  std::string desktop_wallpaper_path_;
  std::shared_ptr<const WorkspaceCapture> desktop_wallpaper_capture_;

  std::unique_ptr<eh::wayland::ToplevelStream> live_stream_;

  std::string last_data_signature_;
  std::unordered_map<int, std::string> last_ws_signatures_;
  bool recapture_pending = false;
  uint64_t recapture_after_ms = 0;

private:
  Host& host_;
};

} // namespace eh::shell::overview
