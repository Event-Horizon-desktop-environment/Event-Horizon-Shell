#include "archive_viewer/core/archive.hpp"
#include "archive_viewer/core/thread_pool.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <regex>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>

#include <openssl/evp.h>

namespace archive_viewer {
namespace {

// Thread-local error storage
thread_local std::string tls_error;

void set_error(const std::string& msg) {
  tls_error = msg;
}

struct ArchiveReader {
  struct archive* a = nullptr;
  bool ok = false;

  ArchiveReader(const std::string& path) {
    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    int r = archive_read_open_filename(a, path.c_str(), 10240);
    if (r != ARCHIVE_OK) {
      set_error(archive_error_string(a));
      archive_read_free(a);
      a = nullptr;
      ok = false;
    } else {
      ok = true;
    }
  }

  ~ArchiveReader() {
    if (a) {
      archive_read_close(a);
      archive_read_free(a);
    }
  }

  bool read_next(struct archive_entry** entry) {
    if (!a) return false;
    int r = archive_read_next_header(a, entry);
    if (r == ARCHIVE_EOF) return false;
    if (r < ARCHIVE_OK) {
      std::string warn = archive_error_string(a);
      if (r < ARCHIVE_WARN) {
        set_error(warn);
        return false;
      }
    }
    return true;
  }

