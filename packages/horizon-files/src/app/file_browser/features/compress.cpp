#include "app/file_browser/features/compress.hpp"
#include "app/file_browser/app.hpp"
#include "app/file_browser/app_types.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>


namespace fs = std::filesystem;
using cmp_clock = std::chrono::steady_clock;

static std::uint64_t cmp_expiry_3s() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             (cmp_clock::now() + std::chrono::milliseconds(3000)).time_since_epoch())
      .count();
}

namespace eh::file_browser {

const CompressFormat kCompressFormats[kNumCompressFormats] = {
  {"Zip",     ".zip"},
  {"Tar.gz",  ".tar.gz"},
  {"Tar.bz2", ".tar.bz2"},
  {"Tar.xz",  ".tar.xz"},
  {"7z",      ".7z"},
  {"Rar",     ".rar"},
  {"Tar",     ".tar"},
};

static bool tool_available(const char* name);

static std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += '\'';
  return out;
}

static std::string archive_name_for(AppState& app) {
  std::string base = app.compress_name_buf.empty() ? app.compress_source_name : app.compress_name_buf;
  // Remove trailing extension if present
  auto dot = base.rfind('.');
  if (dot != std::string::npos) base = base.substr(0, dot);
  base += kCompressFormats[app.compress_format].extension;
  return fs::path(app.cur_tab().current_path) / base;
}

std::string format_compress_cmd(const std::vector<std::string>& source_paths,
                                 const std::string& archive_path,
                                 int format_idx, int level) {
  std::string cmd;
  switch (format_idx) {
    case 0: { // zip
      if (tool_available("zip")) {
        cmd = "zip -r -" + std::to_string(std::min(9, std::max(0, level)));
        for (const auto& p : source_paths) cmd += " " + shell_quote(p);
        cmd += " -- " + shell_quote(archive_path);
      } else {
        cmd = "7z a -tzip -mx=" + std::to_string(std::min(9, std::max(0, level)));
        cmd += " " + shell_quote(archive_path);
        for (const auto& p : source_paths) cmd += " " + shell_quote(p);
      }
      break;
    }
    case 1: // tar.gz
      cmd = "tar -czf " + shell_quote(archive_path);
      for (const auto& p : source_paths) cmd += " " + shell_quote(p);
      break;
    case 2: // tar.bz2
      cmd = "tar -cjf " + shell_quote(archive_path);
      for (const auto& p : source_paths) cmd += " " + shell_quote(p);
      break;
    case 3: // tar.xz
      cmd = "tar -cJf " + shell_quote(archive_path);
      for (const auto& p : source_paths) cmd += " " + shell_quote(p);
      break;
    case 4: // 7z
      cmd = "7z a -mx=" + std::to_string(std::min(9, std::max(0, level / 2)));
      cmd += " " + shell_quote(archive_path);
      for (const auto& p : source_paths) cmd += " " + shell_quote(p);
      break;
    case 5: // rar
      cmd = "rar a -m" + std::to_string(std::min(5, std::max(0, level / 2)));
      cmd += " " + shell_quote(archive_path);
      for (const auto& p : source_paths) cmd += " " + shell_quote(p);
      break;
    case 6: // tar (no compression)
      cmd = "tar -cf " + shell_quote(archive_path);
      for (const auto& p : source_paths) cmd += " " + shell_quote(p);
      break;
  }
  return cmd;
}

