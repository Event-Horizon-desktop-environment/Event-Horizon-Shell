#include "services/windows/toplevel_service.hpp"

#include <iostream>
#include <utility>

namespace eh::windows {

namespace {

// Equality check for the fields clients can see; `key` and `id` are identity,
// not content.
bool same_fields(const ToplevelRecord& a, const ToplevelRecord& b) {
  return a.appId == b.appId && a.title == b.title && a.flags == b.flags;
}

} // namespace

void ToplevelService::register_handlers() {
  ipc_.register_handler(kToplevelListCmd, [this](const std::vector<std::string>&) -> std::string {
    std::string out;
    toplevel_encode_list(snapshot_, out);
    return out;
  });
}

void ToplevelService::set_snapshot(std::vector<ToplevelRecord> incoming) {
  // Assign stable ids to every incoming key first, so created events carry the
  // id the client will keep referring to.
  for (auto& r : incoming) {
    if (r.key.empty()) {
      std::cerr << "[toplevel] set_snapshot: record with empty key dropped\n";
      continue;
    }
    auto it = keyToId_.find(r.key);
    if (it == keyToId_.end()) {
      r.id = nextId_++;
      keyToId_.emplace(r.key, r.id);
    } else {
      r.id = it->second;
    }
  }

  // Closed: keys present before but missing now.
  for (const auto& cur : snapshot_) {
    bool still = false;
    for (const auto& r : incoming) {
      if (r.key == cur.key) {
        still = true;
        break;
      }
    }
    if (!still) {
      std::string payload = std::to_string(cur.id);
      ipc_.publish(kToplevelClosed, payload);
      keyToId_.erase(cur.key);
    }
  }

  // Created / updated.
  for (const auto& r : incoming) {
    const ToplevelRecord* prev = nullptr;
    for (const auto& cur : snapshot_) {
      if (cur.key == r.key) {
        prev = &cur;
        break;
      }
    }
    if (!prev) {
      std::string payload;
      toplevel_encode_record(r, payload);
      ipc_.publish(kToplevelCreated, payload);
    } else if (!same_fields(*prev, r)) {
      std::string payload;
      toplevel_encode_record(r, payload);
      ipc_.publish(kToplevelUpdated, payload);
    }
  }

  // Drop empty-key records (they were skipped above but keep the snapshot sane).
  incoming.erase(std::remove_if(incoming.begin(), incoming.end(),
                                [](const ToplevelRecord& r) { return r.key.empty(); }),
                 incoming.end());
  snapshot_ = std::move(incoming);
}

void ToplevelService::clear() {
  for (const auto& r : snapshot_) {
    ipc_.publish(kToplevelClosed, std::to_string(r.id));
  }
  keyToId_.clear();
  snapshot_.clear();
}

} // namespace eh::windows
