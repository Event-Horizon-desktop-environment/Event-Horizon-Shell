#pragma once
// Disks 51 job-tracking system: live progress for format / resize /
// restore / SMART / benchmark with per-row spinner + global job bar.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace eh::disks {

struct Job {
  uint64_t id = 0;
  std::string label;        // e.g. "Formatting /dev/sda1…"
  std::string block_path;   // object path the job belongs to ("" = drive/global)
  double progress = -1;     // -1 = indeterminate
  bool cancellable = false;
  bool done = false;
  bool ok = false;
  std::string message;
  double started_mono = 0;
};

class JobTracker {
 public:
  static JobTracker& instance();

  std::shared_ptr<Job> start(const std::string& label, const std::string& block_path,
                             bool cancellable);
  void update(uint64_t id, double progress, const std::string& message = "");
  void finish(uint64_t id, bool ok, const std::string& message = "");

  std::vector<std::shared_ptr<Job>> active() const;
  std::vector<std::shared_ptr<Job>> recent() const;
  std::shared_ptr<Job> for_block(const std::string& block_path) const;
  bool has_active() const;
  void prune();

 private:
  JobTracker() = default;
  mutable std::mutex mtx_;
  uint64_t next_ = 1;
  std::vector<std::shared_ptr<Job>> jobs_;
};

double mono_seconds();

}  // namespace eh::disks