static bool tool_available(const char* name) {
  std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

void check_compress_tool_availability(AppState& app) {
  // format 0 (zip), 1 (tar.gz), 2 (tar.bz2), 3 (tar.xz)
  app.compress_format_available[0] = tool_available("zip") || tool_available("7z");
  app.compress_format_available[1] = tool_available("tar") && tool_available("gzip");
  app.compress_format_available[2] = tool_available("tar") && tool_available("bzip2");
  app.compress_format_available[3] = tool_available("tar") && tool_available("xz");
  app.compress_format_available[4] = tool_available("7z");
  app.compress_format_available[5] = tool_available("rar");
  app.compress_format_available[6] = tool_available("tar");

  // Auto-switch to first available format if current one is unavailable
  if (!app.compress_format_available[app.compress_format]) {
    for (int i = 0; i < 7; ++i) {
      if (app.compress_format_available[i]) {
        app.compress_format = i;
        break;
      }
    }
  }
}

void execute_compress_async(AppState& app) {
  std::string archive = archive_name_for(app);

  std::error_code ec;
  if (fs::exists(archive, ec)) fs::remove(archive, ec);

  std::string cmd = format_compress_cmd(app.compress_source_paths, archive,
                                         app.compress_format, app.compress_level);
  cmd += " 2>/dev/null";

  app.compress_dialog_open = false;
  app.operation_in_progress = true;
  app.operation_status = "Compressing...";
  draw(app);

  int ret = std::system(cmd.c_str());

  app.operation_in_progress = false;
  app.operation_status = (ret == 0) ? "Compression complete" : "Compression failed";
  app.operation_status_expires_ms = cmp_expiry_3s();
  reload_dir(app);
  draw(app);
}

// ── archive detection ────────────────────────────────────────────

static const char* kArchiveExts[] = {
  ".zip", ".tar.gz", ".tar.bz2", ".tar.xz", ".7z", ".rar", ".tar",
};

bool is_archive_extension(const std::string& path) {
  // Check double extensions first (.tar.gz, .tar.bz2, .tar.xz)
  std::string lower = path;
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const auto* ext : kArchiveExts) {
    if (lower.size() >= strlen(ext) &&
        lower.compare(lower.size() - strlen(ext), strlen(ext), ext) == 0)
      return true;
  }
  return false;
}

std::string default_extract_dir(const std::string& archive_path) {
  fs::path p(archive_path);
  std::string stem = p.stem().string();
  // Handle .tar.com extension: foo.tar.gz → stem is "foo.tar", we want "foo"
  std::string ext = p.extension().string();
  if (ext == ".gz" || ext == ".bz2" || ext == ".xz") {
    stem = fs::path(stem).stem().string();
  }
  return (p.parent_path() / stem).string();
}

static std::string format_extract_cmd_internal(const std::string& archive_path,
                                                 const std::string& dest_dir) {
  std::string lower = archive_path;
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  std::string qsrc = shell_quote(archive_path);
  std::string qdst = shell_quote(dest_dir);

  if (lower.ends_with(".zip"))
    return "unzip -o " + qsrc + " -d " + qdst;
  if (lower.ends_with(".tar.gz") || lower.ends_with(".tgz"))
    return "tar -xzf " + qsrc + " -C " + qdst;
  if (lower.ends_with(".tar.bz2"))
    return "tar -xjf " + qsrc + " -C " + qdst;
  if (lower.ends_with(".tar.xz"))
    return "tar -xJf " + qsrc + " -C " + qdst;
  if (lower.ends_with(".7z"))
    return "7z x " + qsrc + " -o" + qdst;
  if (lower.ends_with(".rar"))
    return "unrar x -o+ " + qsrc + " " + qdst;
  if (lower.ends_with(".tar"))
    return "tar -xf " + qsrc + " -C " + qdst;

  return {};
}

std::string format_extract_cmd(const std::string& archive_path,
                                const std::string& dest_dir) {
  return format_extract_cmd_internal(archive_path, dest_dir);
}

void execute_extract_async(AppState& app, const std::string& archive_path,
                            const std::string& dest_dir) {
  std::error_code ec;
  fs::create_directories(dest_dir, ec);

  std::string cmd = format_extract_cmd_internal(archive_path, dest_dir);
  if (cmd.empty()) {
    app.operation_status = "Unsupported archive format";
    app.operation_status_expires_ms = cmp_expiry_3s();
    draw(app);
    return;
  }
  cmd += " 2>/dev/null";

  app.operation_in_progress = true;
  app.operation_status = "Extracting...";
  draw(app);

  int ret = std::system(cmd.c_str());

  app.operation_in_progress = false;
  app.operation_status = (ret == 0) ? "Extraction complete" : "Extraction failed";
  app.operation_status_expires_ms = cmp_expiry_3s();
  reload_dir(app);
  draw(app);
}

} // namespace eh::file_browser
