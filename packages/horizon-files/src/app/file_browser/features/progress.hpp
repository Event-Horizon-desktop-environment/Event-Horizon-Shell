#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eh::file_browser {

enum class OperationType : uint8_t {
  None,
  Copy,
  Move,
};

struct OperationProgress {
  std::atomic<bool> active{false};
  std::atomic<bool> cancel{false};
  std::atomic<double> progress{0.0};
  std::atomic<int> total_files{0};
  std::atomic<int> copied_files{0};
  std::string current_file;
  OperationType type{OperationType::Copy};
};

using OpCompleteCallback =
    std::function<void(bool cancelled)>;

void start_async_op(
    const std::vector<std::string>& src_paths,
    const std::string& dest_dir,
    bool is_move,
    std::shared_ptr<OperationProgress> prog,
    OpCompleteCallback on_complete = nullptr);

} // namespace eh::file_browser
