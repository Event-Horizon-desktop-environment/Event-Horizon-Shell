// horizon-polkit-agent — the polkit session agent as its own process.
//
// This used to live inside the supervisor (PolkitAuthService + dialog + seat
// wired into the shell's PollMuxLoop). Splitting it out gives the agent the
// same contract every other session agent has (polkit-gnome / mate-polkit): a
// process that owns its registration lifecycle, its GLib main-context pumping,
// and its own Wayland connection for the dialog — so a protocol error, a
// wedged cancel, or a polkitd restart takes down the agent instead of the
// shell. The supervisor only spawns/respawns/kills this child.

#define _GNU_SOURCE 1
#include "services/polkit/standalone/polkit_agent_standalone.hpp"

#if defined(EH_HAVE_POLKIT_AGENT)

#include "bootstrap/loop/poll_mux.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "services/ipc/client.hpp"
#include "services/ipc/ipc_server.hpp"
#include "services/polkit/dialog/polkit_auth_dialog.hpp"
#include "services/polkit/polkit_log.hpp"
#include "services/polkit/service/polkit_auth_service.hpp"
#include "services/process/parent_death_guard.hpp"
#include "wl/core/connection.hpp"
#include "wl/core/seat.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <poll.h>
#include <unistd.h>

#include <wayland-client.h>

namespace eh::polkit {

int run_polkit_agent_standalone() {
  eh::proc::install_parent_death_guard();

  eh::config::shell_config_reload_from_disk_now(true);

  auto conn = std::make_unique<eh::wayland::WaylandConnection>();
  if (!conn->connect(false)) {
    // Registering with no way to prompt would leave polkitd waiting on an
    // answer nobody can type: fail fast instead and let the supervisor's
    // respawn-with-cooldown retry once the compositor is back.
    std::cerr << "[horizon-polkit-agent] WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }

  // Wire the dialog and its seat input BEFORE registering: registration lands
  // on a thread, and polkitd can ask for a password the moment it completes.
  std::unique_ptr<PolkitAuthDialog> dialog;
  eh::wayland::WaylandSeat seat;
  if (conn->compositor() && conn->shm() && conn->layer_shell()) {
    dialog = std::make_unique<PolkitAuthDialog>(conn->display(), conn->compositor(), conn->shm(),
                                                conn->layer_shell(), conn->viewporter(),
                                                conn->fractional_scale_manager());
    // Keys only arrive while the EXCLUSIVE layer surface holds focus, so input
    // is routed through this connection's seat (the dialog owns no xkb state).
    seat.bind(conn->seat());
    seat.set_pointer_motion_cb([&dialog](wl_surface* surface, double x, double y) {
      if (dialog && surface == dialog->surface()) dialog->pointer_motion(x, y);
    });
    seat.set_pointer_button_cb([&dialog, &seat](uint32_t button, uint32_t state) {
      if (dialog && seat.pointer_focus_surface() == dialog->surface())
        dialog->pointer_button(button, state);
    });
    seat.set_keyboard_key_cb([&dialog](const eh::wayland::WaylandSeat::KeyboardEvent& ev) {
      if (dialog) dialog->consume_keyboard_key(ev.state, ev.sym, ev.utf8.data(), ev.utf8_len);
    });
  } else {
    std::cerr << "[polkit] layer-shell unavailable — auth dialog disabled (agent still registers)\n";
  }

  auto& polkit = PolkitAuthService::instance();
  polkit.set_change_callback([&dialog, &conn]() {
    const Snapshot snap = PolkitAuthService::instance().snapshot();
    if (snap.has_request && snap.state != AuthState::Idle) {
      if (!dialog) return;
      wl_output* out = nullptr;
      if (!conn->outputs().empty()) out = conn->outputs().front();
      dialog->show(out);
    } else if (dialog) {
      dialog->hide();
    }
  });
  polkit.start();

  // Registration runs on its own thread (with internal retries). Don't enter
  // the main loop as a zombie: a live-but-unregistered agent answers nothing
  // and the supervisor can't tell it apart from a healthy one. Fail fast so
  // the respawn-with-cooldown path retries us.
  {
    constexpr int kWaitMs = 15000;
    int waited = 0;
    while (!polkit.registration_done() && waited < kWaitMs) {
      ::usleep(50 * 1000);
      waited += 50;
    }
    if (!polkit.registration_ok()) {
      std::cerr << "[horizon-polkit-agent] registration failed; exiting for supervisor respawn\n";
      polkit_log("agent standalone exiting: registration failed");
      polkit.stop();
      seat.unbind();
      dialog.reset();
      conn.reset();
      return 1;
    }
  }

  // IPC: config.applied → full config reload (including the matugen /
  // horizon-colors wallpaper palette) + repaint. The dialog reads
  // derived_chrome_colors() fresh every paint, so the wallpaper decides the
  // colors live, same contract as the dock/stage children.
  eh::ipc::IpcClient ipc;
  bool ipc_ok = false;
  {
    const int cfd = ipc.connect(eh::ipc::default_socket_path(), 3);
    if (cfd >= 0) {
      ipc.set_event_handler([&dialog](std::string topic, std::string /*payload*/,
                                      std::vector<int> /*fds*/) {
        if (topic == "config.applied") {
          eh::config::shell_config_reload_from_disk_now();
          polkit_log("agent config.applied: reloaded, repainting");
          if (dialog) dialog->repaint();
        }
      });
      ipc_ok = ipc.subscribe("config.applied");
    }
  }

  std::cout << "[horizon-polkit-agent] running pid=" << static_cast<int>(::getpid())
            << " dialog=" << (dialog ? 1 : 0) << " ipc=" << (ipc_ok ? 1 : 0) << std::endl;
  polkit_log("agent standalone up pid=%d dialog=%d", static_cast<int>(::getpid()), dialog ? 1 : 0);

  bool running = true;
  eh::app::PollMuxLoop mux{};
  mux.wayland_display = conn->display();
  mux.display_fd = wl_display_get_fd(conn->display());
  mux.on_display = []() { return true; };
  mux.on_gather_fds = [](std::vector<pollfd>& fds) {
    PolkitAuthService::instance().gather_fds(fds);
  };
  mux.on_poll_timeout = []() { return PolkitAuthService::instance().poll_timeout(); };
  mux.on_post_poll = [](const pollfd* fds, int nfds) {
    PolkitAuthService::instance().dispatch_glib(fds, nfds);
  };
  mux.get_handlers = [&ipc, ipc_ok]() {
    std::vector<eh::app::FdHandler> handlers;
    if (ipc_ok) {
      // Re-resolve every pass: a dropped connection closes the fd inside
      // IpcClient, and polling a stale number spins on POLLNVAL.
      int cfd = ipc.fd();
      if (cfd < 0) {
        static unsigned wait = 0;
        if (++wait >= 2048) {
          wait = 0;
          cfd = ipc.connect(eh::ipc::default_socket_path(), 1);
        }
      }
      if (cfd >= 0) handlers.emplace_back(cfd, POLLIN, [&ipc, cfd](short) { ipc.on_fd_ready(cfd); });
    }
    return handlers;
  };
  (void)mux.run(&running);

  // Tear the agent down first (unregister + join the registration thread),
  // then drop input routing and the connection it was serving.
  polkit.stop();
  polkit.set_change_callback(nullptr);
  seat.unbind();
  dialog.reset();
  conn.reset();
  std::cout << "[horizon-polkit-agent] exit" << std::endl;
  return 0;
}

}  // namespace eh::polkit

#endif
