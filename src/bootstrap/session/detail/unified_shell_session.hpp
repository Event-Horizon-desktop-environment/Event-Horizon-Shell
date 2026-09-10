#pragma once

#include "bootstrap/loop/poll_mux.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/registry/widget_registry.hpp"
#include "desktop_shell/shared/toplevel/toplevel_tracker.hpp"
#include "services/global_keyboard/global_keyboard_handler.hpp"
#include "services/vram_boost/vram_boost_manager.hpp"

#include "services/ipc/client.hpp"
#include "services/ipc/ipc_server.hpp"
#include "wl/color/nightlight.hpp"
#include "wl/session/idle_service.hpp"
#include "desktop_shell/lockscreen/session_lock.hpp"
#include "services/windows/toplevel_bridge.hpp"
#include "wl/core/connection.hpp"
#include "wl/core/seat.hpp"

#if defined(EH_HAVE_POLKIT_AGENT)
#include "services/polkit/dialog/polkit_auth_dialog.hpp"
#endif

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

struct wl_display;

namespace eh::app {

class UnifiedShell;

namespace detail {

struct DragPreviewUser; // forward decl for unique_ptr member

enum class ShellRunMode : std::uint8_t {
  Both,
  DockOnly,
};

[[nodiscard]] ShellRunMode parse_shell_run_mode(int argc, char** argv);

class UnifiedShellSession {
public:
  explicit UnifiedShellSession(UnifiedShell& owner);
  ~UnifiedShellSession();

  int run(ShellRunMode mode);

private:
  void bench_log(const char* tag);
  std::vector<FdHandler> build_handlers();

  bool on_after_wayland_dispatch();
  void on_idle_flush(bool did_display_event);
  void start_notifications_services();
  void stop_notifications_services();
  void sync_widget_registry();

  UnifiedShell& owner_;
  wl_display* display_ = nullptr;
  eh::shell::shared::ToplevelTracker toplevel_tracker_{};
  eh::shell::WidgetRegistry widget_registry_{};
  eh::config::ShellConfig sc0_{};
  std::optional<std::chrono::steady_clock::time_point> bench_t0_{};
  bool force_dock_only_ = false;
  bool running_ = true;
  bool bootstrap_done_ = false;
  bool matugen_reloaded_ = false;
  bool matugen_pending_ = false;
  std::string last_matugen_wallpaper_path_;
  int thumb_poll_fd_ = -1;
  int settings_inotify_fd_ = -1;
  int weather_wake_fd_ = -1;
  int wallpaper_child_pid_ = -1;
  int notifications_child_pid_ = -1;
  int desktop_child_pid_ = -1;
  int dock_child_pid_ = -1;
  int taskbar_child_pid_ = -1;
  int sigchld_fd_ = -1;
  uint64_t desktopLastRestartMs_ = 0;
  uint64_t dockLastRestartMs_ = 0;
  uint64_t taskbarLastRestartMs_ = 0;

  std::unique_ptr<DragPreviewUser> drag_preview_user_;

  // These connections are declared before the services so the services are torn
  // down first; run() also clears them explicitly at exit.
  std::unique_ptr<eh::wayland::WaylandConnection> gamma_conn_;
  std::unique_ptr<eh::wayland::WaylandConnection> idle_conn_;
  std::unique_ptr<eh::wayland::WaylandConnection> lock_conn_;

  std::unique_ptr<eh::wayland::GammaService> gamma_service_;
  int gamma_transition_fd_ = -1;
  std::unique_ptr<eh::wayland::IdleService> idle_service_;
  std::unique_ptr<eh::shell::lockscreen::LockScreen> lock_screen_;
  std::unique_ptr<eh::ipc::IpcService> ipc_service_;
  std::unique_ptr<eh::ipc::IpcClient> ipc_self_client_;
  std::unique_ptr<eh::service::GlobalKeyboardHandler> global_keyboard_;

  std::unique_ptr<eh::windows::ToplevelBridge> toplevel_bridge_;
  std::unique_ptr<eh::service::VramBoostManager> vram_boost_;

#if defined(EH_HAVE_POLKIT_AGENT)
  std::unique_ptr<eh::wayland::WaylandConnection> polkit_conn_;
  eh::wayland::WaylandSeat polkit_seat_;
  std::unique_ptr<eh::polkit::PolkitAuthDialog> polkit_dialog_;
#endif
};

}

}