  void skip_data() {
    if (a) archive_read_data_skip(a);
  }
};

} // namespace

const std::string& last_error() {
  return tls_error;
}

std::vector<ArchiveEntry> scan_archive(const std::string& path) {
  set_error({});
  std::vector<ArchiveEntry> entries;
  ArchiveReader reader(path);
  if (!reader.ok) {
    // log the error via tls_error (already set in ArchiveReader ctor)
    FILE* lf = fopen("/tmp/horizon-archive.log", "a");
    if (lf) {
      struct timespec ts{}; clock_gettime(CLOCK_REALTIME, &ts);
      struct tm tm{}; localtime_r(&ts.tv_sec, &tm);
      std::fprintf(lf, "%02d:%02d:%02d.%03ld [libarchive] scan_archive(%s) FAILED: %s\n",
                   tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000,
                   path.c_str(), tls_error.c_str());
      std::fclose(lf);
    }
    return entries;
  }

  struct archive_entry* ae = nullptr;
  while (reader.read_next(&ae)) {
    ArchiveEntry e;
    e.path = archive_entry_pathname(ae);
    e.size = static_cast<uint64_t>(archive_entry_size(ae));
    e.mtime = archive_entry_mtime(ae);
    e.is_dir = archive_entry_filetype(ae) == AE_IFDIR;
    e.is_symlink = archive_entry_filetype(ae) == AE_IFLNK;
    if (e.is_symlink && archive_entry_symlink(ae)) {
      e.symlink_target = archive_entry_symlink(ae);
    }
    const char* hl = archive_entry_hardlink(ae);
    if (hl) {
      e.is_hardlink = true;
      e.hardlink_target = hl;
    }
    entries.push_back(std::move(e));
    reader.skip_data();
  }

  FILE* lf = fopen("/tmp/horizon-archive.log", "a");
  if (lf) {
    struct timespec ts{}; clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm{}; localtime_r(&ts.tv_sec, &tm);
    std::fprintf(lf, "%02d:%02d:%02d.%03ld [libarchive] scan_archive(%s) -> %zu entries, error=%s\n",
                 tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000,
                 path.c_str(), entries.size(), tls_error.c_str());
    std::fclose(lf);
  }

  return entries;
}

ArchiveInfo get_archive_info(const std::string& path) {
  ArchiveInfo info;
  auto entries = scan_archive(path);
  if (entries.empty() && !tls_error.empty()) return info;

  info.entry_count = entries.size();
  for (const auto& e : entries) {
    if (e.is_dir) {
      ++info.folder_count;
    } else {
      ++info.file_count;
      info.total_size += e.size;
    }
  }

  // Detect format from extension
  std::string lower_path = path;
  std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  auto has_suffix = [&](const std::string& suf) {
    return lower_path.size() >= suf.size() &&
           lower_path.compare(lower_path.size() - suf.size(), suf.size(), suf) == 0;
  };

  if (has_suffix(".tar.gz") || has_suffix(".tgz")) {
    info.format = "Tar"; info.compression = "gzip";
  } else if (has_suffix(".tar.bz2") || has_suffix(".tbz2") || has_suffix(".tbz")) {
    info.format = "Tar"; info.compression = "bzip2";
  } else if (has_suffix(".tar.xz") || has_suffix(".txz")) {
    info.format = "Tar"; info.compression = "xz";
  } else if (has_suffix(".tar.zst") || has_suffix(".tzst")) {
    info.format = "Tar"; info.compression = "zstd";
  } else if (has_suffix(".tar.lz") || has_suffix(".tlz")) {
    info.format = "Tar"; info.compression = "lzip";
  } else if (has_suffix(".tar.lzma")) {
    info.format = "Tar"; info.compression = "lzma";
  } else if (has_suffix(".tar.lzo")) {
    info.format = "Tar"; info.compression = "lzo";
  } else if (has_suffix(".zip") && !has_suffix(".zipx")) {
    info.format = "ZIP"; info.compression = "deflate";
  } else if (has_suffix(".tar")) {
    info.format = "Tar"; info.compression = "none";
  } else if (has_suffix(".7z")) {
    info.format = "7z"; info.compression = "LZMA2";
  } else if (has_suffix(".rar")) {
    info.format = "RAR"; info.compression = "RAR";
  } else if (has_suffix(".gz")) {
    info.format = "GZip"; info.compression = "gzip";
  } else if (has_suffix(".bz2")) {
    info.format = "BZip2"; info.compression = "bzip2";
  } else if (has_suffix(".xz")) {
    info.format = "XZ"; info.compression = "xz";
  } else if (has_suffix(".zst")) {
    info.format = "Zstd"; info.compression = "zstd";
  } else if (has_suffix(".lz")) {
    info.format = "Lzip"; info.compression = "lzip";
  } else if (has_suffix(".lzma")) {
    info.format = "LZMA"; info.compression = "lzma";
  } else if (has_suffix(".lzo")) {
    info.format = "LZO"; info.compression = "lzo";
  } else if (has_suffix(".iso")) {
    info.format = "ISO"; info.compression = "none";
  } else if (has_suffix(".cab")) {
    info.format = "CAB"; info.compression = "mszip";
  } else if (has_suffix(".cpio")) {
    info.format = "CPIO"; info.compression = "none";
  } else if (has_suffix(".rpm")) {
    info.format = "RPM"; info.compression = "none";
  } else if (has_suffix(".deb")) {
    info.format = "DEB"; info.compression = "ar";
  } else if (has_suffix(".zpaq") || has_suffix(".paq")) {
    info.format = "ZPAQ"; info.compression = "zpaq";
  } else {
    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
      info.format = path.substr(dot + 1);
      info.format[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(info.format[0])));
      info.compression = "unknown";
    }
  }

  return info;
}

std::vector<ArchiveEntry> filter_entries(const std::vector<ArchiveEntry>& entries,
                                          const std::string& prefix) {
  if (prefix.empty()) return entries;

  std::vector<ArchiveEntry> result;
  std::string p = prefix;
  if (p.back() != '/') p += '/';

  for (const auto& e : entries) {
    if (e.path.compare(0, p.size(), p) == 0) {
      result.push_back(e);
    }
  }
  return result;
}

