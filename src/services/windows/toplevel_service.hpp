#pragma once

#include "services/ipc/ipc_server.hpp"
#include "services/windows/toplevel_types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Server side of the toplevel/window service. The shell owns
// this on its main process: callers push the current window snapshot via
// set_snapshot() (diffed against the previous one), and the service publishes
// toplevel.created / toplevel.updated / toplevel.closed over the IPC bus plus
// answers the "toplevel.list" command with the full snapshot.
//
// Each window keeps a stable `id` (assigned on first appearance, reused while
// the key survives) so clients can address windows across created/updated/
// closed events. The `key` field is the bridge-provided stable identity and is
// never sent over the wire.

namespace eh::windows {

class ToplevelService {
public:
  explicit ToplevelService(eh::ipc::IpcService& ipc) : ipc_(ipc) {}

  ToplevelService(const ToplevelService&) = delete;
  ToplevelService& operator=(const ToplevelService&) = delete;

  // Register the "toplevel.list" handler on the IPC service. Idempotent.
  void register_handlers();

  // Replace the tracked snapshot with `records`, publishing created/updated/
  // closed deltas. Records must have non-empty, stable `key` fields.
  void set_snapshot(std::vector<ToplevelRecord> records);

  void clear();

  [[nodiscard]] const std::vector<ToplevelRecord>& snapshot() const { return snapshot_; }

private:
  eh::ipc::IpcService& ipc_;
  std::vector<ToplevelRecord> snapshot_;
  std::unordered_map<std::string, std::uint64_t> keyToId_;
  std::uint64_t nextId_ = 1;
};

} // namespace eh::windows
