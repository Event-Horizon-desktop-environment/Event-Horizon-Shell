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

  // Initial snapshot via request/response.
  const auto snapshot = ipc->request(std::string(kToplevelListCmd));
  if (!snapshot || !toplevel_decode_list(*snapshot, list_)) {
    std::cerr << "[toplevel-client] failed to fetch initial snapshot\n";
    return false;
  }

  ipc_ = std::move(ipc);
  notify();
  return true;
}

const ToplevelRecord* ToplevelClient::find(std::uint64_t id) const {
  for (const auto& r : list_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

void ToplevelClient::handle_event(std::string topic, std::string payload,
                                  std::vector<int>) {
  if (topic == kToplevelClosed) {
    std::uint64_t id = 0;
    for (const char c : payload) {
      if (c < '0' || c > '9') return;
      id = id * 10 + static_cast<std::uint64_t>(c - '0');
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
  auto it = std::find_if(list_.begin(), list_.end(),
                         [id](const ToplevelRecord& r) { return r.id == id; });
  if (it != list_.end()) {
    list_.erase(it);
  }
  notify();
}

} // namespace eh::windows