namespace {

bool path_matches(const std::string& entry_path,
                   const std::unordered_set<std::string>& targets) {
  if (targets.count(entry_path)) return true;
  for (const auto& t : targets) {
    if (entry_path.compare(0, t.size(), t) == 0 &&
        (t.back() == '/' || entry_path.size() == t.size() ||
         entry_path[t.size()] == '/')) {
      return true;
    }
  }
  return false;
}

// Sanitize an extraction path: strip leading /, reject .. traversal
std::string safe_path(const std::string& entry_path) {
  std::string p = entry_path;
  // Strip leading /
  while (!p.empty() && p[0] == '/') p.erase(p.begin());
  // Strip .. components
  std::istringstream ss(p);
  std::string seg;
  std::vector<std::string> clean;
  while (std::getline(ss, seg, '/')) {
    if (seg == "..") {
      if (!clean.empty()) clean.pop_back();
    } else if (!seg.empty() && seg != ".") {
      clean.push_back(seg);
    }
  }
  std::string result;
  for (size_t i = 0; i < clean.size(); ++i) {
    if (i > 0) result += '/';
    result += clean[i];
  }
  return result;
}

ExtractResult do_extract(struct archive* a,
                          const std::string& dest,
                          const std::unordered_set<std::string>& targets,
                          bool extract_all,
                          std::atomic<float>* progress = nullptr,
                          size_t total_entries = 0) {
  ExtractResult result;
  size_t processed = 0;
  struct archive* disk = archive_write_disk_new();
  archive_write_disk_set_options(disk,
    ARCHIVE_EXTRACT_TIME |
    ARCHIVE_EXTRACT_PERM |
    ARCHIVE_EXTRACT_ACL |
    ARCHIVE_EXTRACT_FFLAGS |
    ARCHIVE_EXTRACT_SECURE_SYMLINKS |
    ARCHIVE_EXTRACT_SECURE_NODOTDOT |
    ARCHIVE_EXTRACT_UNLINK);
  archive_write_disk_set_standard_lookup(disk);

  struct archive_entry* ae = nullptr;
  int r;
  bool any_fail = false;

  while ((r = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
    std::string entry_path = archive_entry_pathname(ae);

    // Skip encrypted entries
    if (archive_entry_is_encrypted(ae)) {
      ++result.encrypted_skipped;
      archive_read_data_skip(a);
      continue;
    }

    bool want = extract_all || path_matches(entry_path, targets);
    if (!want) {
      archive_read_data_skip(a);
      continue;
    }

    std::string clean = safe_path(entry_path);
    if (clean.empty()) {
      archive_read_data_skip(a);
      continue;
    }
    std::string full = dest + "/" + clean;
    archive_entry_set_pathname(ae, full.c_str());

    // Handle hard links: write as empty file with link target
    const char* hardlink = archive_entry_hardlink(ae);
    if (hardlink) {
      std::string hl_clean = safe_path(hardlink);
      std::string hl_full = dest + "/" + hl_clean;
      archive_entry_set_hardlink(ae, hl_full.c_str());
    }

    r = archive_write_header(disk, ae);
    if (r != ARCHIVE_OK) {
      // ARCHIVE_WARN for hardlinks that can't be created is non-fatal
      if (r < ARCHIVE_WARN) {
        any_fail = true;
        if (result.error_msg.empty()) result.error_msg = archive_error_string(disk);
      } else {
        // WARN only — could be hardlink across filesystems
        if (result.error_msg.empty()) result.error_msg = archive_error_string(disk);
      }
      archive_read_data_skip(a);
    } else {
      if (archive_entry_size(ae) > 0 && !hardlink) {
        char buf[65536];
        ssize_t len;
        while ((len = archive_read_data(a, buf, sizeof(buf))) > 0) {
          ssize_t written = archive_write_data(disk, buf, len);
          if (written < 0) {
            any_fail = true;
            break;
          }
        }
      }
      r = archive_write_finish_entry(disk);
      if (r != ARCHIVE_OK && r < ARCHIVE_WARN) {
        any_fail = true;
      }
      if (archive_entry_filetype(ae) == AE_IFDIR) {
        ++result.folders_created;
      } else if (hardlink) {
        ++result.hardlinks_skipped;
      } else {
        ++result.files_extracted;
      }
    }
    if (progress && total_entries > 0) {
      *progress = static_cast<float>(++processed) / total_entries;
    }
  }

  if (progress) *progress = 1.0f;
  if (r != ARCHIVE_EOF && r < ARCHIVE_WARN) {
    any_fail = true;
    if (result.error_msg.empty()) result.error_msg = archive_error_string(a);
  }

  archive_write_close(disk);
  archive_write_free(disk);

  result.success = !any_fail || result.files_extracted > 0;
  return result;
}

} // namespace

ExtractResult extract_all(const std::string& archive_path,
                           const std::string& dest,
                           std::atomic<float>* progress,
                           size_t total_entries) {
  ArchiveReader reader(archive_path);
  if (!reader.ok) {
    return {false, 0, 0, 0, 0, tls_error};
  }
  std::unordered_set<std::string> empty;
  return do_extract(reader.a, dest, empty, true, progress, total_entries);
}

ExtractResult extract_selected(const std::string& archive_path,
                                 const std::string& dest,
                                 const std::vector<std::string>& paths,
                                 std::atomic<float>* progress,
                                 size_t total_entries) {
  ArchiveReader reader(archive_path);
  if (!reader.ok) {
    return {false, 0, 0, 0, 0, tls_error};
  }
  std::unordered_set<std::string> targets(paths.begin(), paths.end());

  // Expand directory selections — include all descendants
  // We need two passes: first scan to find all entries, then filter
  auto all = scan_archive(archive_path);
  for (const auto& e : all) {
    for (const auto& t : paths) {
      if (e.path.compare(0, t.size(), t) == 0 &&
          (t.back() == '/' || e.path.size() == t.size() ||
           e.path[t.size()] == '/')) {
        targets.insert(e.path);
      }
    }
  }

  // Re-open archive for extraction
  ArchiveReader reader2(archive_path);
  if (!reader2.ok) {
    return {false, 0, 0, 0, 0, tls_error};
  }
  return do_extract(reader2.a, dest, targets, false, progress, total_entries);
}

bool check_integrity(const std::string& path, std::string* error_out) {
  set_error({});
  ArchiveReader reader(path);
  if (!reader.ok) {
    if (error_out) *error_out = tls_error;
    return false;
  }

  struct archive_entry* ae = nullptr;
  while (reader.read_next(&ae)) {
    reader.skip_data();
  }

  bool ok = tls_error.empty();
  if (!ok && error_out) *error_out = tls_error;
  return ok;
}

std::vector<ArchiveEntry> search_entries(const std::vector<ArchiveEntry>& entries,
                                          const std::string& pattern) {
  std::vector<ArchiveEntry> result;
  std::string glob;
  // Convert simple glob: * -> .*, ? -> ., escape regex specials
  for (char c : pattern) {
    if (c == '*') glob += ".*";
    else if (c == '?') glob += ".";
    else if (c == '.' || c == '+' || c == '^' || c == '$' ||
             c == '[' || c == ']' || c == '(' || c == ')' ||
             c == '{' || c == '}' || c == '\\' || c == '|')
      glob += '\\', glob += c;
    else
      glob += c;
  }

  try {
    std::regex re(glob, std::regex::icase | std::regex::optimize);
    for (const auto& e : entries) {
      if (std::regex_search(e.path, re)) {
        result.push_back(e);
      }
    }
  } catch (const std::regex_error&) {
    // fallback: simple substring match
    for (const auto& e : entries) {
      if (e.path.find(pattern) != std::string::npos) {
        result.push_back(e);
      }
    }
  }

  return result;
}

namespace {

// Determine archive write format/filter from output filename extension.
// Returns the format name string for archive_write_set_format_filter_by_ext.
// Special-cases bare .gz/.bz2/.xz/.zst (raw, no tar wrapper).
bool set_write_format_by_ext(struct archive* a, const std::string& path) {
  std::string lower = path;
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  // Two-part extensions
  const char* filter = nullptr;
  const char* fmt = nullptr;

  if (lower.size() > 7 && lower.substr(lower.size() - 7) == ".tar.gz") {
    filter = "gzip"; fmt = "pax";
  } else if (lower.size() > 8 && lower.substr(lower.size() - 8) == ".tar.bz2") {
    filter = "bzip2"; fmt = "pax";
  } else if (lower.size() > 7 && lower.substr(lower.size() - 7) == ".tar.xz") {
    filter = "xz"; fmt = "pax";
  } else if (lower.size() > 8 && lower.substr(lower.size() - 8) == ".tar.zst") {
    filter = "zstd"; fmt = "pax";
  } else if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".tgz") {
    filter = "gzip"; fmt = "pax";
  } else if (lower.size() > 5 && lower.substr(lower.size() - 5) == ".tbz2") {
    filter = "bzip2"; fmt = "pax";
  } else if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".txz") {
    filter = "xz"; fmt = "pax";
  } else if (lower.size() > 5 && lower.substr(lower.size() - 5) == ".tzst") {
    filter = "zstd"; fmt = "pax";
  } else {
    // Single extension
    auto dot = lower.rfind('.');
    if (dot == std::string::npos) {
      set_error("no extension: cannot determine archive format");
      return false;
    }
    std::string ext = lower.substr(dot + 1);
    if (ext == "zip") {
      fmt = "zip";
    } else if (ext == "tar") {
      fmt = "pax";
    } else if (ext == "7z") {
      fmt = "7zip";
    } else if (ext == "gz") {
      fmt = "raw"; filter = "gzip";
    } else if (ext == "bz2") {
      fmt = "raw"; filter = "bzip2";
    } else if (ext == "xz") {
      fmt = "raw"; filter = "xz";
    } else if (ext == "zst") {
      fmt = "raw"; filter = "zstd";
    } else if (ext == "cpio") {
      fmt = "cpio";
    } else if (ext == "iso") {
      fmt = "iso9660";
    } else if (ext == "cab") {
      fmt = "cab";
    } else if (ext == "rar") {
      fmt = "rar";
    } else {
      // Let libarchive guess from extension
      return archive_write_set_format_filter_by_ext(a, path.c_str()) == ARCHIVE_OK;
    }
  }

