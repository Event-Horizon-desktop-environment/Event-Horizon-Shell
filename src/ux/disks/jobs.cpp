#include "ux/disks/jobs.hpp"

#include <algorithm>
#include <chrono>

namespace eh::disks {

double mono_seconds() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

JobTracker& JobTracker::instance() {
  static JobTracker t;
  return t;
}

std::shared_ptr<Job> JobTracker::start(const std::string& label,
                                       const std::string& block_path,
                                       bool cancellable) {
  auto j = std::make_shared<Job>();
  std::lock_guard<std::mutex> lk(mtx_);
  j->id = next_++;
  j->label = label;
  j->block_path = block_path;
  j->cancellable = cancellable;
  j->started_mono = mono_seconds();
  jobs_.push_back(j);
  // Keep bounded.
  if (jobs_.size() > 32) jobs_.erase(jobs_.begin());
  return j;
}

void JobTracker::update(uint64_t id, double progress, const std::string& message) {
  std::lock_guard<std::mutex> lk(mtx_);
  for (auto& j : jobs_) {
    if (j->id == id && !j->done) {
      j->progress = progress;
      if (!message.empty()) j->message = message;
    }
  }
}

void JobTracker::finish(uint64_t id, bool ok, const std::string& message) {
  std::lock_guard<std::mutex> lk(mtx_);
  for (auto& j : jobs_) {
    if (j->id == id) {
      j->done = true;
      j->ok = ok;
      j->progress = 1.0;
      if (!message.empty()) j->message = message;
    }
  }
}

std::vector<std::shared_ptr<Job>> JobTracker::active() const {
  std::lock_guard<std::mutex> lk(mtx_);
  std::vector<std::shared_ptr<Job>> out;
  for (auto& j : jobs_)
    if (!j->done) out.push_back(j);
  return out;
}

std::vector<std::shared_ptr<Job>> JobTracker::recent() const {
  std::lock_guard<std::mutex> lk(mtx_);
  std::vector<std::shared_ptr<Job>> out = jobs_;
  std::reverse(out.begin(), out.end());
  if (out.size() > 8) out.resize(8);
  return out;
}

std::shared_ptr<Job> JobTracker::for_block(const std::string& block_path) const {
  if (block_path.empty()) return nullptr;
  std::lock_guard<std::mutex> lk(mtx_);
  for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) {
    if (!(*it)->done && (*it)->block_path == block_path) return *it;
  }
  return nullptr;
}

bool JobTracker::has_active() const {
  std::lock_guard<std::mutex> lk(mtx_);
  for (auto& j : jobs_)
    if (!j->done) return true;
  return false;
}

void JobTracker::prune() {
  std::lock_guard<std::mutex> lk(mtx_);
  double now = mono_seconds();
  jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                             [now](const std::shared_ptr<Job>& j) {
                               return j->done && (now - j->started_mono) > 12.0;
                             }),
              jobs_.end());
}

}  // namespace eh::disks
