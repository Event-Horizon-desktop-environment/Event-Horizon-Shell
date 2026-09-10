#include "archive_viewer/core/archive.hpp"
#ifdef HORIZON_HAS_GUI
#include "archive_viewer/ui/state.hpp"
#endif

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void print_usage(const char* prog) {
  std::fprintf(stderr,
    "Horizon Archive Manager\n"
    "Usage:\n"
    "  %s info <archive>                        Show archive metadata\n"
    "  %s list <archive> [<path/>]              List archive contents\n"
    "  %s search <archive> <pattern>            Search entries by glob\n"
    "  %s extract <archive> <dest> [<path>...]  Extract files\n"
    "  %s create <archive> <file>...            Create archive\n"
    "  %s convert <src> <dest>                  Convert between formats\n"
    "  %s check <archive>                       Verify integrity\n"
    "  %s hash <archive>                        SHA256 per entry\n"
    "  %s comment <archive>                     Show archive comment\n"
    "  %s batch-check <file>... -j<N>           Check multiple archives\n"
    "  %s batch-convert <file>... <dest_dir> <ext> -j<N>  Convert batch\n"
#ifdef HORIZON_HAS_GUI
    "  %s gui [<archive>]                      Launch archive viewer GUI\n"
    "  %s gui --create <file>...               Create archive via GUI\n"
#endif
    ,
    prog, prog, prog, prog, prog, prog, prog, prog, prog, prog, prog
#ifdef HORIZON_HAS_GUI
    , prog, prog
#endif
    );
}

int cmd_info(const std::string& archive) {
  auto info = archive_viewer::get_archive_info(archive);
  if (info.entry_count == 0 && !archive_viewer::last_error().empty()) {
    std::fprintf(stderr, "Error: %s\n", archive_viewer::last_error().c_str());
    return 1;
  }
  std::printf("format=%s\n", info.format.c_str());
  std::printf("compression=%s\n", info.compression.c_str());
  std::printf("entries=%zu\n", info.entry_count);
  std::printf("files=%zu\n", info.file_count);
  std::printf("folders=%zu\n", info.folder_count);
  std::printf("size=%" PRIu64 "\n", info.total_size);

  char size_buf[64];
  if (info.total_size < 1024)
    std::snprintf(size_buf, sizeof(size_buf), "%" PRIu64 " B", info.total_size);
  else if (info.total_size < 1024 * 1024)
    std::snprintf(size_buf, sizeof(size_buf), "%.1f KB", info.total_size / 1024.0);
  else if (info.total_size < 1024 * 1024 * 1024)
    std::snprintf(size_buf, sizeof(size_buf), "%.1f MB", info.total_size / (1024.0 * 1024.0));
  else
    std::snprintf(size_buf, sizeof(size_buf), "%.1f GB", info.total_size / (1024.0 * 1024.0 * 1024.0));
  std::printf("size_human=%s\n", size_buf);
  std::printf("encrypted=%s\n", info.encrypted ? "true" : "false");
  return 0;
}

int cmd_list(const std::string& archive, const std::string& prefix) {
  auto entries = archive_viewer::scan_archive(archive);
  if (entries.empty() && !archive_viewer::last_error().empty()) {
    std::fprintf(stderr, "Error: %s\n", archive_viewer::last_error().c_str());
    return 1;
  }

  auto filtered = prefix.empty() ? std::move(entries)
                                 : archive_viewer::filter_entries(entries, prefix);
  for (const auto& e : filtered) {
    char mtime_buf[64] = {};
    if (e.mtime > 0) {
      struct tm tm{};
      localtime_r(&e.mtime, &tm);
      std::strftime(mtime_buf, sizeof(mtime_buf), "%Y-%m-%d %H:%M", &tm);
    }
    std::printf("%s\t%s\t%" PRIu64 "\t%s\n",
                e.is_dir ? "folder" : (e.is_symlink ? "symlink" : "file"),
                e.path.c_str(),
                e.size,
                mtime_buf);
  }
  return 0;
}

int cmd_extract(const std::string& archive, const std::string& dest,
                 const std::vector<std::string>& paths) {
  archive_viewer::ExtractResult result;
  if (paths.empty()) {
    result = archive_viewer::extract_all(archive, dest);
  } else {
    result = archive_viewer::extract_selected(archive, dest, paths);
  }

  if (!result.success && result.files_extracted == 0) {
    std::fprintf(stderr, "Error: %s\n",
                 result.error_msg.empty() ? "extraction failed" : result.error_msg.c_str());
    return 1;
  }

  std::printf("Extracted %zu files, %zu folders to %s\n",
              result.files_extracted, result.folders_created, dest.c_str());
  return 0;
}

