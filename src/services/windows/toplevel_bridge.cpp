#include "services/windows/toplevel_bridge.hpp"

#include <cstdint>

namespace eh::windows {

namespace {

std::string key_for_handle(zwlr_foreign_toplevel_handle_v1* handle) {
  return "w:" + std::to_string(reinterpret_cast<std::uintptr_t>(handle));
}

} // namespace

void ToplevelBridge::start() {
  service_.register_handlers();
  snapshotToken_ = source_.add_snapshot_cb([this]() { sync(); });
  sync();
}

void ToplevelBridge::stop() {
  if (snapshotToken_ != 0) {
    source_.remove_snapshot_cb(snapshotToken_);
    snapshotToken_ = 0;
  }
  service_.clear();
}

void ToplevelBridge::sync() {
  std::vector<ToplevelRecord> records;
  records.reserve(source_.size());
  for (const auto& tl : source_.list()) {
    if (tl.closed || !tl.handle) continue;
    ToplevelRecord r;
    r.key = key_for_handle(tl.handle);
    r.appId = tl.appId;
    r.title = tl.title;
    if (tl.activated) r.flags |= kActivated;
    if (tl.minimized) r.flags |= kMinimized;
    if (tl.maximized) r.flags |= kMaximized;
    if (tl.fullscreen) r.flags |= kFullscreen;
    records.push_back(std::move(r));
  }
  service_.set_snapshot(std::move(records));
}

} // namespace eh::windows
