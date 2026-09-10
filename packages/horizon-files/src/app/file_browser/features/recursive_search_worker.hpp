#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace eh::file_browser {

struct SearchResult {
  std::string path;
  std::string relative_path; // relative to search root
  bool is_dir = false;
};

class RecursiveSearchWorker {
public:
  RecursiveSearchWorker();
  ~RecursiveSearchWorker();

  /// Start a new recursive search from root_dir for files matching query.
  /// Cancels any in-flight search.
  void start_search(const std::string& root_dir, const std::string& query);

  /// Poll available results. Returns false when no more results in queue.
  bool poll(SearchResult& out);

  /// Returns true if a search is currently running.
  bool busy();

  /// Cancel any running search and clear pending results.
  void cancel();

private:
  void thread_main();
  void walk_directory(const std::string& dir, const std::string& rel,
                      const std::string& q_lower, int depth);

  std::thread m_thread;
  std::mutex m_out_mutex;
  std::queue<SearchResult> m_out;

  std::mutex m_ctrl_mutex;
  std::string m_root_dir;
  std::string m_query;
  bool m_search_pending = false;
  bool m_cancel_requested = false;

  std::condition_variable m_cv;
  std::atomic<bool> m_running{true};
};

RecursiveSearchWorker& recursive_search_worker();

} // namespace eh::file_browser
