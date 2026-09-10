#pragma once

#include "services/windows/toplevel_service.hpp"
#include "wl/toplevel/foreign_toplevels.hpp"

// Session-side bridge: feeds the shell's foreign-toplevel
// tracker (main connection) into the IPC toplevel service. It re-derives the
// snapshot on every tracker change and lets ToplevelService diff + broadcast.
//
// Only the wlr list is published: it is the taskbar's source today (state
// flags + titles). The ext-foreign-toplevel list stays in-process for the
// overview host and the dock app menu.

namespace eh::windows {

class ToplevelBridge {
public:
  ToplevelBridge(eh::wayland::ForeignToplevels& source, eh::ipc::IpcService& ipc)
      : source_(source), service_(ipc) {}

  ToplevelBridge(const ToplevelBridge&) = delete;
  ToplevelBridge& operator=(const ToplevelBridge&) = delete;

  void start();
  void stop();

  [[nodiscard]] ToplevelService& service() { return service_; }

private:
  void sync();

  eh::wayland::ForeignToplevels& source_;
  ToplevelService service_;
  uint64_t snapshotToken_ = 0;
};

} // namespace eh::windows
