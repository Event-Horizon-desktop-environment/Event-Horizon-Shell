#include "app/file_browser/features/progress.hpp"

#include <filesystem>
#include <system_error>
#include <vector>

#include "base/thread/thread_dispatch.hpp"
#include "base/thread/thread_pool.hpp"

namespace fs = std::filesystem;

namespace eh::file_browser {

static int count_files_recursive(const fs::path& path) {
  std::error_code ec;
  if (fs::is_directory(path, ec)) {
    int n = 0;
    for (auto& de : fs::recursive_directory_iterator(path, ec))
      if (de.is_regular_file(ec)) ++n;
    return n;
  }
  return 1;
}

static void copy_file_with_parents(const fs::path& src, const fs::path& dest,
                                    std::error_code& ec) {
  fs::create_directories(dest.parent_path(), ec);
  if (ec) return;
  fs::copy_file(src, dest, fs::copy_options::copy_symlinks, ec);
}

static void copy_recursive(const fs::path& src, const fs::path& dest,
                           std::atomic<int>& done, int total,
                           std::shared_ptr<OperationProgress> prog) {
  std::error_code ec;
  if (fs::is_directory(src, ec)) {
    fs::create_directories(dest, ec);
    if (ec) return;
    for (auto& de : fs::directory_iterator(src, ec)) {
      if (prog->cancel.load()) return;
      auto child_dest = dest / de.path().filename();
      copy_recursive(de.path(), child_dest, done, total, prog);
    }
  } else {
    copy_file_with_parents(src, dest, ec);
    if (!ec) {
      ++done;
      prog->copied_files.store(done.load());
      prog->progress.store(total > 0 ? static_cast<double>(done.load()) / total : 0.0);
      prog->current_file = src.filename().string();
    }
  }
}

static void do_operation(std::vector<std::string> src_paths,
                         std::string dest_dir, bool is_move,
                         std::shared_ptr<OperationProgress> prog,
                         OpCompleteCallback on_complete) {
  // Phase 1: count total files
  int total = 0;
  for (auto& src : src_paths) {
    total += count_files_recursive(src);
    if (prog->cancel.load()) { prog->active = false; goto done; }
  }
  prog->total_files.store(total);

  // Phase 2: copy each source
  {
    auto& done = prog->copied_files;
    for (auto& src : src_paths) {
      if (prog->cancel.load()) break;

      fs::path src_path(src);
      fs::path dest = fs::path(dest_dir) / src_path.filename();
      prog->current_file = src_path.filename().string();

      if (is_move) {
        std::error_code ec;
        fs::rename(src, dest, ec);
        if (!ec) {
          int n = count_files_recursive(src_path);
          int cur = done.load() + n;
          done.store(cur);
          prog->progress.store(total > 0 ? static_cast<double>(cur) / total : 0.0);
        } else {
          int before = done.load();
          copy_recursive(src_path, dest, done, total, prog);
          if (!prog->cancel.load() && done.load() > before)
            fs::remove_all(src, ec);
        }
      } else {
        copy_recursive(src_path, dest, done, total, prog);
      }
    }
  }

  prog->active = false;
  prog->current_file.clear();

done:
  bool cancelled = prog->cancel.load();
  DeferredCall::callLater([on_complete, cancelled] {
    if (on_complete) on_complete(cancelled);
  });
}

void start_async_op(const std::vector<std::string>& src_paths,
                    const std::string& dest_dir, bool is_move,
                    std::shared_ptr<OperationProgress> prog,
                    OpCompleteCallback on_complete) {
  if (src_paths.empty()) {
    if (on_complete) on_complete(false);
    return;
  }

  prog->active.store(true);
  prog->cancel.store(false);
  prog->progress.store(0.0);
  prog->copied_files.store(0);
  prog->total_files.store(0);

  ThreadPool::instance().enqueue(
      [src_paths, dest_dir, is_move, prog, on_complete] {
        do_operation(src_paths, dest_dir, is_move, prog, on_complete);
      });
}

} // namespace eh::file_browser