int cmd_check(const std::string& archive) {
  std::string error;
  if (archive_viewer::check_integrity(archive, &error)) {
    std::printf("OK\n");
    return 0;
  }
  std::printf("FAIL: %s\n", error.c_str());
  return 1;
}

int cmd_search(const std::string& archive, const std::string& pattern) {
  auto entries = archive_viewer::scan_archive(archive);
  if (entries.empty() && !archive_viewer::last_error().empty()) {
    std::fprintf(stderr, "Error: %s\n", archive_viewer::last_error().c_str());
    return 1;
  }

  auto matched = archive_viewer::search_entries(entries, pattern);
  for (const auto& e : matched) {
    char mtime_buf[64] = {};
    if (e.mtime > 0) {
      struct tm tm{};
      localtime_r(&e.mtime, &tm);
      std::strftime(mtime_buf, sizeof(mtime_buf), "%Y-%m-%d %H:%M", &tm);
    }
    std::printf("%s\t%s\t%" PRIu64 "\t%s\n",
                e.is_dir ? "folder" : (e.is_symlink ? "symlink" : "file"),
                e.path.c_str(), e.size, mtime_buf);
  }
  return 0;
}

int cmd_create(const std::string& archive,
                const std::vector<std::string>& files) {
  std::string error;
  if (archive_viewer::create_archive(archive, files, &error)) {
    std::printf("Created %s (%zu files)\n", archive.c_str(), files.size());
    return 0;
  }
  std::fprintf(stderr, "Error: %s\n", error.c_str());
  return 1;
}

int cmd_convert(const std::string& src, const std::string& dest) {
  std::string error;
  if (archive_viewer::convert_archive(src, dest, &error)) {
    std::printf("Converted %s -> %s\n", src.c_str(), dest.c_str());
    return 0;
  }
  std::fprintf(stderr, "Error: %s\n", error.c_str());
  return 1;
}

int cmd_hash(const std::string& archive) {
  std::string error;
  auto hashes = archive_viewer::hash_entries(archive, &error);
  if (hashes.empty() && !error.empty()) {
    std::fprintf(stderr, "Error: %s\n", error.c_str());
    return 1;
  }
  for (const auto& h : hashes) {
    std::printf("%s  %s\n", h.sha256.c_str(), h.path.c_str());
  }
  return 0;
}

int cmd_comment(const std::string& archive) {
  auto comment = archive_viewer::get_comment(archive);
  if (comment.empty()) {
    std::printf("(no comment)\n");
    return 0;
  }
  std::printf("%s\n", comment.c_str());
  return 0;
}

// ── Batch commands ───────────────────────────────────────────────────

// Parse -j<N> from args, remove the flag and return jobs count (0 = default).
// Updates argc/argv in-place to exclude the flag.
size_t parse_jobs(int& argc, char** argv) {
  for (int i = 0; i < argc; ++i) {
    std::string a = argv[i];
    if (a.size() > 2 && a[0] == '-' && a[1] == 'j') {
      size_t n = static_cast<size_t>(std::stoul(a.substr(2)));
      // Remove this arg by shifting
      for (int j = i; j < argc - 1; ++j) argv[j] = argv[j + 1];
      --argc;
      return n;
    }
  }
  return 0;
}

int cmd_batch_check(int argc, char** argv) {
  size_t jobs = parse_jobs(argc, argv);

  std::vector<std::string> paths;
  for (int i = 0; i < argc; ++i)
    paths.emplace_back(argv[i]);

  if (paths.empty()) {
    std::fprintf(stderr, "No files specified\n");
    return 1;
  }

  size_t jobs_used = jobs ? jobs : std::thread::hardware_concurrency();
  std::fprintf(stderr, "Checking %zu archives with %zu threads...\n",
               paths.size(), jobs_used);

  auto results = archive_viewer::batch_check(paths, jobs);
  int failed = 0;
  for (const auto& r : results) {
    std::printf("%s  %s\n", r.ok ? "OK" : "FAIL", r.path.c_str());
    if (!r.ok) {
      std::fprintf(stderr, "  %s: %s\n", r.path.c_str(), r.message.c_str());
      ++failed;
    }
  }
  std::fprintf(stderr, "Done: %zu OK, %d FAILED\n", results.size() - failed, failed);
  return failed > 0 ? 1 : 0;
}

