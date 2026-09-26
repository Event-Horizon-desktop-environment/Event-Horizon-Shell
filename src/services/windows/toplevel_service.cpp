#include "services/windows/toplevel_service.hpp"

#include <iostream>
#include <mutex>
#include <unordered_set>
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
    std::lock_guard<std::mutex> lock(mu_);
    std::string out;
    toplevel_encode_list(snapshot_, out);
    return out;
  });
}

void ToplevelService::set_snapshot(std::vector<ToplevelRecord> incoming) {
  std::lock_guard<std::mutex> lock(mu_);
  incoming.erase(std::remove_if(incoming.begin(), incoming.end(),
                                [](const ToplevelRecord& r) { return r.key.empty(); }),
                 incoming.end());
  std::unordered_set<std::uint64_t> reborn;
  // Assign stable ids to every incoming key first, so created events carry the
  // id the client will keep referring to.
  for (auto& r : incoming) {
    auto it = keyToId_.find(r.key);
    if (it == keyToId_.end()) {
      r.id = nextId_++;
      keyToId_.emplace(r.key, r.id);
    } else {
      const ToplevelRecord* prev = nullptr;
      for (const auto& cur : snapshot_) {
        if (cur.key == r.key) {
          prev = &cur;
          break;
        }
      }
      if (prev && prev->appId != r.appId) {
        ipc_.publish(kToplevelClosed, std::to_string(it->second));
        keyToId_.erase(it);
        r.id = nextId_++;
        keyToId_.emplace(r.key, r.id);
        reborn.insert(r.id);
      } else {
        r.id = it->second;
      }
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
    if (!prev || reborn.count(r.id) != 0) {
      std::string payload;
      toplevel_encode_record(r, payload);
      ipc_.publish(kToplevelCreated, payload);
    } else if (!same_fields(*prev, r)) {
      std::string payload;
      toplevel_encode_record(r, payload);
      ipc_.publish(kToplevelUpdated, payload);
    }
  }

  snapshot_ = std::move(incoming);
}

void ToplevelService::clear() {
  std::lock_guard<std::mutex> lock(mu_);
  for (const auto& r : snapshot_) {
    ipc_.publish(kToplevelClosed, std::to_string(r.id));
  }
  keyToId_.clear();
  snapshot_.clear();
}

void ToplevelService::unregister_handlers() {
  ipc_.unregister_handler(kToplevelListCmd);
}

std::vector<ToplevelRecord> ToplevelService::snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return snapshot_;
}

} // namespace eh::windows