  if (filter) {
    int r = archive_write_add_filter_by_name(a, filter);
    if (r != ARCHIVE_OK) { set_error(archive_error_string(a)); return false; }
  }
  if (fmt) {
    int r = archive_write_set_format_by_name(a, fmt);
    if (r != ARCHIVE_OK) { set_error(archive_error_string(a)); return false; }
  }
  return true;
}

} // namespace

bool create_archive(const std::string& archive_path,
                     const std::vector<std::string>& files,
                     std::string* error_out) {
  if (files.empty()) {
    if (error_out) *error_out = "no input files specified";
    return false;
  }

  struct archive* a = archive_write_new();
  if (!set_write_format_by_ext(a, archive_path)) {
    if (error_out) *error_out = last_error();
    archive_write_free(a);
    return false;
  }

  int r = archive_write_open_filename(a, archive_path.c_str());
  if (r != ARCHIVE_OK) {
    if (error_out) *error_out = archive_error_string(a);
    archive_write_free(a);
    return false;
  }

  bool any_fail = false;

  for (const auto& file : files) {
    // Use archive_read_disk to walk the filesystem entry
    struct archive* disk = archive_read_disk_new();
    archive_read_disk_set_standard_lookup(disk);

    r = archive_read_disk_open(disk, file.c_str());
    if (r != ARCHIVE_OK) {
      if (error_out && !any_fail) *error_out = archive_error_string(disk);
      any_fail = true;
      archive_read_close(disk);
      archive_read_free(disk);
      continue;
    }

    // Compute base offset for path relativization
    // Use realpath to resolve the input path, then strip it from entries
    char real_buf[PATH_MAX];
    std::string base;
    if (::realpath(file.c_str(), real_buf)) {
      base = real_buf;
      if (base.back() != '/') base += '/';
    }

    struct archive_entry* ae = nullptr;
    while ((r = archive_read_next_header(disk, &ae)) == ARCHIVE_OK) {
      std::string entry_path = archive_entry_pathname(ae);
      // relativize: strip the base path
      if (!base.empty() && entry_path.compare(0, base.size(), base) == 0) {
        entry_path = entry_path.substr(base.size());
      }
      if (entry_path.empty()) {
        if (archive_entry_filetype(ae) == AE_IFDIR)
          archive_read_disk_descend(disk);
        continue;
      }

      // Check for symlinks pointing outside
      if (archive_entry_filetype(ae) == AE_IFLNK) {
        const char* link = archive_entry_symlink(ae);
        if (link && link[0] == '/') {
          archive_read_data_skip(disk);
          if (archive_entry_filetype(ae) == AE_IFDIR)
            archive_read_disk_descend(disk);
          continue;
        }
      }

      archive_entry_set_pathname(ae, entry_path.c_str());
      archive_entry_set_size(ae, archive_entry_size(ae));

      r = archive_write_header(a, ae);
      if (r != ARCHIVE_OK) {
        if (error_out && !any_fail) *error_out = archive_error_string(a);
        any_fail = true;
        archive_read_data_skip(disk);
        if (archive_entry_filetype(ae) == AE_IFDIR)
          archive_read_disk_descend(disk);
        continue;
      }

      if (archive_entry_size(ae) > 0) {
        // Copy file data
        const char* src_path = archive_entry_sourcepath(ae);
        if (!src_path) {
          any_fail = true;
          archive_write_finish_entry(a);
          archive_read_data_skip(disk);
          continue;
        }
        int fd = ::open(src_path, O_RDONLY);
        if (fd < 0) {
          any_fail = true;
          archive_write_finish_entry(a);
          archive_read_data_skip(disk);
          continue;
        }
        char buf[65536];
        ssize_t nread;
        while ((nread = ::read(fd, buf, sizeof(buf))) > 0) {
          ssize_t written = archive_write_data(a, buf, static_cast<size_t>(nread));
          if (written < 0) {
            any_fail = true;
            break;
          }
        }
        ::close(fd);
      }

      r = archive_write_finish_entry(a);
      if (r != ARCHIVE_OK) {
        any_fail = true;
      }

      if (archive_entry_filetype(ae) == AE_IFDIR)
        archive_read_disk_descend(disk);
    }

    archive_read_close(disk);
    archive_read_free(disk);
  }

  archive_write_close(a);
  archive_write_free(a);

  if (any_fail && files.size() == 1 && error_out->empty()) {
    if (error_out) *error_out = "some entries failed";
  }
  return !any_fail || !files.empty();
}