int cmd_batch_convert(int argc, char** argv) {
  // batch-convert <src>... <dest_dir> <ext> [-j<N>]
  if (argc < 3) {
    std::fprintf(stderr, "Usage: batch-convert <src>... <dest_dir> <ext> [-j<N>]\n");
    return 1;
  }

  size_t jobs = parse_jobs(argc, argv);

  // Last two args are dest_dir and ext
  std::string dest_dir = argv[argc - 2];
  std::string dest_ext = argv[argc - 1];

  std::vector<std::string> srcs;
  for (int i = 0; i < argc - 2; ++i)
    srcs.emplace_back(argv[i]);

  if (srcs.empty()) {
    std::fprintf(stderr, "No source files specified\n");
    return 1;
  }

  size_t jobs_used = jobs ? jobs : std::thread::hardware_concurrency();
  std::fprintf(stderr, "Converting %zu archives with %zu threads...\n",
               srcs.size(), jobs_used);

  auto results = archive_viewer::batch_convert(srcs, dest_dir, dest_ext, jobs);
  int failed = 0;
  for (const auto& r : results) {
    std::printf("%s  %s\n", r.ok ? "OK" : "FAIL", r.path.c_str());
    if (!r.ok) {
      std::fprintf(stderr, "  %s: %s\n", r.path.c_str(), r.message.c_str());
      ++failed;
    }
    if (r.ok) {
      std::printf("  -> %s\n", r.message.c_str() + 3);
    }
  }
  std::fprintf(stderr, "Done: %zu OK, %d FAILED\n", results.size() - failed, failed);
  return failed > 0 ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  std::string cmd = argv[1];

#ifdef HORIZON_HAS_GUI
  if (cmd == "gui") {
    std::string path;
    std::vector<std::string> create_files;
    if (argc > 2) {
      if (std::string(argv[2]) == "--create" && argc > 3) {
        for (int i = 3; i < argc; ++i) create_files.emplace_back(argv[i]);
      } else {
        path = argv[2];
      }
    }
    return archive_viewer::run_gui(path, create_files);
  }
#endif

  if (argc < 3) {
    print_usage(argv[0]);
    return 1;
  }

  if (cmd == "batch-check") {
    // batch-check <file>... [-j<N>]
    if (argc < 3) {
      std::fprintf(stderr, "Usage: %s batch-check <file>... [-j<N>]\n", argv[0]);
      return 1;
    }
    return cmd_batch_check(argc - 2, argv + 2);
  }

  if (cmd == "batch-convert") {
    // batch-convert <src>... <dest_dir> <ext> [-j<N>]
    if (argc < 5) {
      std::fprintf(stderr, "Usage: %s batch-convert <src>... <dest_dir> <ext> [-j<N>]\n", argv[0]);
      return 1;
    }
    return cmd_batch_convert(argc - 2, argv + 2);
  }

  // All other commands require at least 3 args
  if (argc < 3) {
    print_usage(argv[0]);
    return 1;
  }

  std::string archive = argv[2];

  if (cmd == "info") {
    return cmd_info(archive);
  }

  if (cmd == "list") {
    std::string prefix;
    if (argc > 3) prefix = argv[3];
    return cmd_list(archive, prefix);
  }

  if (cmd == "search") {
    if (argc < 4) {
      std::fprintf(stderr, "Usage: %s search <archive> <pattern>\n", argv[0]);
      return 1;
    }
    return cmd_search(archive, argv[3]);
  }

  if (cmd == "extract") {
    if (argc < 4) {
      std::fprintf(stderr, "Usage: %s extract <archive> <dest> [<path>...]\n", argv[0]);
      return 1;
    }
    std::string dest = argv[3];
    std::vector<std::string> paths;
    for (int i = 4; i < argc; ++i) paths.emplace_back(argv[i]);
    return cmd_extract(archive, dest, paths);
  }

  if (cmd == "create") {
    if (argc < 4) {
      std::fprintf(stderr, "Usage: %s create <archive> <file>...\n", argv[0]);
      return 1;
    }
    std::vector<std::string> files;
    for (int i = 3; i < argc; ++i) files.emplace_back(argv[i]);
    return cmd_create(archive, files);
  }

  if (cmd == "convert") {
    if (argc < 4) {
      std::fprintf(stderr, "Usage: %s convert <src> <dest>\n", argv[0]);
      return 1;
    }
    return cmd_convert(archive, argv[3]);
  }

  if (cmd == "check") {
    return cmd_check(archive);
  }

  if (cmd == "hash") {
    return cmd_hash(archive);
  }

  if (cmd == "comment") {
    return cmd_comment(archive);
  }

  std::fprintf(stderr, "Unknown command: %s\n", cmd.c_str());
  print_usage(argv[0]);
  return 1;
}
