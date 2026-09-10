#include "app/file_browser/features/recursive_search_worker.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace eh::file_browser {
namespace {

bool is_skip_dir(const std::string& name) {
  return name == "." || name == ".." || name == "snap" ||
         name == "lost+found";
}

bool matches(const std::string& name, const std::string& q_lower) {
  if (q_lower.empty()) return false;
  // Case-insensitive substring match on filename
  for (size_t i = 0; i < name.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(name[i])) !=
        static_cast<unsigned char>(q_lower[0]))
      continue;
    // Check the rest
    size_t j = 0;
    while (i + j < name.size() && j < q_lower.size() &&
           std::tolower(static_cast<unsigned char>(name[i + j])) ==
               static_cast<unsigned char>(q_lower[j]))
      ++j;
    if (j == q_lower.size()) return true;
  }
  return false;
}

} // namespace

RecursiveSearchWorker::RecursiveSearchWorker()
    : m_thread(&RecursiveSearchWorker::thread_main, this) {}

RecursiveSearchWorker::~RecursiveSearchWorker() {
  cancel();
  m_running.store(false);
  m_cv.notify_one();
  if (m_thread.joinable()) m_thread.join();
}

void RecursiveSearchWorker::start_search(const std::string& root_dir,
                                          const std::string& query) {
  // Clear remaining results from previous search
  {
    std::lock_guard<std::mutex> lock(m_out_mutex);
    std::queue<SearchResult> empty;
    std::swap(m_out, empty);
  }
  {
    std::lock_guard<std::mutex> lock(m_ctrl_mutex);
    m_root_dir = root_dir;
    m_query = query;
    m_search_pending = true;
    m_cancel_requested = false;
  }
  m_cv.notify_one();
}

bool RecursiveSearchWorker::poll(SearchResult& out) {
  std::lock_guard<std::mutex> lock(m_out_mutex);
  if (m_out.empty()) return false;
  out = std::move(m_out.front());
  m_out.pop();
  return true;
}

bool RecursiveSearchWorker::busy() {
  std::lock_guard<std::mutex> lock(m_ctrl_mutex);
  return m_search_pending;
}

void RecursiveSearchWorker::cancel() {
  {
    std::lock_guard<std::mutex> lock(m_ctrl_mutex);
    m_cancel_requested = true;
    m_search_pending = false;
  }
  {
    std::lock_guard<std::mutex> lock(m_out_mutex);
    std::queue<SearchResult> empty;
    std::swap(m_out, empty);
  }
}

void RecursiveSearchWorker::thread_main() {
  while (m_running.load()) {
    std::string root_dir, query;
    {
      std::unique_lock<std::mutex> lock(m_ctrl_mutex);
      m_cv.wait(lock, [this] {
        return m_search_pending || !m_running.load();
      });
      if (!m_running.load()) return;
      if (!m_search_pending) continue;
      root_dir = m_root_dir;
      query = m_query;
      m_search_pending = false;
    }

    std::string q_lower = query;
    std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    walk_directory(root_dir, "", q_lower, 0);

    // Signal that search is complete
    {
      std::lock_guard<std::mutex> lock(m_ctrl_mutex);
      m_search_pending = false;
    }
  }
}

void RecursiveSearchWorker::walk_directory(const std::string& dir,
                                            const std::string& rel,
                                            const std::string& q_lower,
                                            int depth) {
  // Check cancel
  {
    std::lock_guard<std::mutex> lock(m_ctrl_mutex);
    if (m_cancel_requested) return;
  }

  // Limit depth to avoid going too deep
  if (depth > 8) return;

  DIR* d = opendir(dir.c_str());
  if (!d) return;

  struct dirent* dent;
  while ((dent = readdir(d)) != nullptr) {
    // Check cancel periodically
    {
      std::lock_guard<std::mutex> lock(m_ctrl_mutex);
      if (m_cancel_requested) { closedir(d); return; }
    }

    std::string name = dent->d_name;
    if (name == "." || name == "..") continue;

    std::string full = dir + "/" + name;
    std::string relative = rel.empty() ? name : rel + "/" + name;

    bool is_dir = (dent->d_type == DT_DIR);

    if (is_dir) {
      if (is_skip_dir(name)) continue;
    }

    // Check if name matches query
    if (matches(name, q_lower)) {
      SearchResult r;
      r.path = full;
      r.relative_path = relative;
      r.is_dir = is_dir;
      {
        std::lock_guard<std::mutex> lock(m_out_mutex);
        m_out.push(std::move(r));
      }
    }

    // Recurse into subdirectories
    if (is_dir) {
      walk_directory(full, relative, q_lower, depth + 1);
    }
  }
  closedir(d);
}

RecursiveSearchWorker& recursive_search_worker() {
  static RecursiveSearchWorker instance;
  return instance;
}

} // namespace eh::file_browser