bool create_archive(const std::string& archive_path,
                     const std::vector<std::string>& files,
                     std::string* error_out,
                     std::atomic<float>* progress,
                     size_t total_entries) {
  if (files.empty()) {
    if (error_out) *error_out = "no input files specified";
    return false;
  }

  if (total_entries == 0) total_entries = files.size();

  struct archive* a = archive_write_new();
  if (!set_write_format_by_ext(a, archive_path)) {
    if (error_out) *error_out = last_error();
    archive_write_free(a);
    return false;
  }

  int r = archive_write_open_filename(a, archive_path.c_str());
  if (r != ARCHIVE_OK) {
    if (error_out) *error_out = archive_error_string(a);
    archive_write_free(a);
    return false;
  }

  bool any_fail = false;
  size_t processed = 0;

  for (const auto& file : files) {
    struct archive* disk = archive_read_disk_new();
    archive_read_disk_set_standard_lookup(disk);

    r = archive_read_disk_open(disk, file.c_str());
    if (r != ARCHIVE_OK) {
      if (error_out && !any_fail) *error_out = archive_error_string(disk);
      any_fail = true;
      archive_read_close(disk);
      archive_read_free(disk);
      ++processed;
      if (progress) *progress = static_cast<float>(processed) / total_entries;
      continue;
    }

    char real_buf[PATH_MAX];
    std::string base;
    if (::realpath(file.c_str(), real_buf)) {
      base = real_buf;
      if (base.back() != '/') base += '/';
    }

    struct archive_entry* ae = nullptr;
    while ((r = archive_read_next_header(disk, &ae)) == ARCHIVE_OK) {
      std::string entry_path = archive_entry_pathname(ae);
      if (!base.empty() && entry_path.compare(0, base.size(), base) == 0) {
        entry_path = entry_path.substr(base.size());
      }
      if (entry_path.empty()) {
        if (archive_entry_filetype(ae) == AE_IFDIR)
          archive_read_disk_descend(disk);
        continue;
      }

      if (archive_entry_filetype(ae) == AE_IFLNK) {
        const char* link = archive_entry_symlink(ae);
        if (link && link[0] == '/') {
          archive_read_data_skip(disk);
          if (archive_entry_filetype(ae) == AE_IFDIR)
            archive_read_disk_descend(disk);
          continue;
        }
      }

      archive_entry_set_pathname(ae, entry_path.c_str());
      archive_entry_set_size(ae, archive_entry_size(ae));

      r = archive_write_header(a, ae);
      if (r != ARCHIVE_OK) {
        if (error_out && !any_fail) *error_out = archive_error_string(a);
        any_fail = true;
        archive_read_data_skip(disk);
        if (archive_entry_filetype(ae) == AE_IFDIR)
          archive_read_disk_descend(disk);
        continue;
      }

      if (archive_entry_size(ae) > 0) {
        const char* src_path = archive_entry_sourcepath(ae);
        if (!src_path) {
          any_fail = true;
          archive_write_finish_entry(a);
          archive_read_data_skip(disk);
          continue;
        }
        int fd = ::open(src_path, O_RDONLY);
        if (fd < 0) {
          any_fail = true;
          archive_write_finish_entry(a);
          archive_read_data_skip(disk);
          continue;
        }
        char buf[65536];
        ssize_t nread;
        while ((nread = ::read(fd, buf, sizeof(buf))) > 0) {
          ssize_t written = archive_write_data(a, buf, static_cast<size_t>(nread));
          if (written < 0) {
            any_fail = true;
            break;
          }
        }
        ::close(fd);
      }

      r = archive_write_finish_entry(a);
      if (r != ARCHIVE_OK) any_fail = true;

      if (archive_entry_filetype(ae) == AE_IFDIR)
        archive_read_disk_descend(disk);
    }

    archive_read_close(disk);
    archive_read_free(disk);
    ++processed;
    if (progress) *progress = static_cast<float>(processed) / total_entries;
  }

  archive_write_close(a);
  archive_write_free(a);

  if (any_fail && error_out && error_out->empty())
    *error_out = "some entries failed";
  return !any_fail || !files.empty();
}

