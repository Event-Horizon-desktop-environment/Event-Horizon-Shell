#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace archive_viewer {

struct ArchiveEntry {
  std::string path;
  uint64_t size = 0;
  int64_t mtime = 0;
  bool is_dir = false;
  bool is_symlink = false;
  bool is_hardlink = false;
  std::string hardlink_target;
  std::string symlink_target;
};

struct ArchiveInfo {
  std::string format;
  std::string compression;
  size_t entry_count = 0;
  size_t file_count = 0;
  size_t folder_count = 0;
  uint64_t total_size = 0;
  bool encrypted = false;
};

// Scan all entries in an archive into memory.
// Returns empty vector on error; check last_error() for message.
std::vector<ArchiveEntry> scan_archive(const std::string& path);
ArchiveInfo get_archive_info(const std::string& path);

// Filter entries by prefix (e.g. "docs/subdir/"). Empty prefix returns all.
std::vector<ArchiveEntry> filter_entries(const std::vector<ArchiveEntry>& entries,
                                          const std::string& prefix);

// Search entries whose path matches a glob pattern (*, ?).
std::vector<ArchiveEntry> search_entries(const std::vector<ArchiveEntry>& entries,
                                          const std::string& pattern);

struct ExtractResult {
  bool success = false;
  size_t files_extracted = 0;
  size_t folders_created = 0;
  size_t hardlinks_skipped = 0;
  size_t encrypted_skipped = 0;
  std::string error_msg;
};

// Extract all entries preserving paths.
// If progress is non-null, it's updated with 0.0–1.0 fraction based on
// total_entries (pass 0 to skip progress).
ExtractResult extract_all(const std::string& archive_path,
                           const std::string& dest,
                           std::atomic<float>* progress = nullptr,
                           size_t total_entries = 0);

// Extract only entries whose paths match the given set.
// If an entry is a directory, all descendants are included.
// If progress is non-null, it's updated with 0.0–1.0 fraction based on
// total_entries (pass 0 to skip progress).
ExtractResult extract_selected(const std::string& archive_path,
                                 const std::string& dest,
                                 const std::vector<std::string>& paths,
                                 std::atomic<float>* progress = nullptr,
                                 size_t total_entries = 0);

// Quick integrity check — tries to open and read headers.
bool check_integrity(const std::string& path, std::string* error_out);

// Create a new archive from files/directories on disk.
bool create_archive(const std::string& archive_path,
                     const std::vector<std::string>& files,
                     std::string* error_out);

// Same, with progress callback (progress updated 0.0–1.0 over total_entries).
bool create_archive(const std::string& archive_path,
                     const std::vector<std::string>& files,
                     std::string* error_out,
                     std::atomic<float>* progress,
                     size_t total_entries);

// Convert archive from one format to another (reads src, writes dest).
bool convert_archive(const std::string& src,
                      const std::string& dest,
                      std::string* error_out);

struct EntryHash {
  std::string path;
  std::string sha256;
};

// Compute SHA256 of entry data.
std::vector<EntryHash> hash_entries(const std::string& archive_path,
                                     std::string* error_out);

// Read archive-level comment string (ZIP, 7z).
std::string get_comment(const std::string& archive_path);

// Last error message (thread-local).
const std::string& last_error();

// ── Batch / parallel operations ──────────────────────────────────────

struct BatchItem {
  std::string path;
  bool ok = false;
  std::string message;
};

// Check multiple archives in parallel using thread pool.
std::vector<BatchItem> batch_check(const std::vector<std::string>& paths,
                                    size_t jobs = 0);

// Convert multiple source archives into dest_dir with dest_extension.
// e.g. batch_convert({"a.zip","b.zip"}, "/out", ".tar.gz")
std::vector<BatchItem> batch_convert(const std::vector<std::string>& srcs,
                                      const std::string& dest_dir,
                                      const std::string& dest_ext,
                                      size_t jobs = 0);

} // namespace archive_viewer
