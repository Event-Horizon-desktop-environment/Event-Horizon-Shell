#include "services/windows/toplevel_client.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace eh::windows {

bool ToplevelClient::start(const std::string& socket_path) {
  if (ipc_) return false;

  auto ipc = std::make_unique<eh::ipc::IpcClient>();
  if (ipc->connect(socket_path) < 0) {
    std::cerr << "[toplevel-client] failed to connect to " << socket_path << "\n";
    return false;
  }

  const bool created = ipc->subscribe(kToplevelCreated);
  const bool updated = ipc->subscribe(kToplevelUpdated);
  const bool closed = ipc->subscribe(kToplevelClosed);
  if (!created || !updated || !closed) {
    std::cerr << "[toplevel-client] failed to subscribe to toplevel topics\n";
    return false;
  }

  ipc->set_event_handler([this](std::string topic, std::string payload,
                                std::vector<int> fds) {
    handle_event(std::move(topic), std::move(payload), std::move(fds));
  });

  // Initial snapshot via request/response. Events arriving between
  // subscribe and fetch are buffered and replayed in order afterwards.
  {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    syncing_ = true;
  }
  const auto snapshot = ipc->request(std::string(kToplevelListCmd));
  {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (!snapshot || !toplevel_decode_list(*snapshot, list_)) {
      std::cerr << "[toplevel-client] failed to fetch initial snapshot\n";
      syncing_ = false;
      pending_.clear();
      return false;
    }
    for (auto& ev : pending_) dispatch_event(std::move(ev.topic), std::move(ev.payload));
    pending_.clear();
    syncing_ = false;
  }

  ipc_ = std::move(ipc);
  notify();
  return true;
}

std::vector<ToplevelRecord> ToplevelClient::list() const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  return list_;
}

const ToplevelRecord* ToplevelClient::find(std::uint64_t id) const {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  for (const auto& r : list_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

void ToplevelClient::handle_event(std::string topic, std::string payload,
                                  std::vector<int>) {
  {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (syncing_) {
      pending_.push_back(PendingEvent{std::move(topic), std::move(payload)});
      return;
    }
  }
  dispatch_event(std::move(topic), std::move(payload));
}

void ToplevelClient::dispatch_event(std::string topic, std::string payload) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  if (topic == kToplevelClosed) {
    if (payload.empty()) return;
    std::uint64_t id = 0;
    for (const char c : payload) {
      if (c < '0' || c > '9') return;
      const unsigned digit = static_cast<unsigned>(c - '0');
      if (id > (0xFFFFFFFFFFFFFFFFull - digit) / 10ull) return;
      id = id * 10 + digit;
    }
    remove_record(id);
    return;
  }

  if (topic != kToplevelCreated && topic != kToplevelUpdated) return;
  ToplevelRecord rec;
  size_t consumed = 0;
  if (!toplevel_decode_record(payload, 0, rec, consumed)) return;
  apply_record(rec, topic == kToplevelCreated);
}

void ToplevelClient::apply_record(const ToplevelRecord& rec, bool create) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  auto it = std::find_if(list_.begin(), list_.end(),
                         [&rec](const ToplevelRecord& r) { return r.id == rec.id; });
  if (it != list_.end()) {
    // Refresh content for created (duplicate/out-of-order) and updated alike.
    *it = rec;
  } else if (create) {
    list_.push_back(rec);
  }
  notify();
}

void ToplevelClient::remove_record(std::uint64_t id) {
  std::lock_guard<std::recursive_mutex> lock(mu_);
  auto it = std::find_if(list_.begin(), list_.end(),
                         [id](const ToplevelRecord& r) { return r.id == id; });
  if (it != list_.end()) {
    list_.erase(it);
  }
  notify();
}

} // namespace eh::windows