bool convert_archive(const std::string& src,
                      const std::string& dest,
                      std::string* error_out) {
  struct archive* a_in = archive_read_new();
  archive_read_support_format_all(a_in);
  archive_read_support_filter_all(a_in);

  int r = archive_read_open_filename(a_in, src.c_str(), 10240);
  if (r != ARCHIVE_OK) {
    if (error_out) *error_out = archive_error_string(a_in);
    archive_read_free(a_in);
    return false;
  }

  struct archive* a_out = archive_write_new();
  if (!set_write_format_by_ext(a_out, dest)) {
    if (error_out) *error_out = last_error();
    archive_read_close(a_in);
    archive_read_free(a_in);
    archive_write_free(a_out);
    return false;
  }

  r = archive_write_open_filename(a_out, dest.c_str());
  if (r != ARCHIVE_OK) {
    if (error_out) *error_out = archive_error_string(a_out);
    archive_read_close(a_in);
    archive_read_free(a_in);
    archive_write_free(a_out);
    return false;
  }

  bool any_fail = false;
  struct archive_entry* ae = nullptr;

  while ((r = archive_read_next_header(a_in, &ae)) == ARCHIVE_OK) {
    // Run safe_path on entry to catch traversal
    std::string clean = safe_path(archive_entry_pathname(ae));
    if (clean.empty()) {
      archive_read_data_skip(a_in);
      continue;
    }
    archive_entry_set_pathname(ae, clean.c_str());

    r = archive_write_header(a_out, ae);
    if (r != ARCHIVE_OK) {
      if (error_out && !any_fail) *error_out = archive_error_string(a_out);
      any_fail = true;
      archive_read_data_skip(a_in);
      continue;
    }

    if (archive_entry_size(ae) > 0) {
      char buf[65536];
      ssize_t len;
      while ((len = archive_read_data(a_in, buf, sizeof(buf))) > 0) {
        ssize_t written = archive_write_data(a_out, buf, static_cast<size_t>(len));
        if (written < 0) {
          any_fail = true;
          break;
        }
      }
    }

    r = archive_write_finish_entry(a_out);
    if (r != ARCHIVE_OK) {
      any_fail = true;
    }
  }

  if (r != ARCHIVE_EOF) {
    if (error_out && !any_fail) *error_out = archive_error_string(a_in);
    any_fail = true;
  }

  archive_read_close(a_in);
  archive_read_free(a_in);
  archive_write_close(a_out);
  archive_write_free(a_out);

  return !any_fail;
}

std::vector<EntryHash> hash_entries(const std::string& archive_path,
                                     std::string* error_out) {
  std::vector<EntryHash> result;

  struct archive* a = archive_read_new();
  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);

  int r = archive_read_open_filename(a, archive_path.c_str(), 10240);
  if (r != ARCHIVE_OK) {
    if (error_out) *error_out = archive_error_string(a);
    archive_read_free(a);
    return result;
  }

  struct archive_entry* ae = nullptr;
  while ((r = archive_read_next_header(a, &ae)) == ARCHIVE_OK) {
    if (archive_entry_filetype(ae) == AE_IFDIR) {
      archive_read_data_skip(a);
      continue;
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

    char buf[65536];
    ssize_t len;
    while ((len = archive_read_data(a, buf, sizeof(buf))) > 0) {
      EVP_DigestUpdate(ctx, buf, static_cast<size_t>(len));
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    char hex[65] = {};
    for (unsigned int i = 0; i < hash_len; ++i)
      std::snprintf(hex + i * 2, 3, "%02x", hash[i]);

    result.push_back({archive_entry_pathname(ae), std::string(hex)});
  }

  if (r != ARCHIVE_EOF && error_out) {
    *error_out = archive_error_string(a);
  }

  archive_read_close(a);
  archive_read_free(a);

  return result;
}

std::string get_comment(const std::string&) {
  return {};
}

// ── Batch / parallel operations ──────────────────────────────────────

std::vector<BatchItem> batch_check(const std::vector<std::string>& paths,
                                    size_t jobs) {
  if (jobs == 0) jobs = std::thread::hardware_concurrency();
  ThreadPool pool(jobs);
  std::vector<std::future<BatchItem>> futures;

  for (const auto& p : paths) {
    futures.push_back(pool.enqueue([p]() -> BatchItem {
      std::string err;
      bool ok = check_integrity(p, &err);
      return {p, ok, ok ? "OK" : err};
    }));
  }

  std::vector<BatchItem> results;
  for (auto& f : futures) {
    results.push_back(f.get());
  }
  return results;
}

std::vector<BatchItem> batch_convert(const std::vector<std::string>& srcs,
                                      const std::string& dest_dir,
                                      const std::string& dest_ext,
                                      size_t jobs) {
  if (jobs == 0) jobs = std::thread::hardware_concurrency();
  ThreadPool pool(jobs);
  std::vector<std::future<BatchItem>> futures;

  for (const auto& src : srcs) {
    futures.push_back(pool.enqueue([src, dest_dir, dest_ext]() -> BatchItem {
      // Build dest path: dest_dir / basename_with_new_ext
      auto slash = src.rfind('/');
      std::string base = (slash == std::string::npos) ? src : src.substr(slash + 1);
      auto dot = base.rfind('.');
      if (dot != std::string::npos) base = base.substr(0, dot);
      std::string dest = dest_dir + "/" + base + dest_ext;

      std::string err;
      bool ok = convert_archive(src, dest, &err);
      return {src, ok, ok ? ("-> " + dest) : err};
    }));
  }

  std::vector<BatchItem> results;
  for (auto& f : futures) {
    results.push_back(f.get());
  }
  return results;
}

} // namespace archive_viewer
