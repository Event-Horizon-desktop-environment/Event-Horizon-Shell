#include "desktop_shell/desktop/entries/desktop_archive.hpp"

#include "desktop_shell/desktop/entries/desktop_xdg_ops.hpp"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace eh::shell::desktop::archive {

namespace {

std::string lower_ext(const fs::path& p) {
   
  std::string e = p.extension().string();
  for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

std::string sh_single_quote(std::string_view s) {
   
  std::string o = "'";
  for (char c : s) {
    if (c == '\'')
      o += "'\\''";
    else
      o += c;
  }
  o += '\'';
  return o;
}

fs::path archive_folder_basename(const fs::path& filename) {
   
  fs::path x = filename;
  for (;;) {
    std::string e = lower_ext(x);
    if (e == ".zip" || e == ".7z" || e == ".rar" || e == ".cab") {
      fs::path nx = x.stem();
      if (nx == x || nx.empty()) break;
      x = nx;
      continue;
    }
    if (e == ".tar" || e == ".tgz" || e == ".tbz2" || e == ".tbz" || e == ".txz" || e == ".tzst" || e == ".tlz") {
      fs::path nx = x.stem();
      if (nx == x || nx.empty()) break;
      x = nx;
      continue;
    }
    if (e == ".gz" || e == ".bz2" || e == ".xz" || e == ".zst" || e == ".lzma") {
      fs::path nx = x.stem();
      if (nx == x || nx.empty()) break;
      x = nx;
      continue;
    }
    break;
  }
  if (x.empty() || x == "." || x == "..") return fs::path("archive");
  return x;
}

std::string timestamp_tag() {
   
  const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream o;
  o << std::put_time(&tm, "%Y%m%d-%H%M%S");
  return o.str();
}

fs::path unique_output_file(const fs::path& parent, std::string_view name_base, std::string_view ext_with_dot) {
   
  for (int n = 0;; ++n) {
    std::string fn = std::string(name_base) + (n > 0 ? (" (" + std::to_string(n) + ")") : "") + std::string(ext_with_dot);
    fs::path cand = parent / fn;
    if (!fs::exists(cand)) return cand;
  }
}

static std::string compress_extension(CompressFormat fmt) {
   
  switch (fmt) {
    case CompressFormat::Zip:
      return ".zip";
    case CompressFormat::TarGz:
      return ".tar.gz";
    case CompressFormat::Tar:
      return ".tar";
    case CompressFormat::SevenZ:
      return ".7z";
  }
  return ".zip";
}

}

bool path_looks_like_archive(const fs::path& p) {
   
  if (!p.has_filename()) return false;
  fs::path name = p.filename();
  std::string e = lower_ext(name);
  if (e == ".zip" || e == ".7z" || e == ".rar" || e == ".cab") return true;
  if (e == ".tar" || e == ".tgz" || e == ".tbz2" || e == ".tbz" || e == ".txz" || e == ".tlz" || e == ".tzst")
    return true;
  fs::path stem = name.stem();
  std::string e2 = lower_ext(stem);
  if (e2 == ".tar" && (e == ".gz" || e == ".bz2" || e == ".xz" || e == ".zst" || e == ".lzma")) return true;
  return false;
}

std::vector<std::string> filter_archive_paths(const std::vector<std::string>& abs_paths) {
   
  std::vector<std::string> out;
  for (const std::string& p : abs_paths) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::path(p), ec)) continue;
    if (path_looks_like_archive(fs::path(p))) out.push_back(p);
  }
  return out;
}

void extract_archives_detached(const std::vector<std::string>& archive_abs_paths) {
   
  if (archive_abs_paths.empty()) return;
  std::string script;
  for (const std::string& ap : archive_abs_paths) {
    std::error_code ec;
    const fs::path p(ap);
    if (!fs::is_regular_file(p, ec)) continue;
    const fs::path parent = p.parent_path();
    const fs::path folder_base = archive_folder_basename(p.filename());
    fs::path dest = unique_output_file(parent, folder_base.string(), "");
    const std::string qarch = sh_single_quote(ap);
    const std::string qdest = sh_single_quote(dest.string());
    const std::string el = lower_ext(p);
    const fs::path stem_file = p.stem();
    const std::string el2 = lower_ext(stem_file);
    const bool tar_family = el == ".tar" || el == ".tgz" || el == ".tbz2" || el == ".tbz" || el == ".txz" ||
                            el == ".tzst" ||
                            (el2 == ".tar" && (el == ".gz" || el == ".bz2" || el == ".xz" || el == ".zst" || el == ".lzma"));

    script += "DEST=" + qdest + "\n";
    script += "mkdir -p \"$DEST\" || true\n";
    script += "if command -v 7zz >/dev/null 2>&1 && 7zz x -y " + qarch + " -o\"$DEST/\"; then :\n";
    script += "elif command -v 7z >/dev/null 2>&1 && 7z x -y " + qarch + " -o\"$DEST/\"; then :\n";
    if (el == ".zip")
      script += "elif command -v unzip >/dev/null 2>&1 && unzip -q -o " + qarch + " -d \"$DEST\"; then :\n";
    if (tar_family)
      script += "elif command -v tar >/dev/null 2>&1 && tar -xf " + qarch + " -C \"$DEST\"; then :\n";
    script += "else echo \"[desktop] extract failed (install p7zip/unzip/tar): " + ap + "\" >&2\n";
    script += "fi\n";
  }
  xdg::spawn_sh_lc_detached(script);
}

void compress_paths_detached(CompressFormat fmt, const std::vector<std::string>& abs_paths,
                             const std::string& dest_parent_dir) {
   
  if (abs_paths.empty()) return;
  std::error_code ec;
  fs::path parent(dest_parent_dir);
  if (!fs::is_directory(parent, ec)) return;

  const std::string ext = compress_extension(fmt);
  fs::path out = unique_output_file(parent, "Selection-" + timestamp_tag(), ext);
  const std::string qout = sh_single_quote(out.string());

  std::string args;
  for (const std::string& p : abs_paths) {
    std::error_code e2;
    if (!fs::exists(fs::path(p), e2)) continue;
    args += " " + sh_single_quote(p);
  }
  if (args.empty()) return;

  std::string script;
  switch (fmt) {
    case CompressFormat::Zip:
      script += "if command -v zip >/dev/null 2>&1; then zip -q -r " + qout + args + "; else echo '[desktop] compress: zip not found' >&2; exit 1; fi\n";
      break;
    case CompressFormat::TarGz:
      script += "if command -v tar >/dev/null 2>&1; then tar -czf " + qout + args + "; else echo '[desktop] compress: tar not found' >&2; exit 1; fi\n";
      break;
    case CompressFormat::Tar:
      script += "if command -v tar >/dev/null 2>&1; then tar -cf " + qout + args + "; else echo '[desktop] compress: tar not found' >&2; exit 1; fi\n";
      break;
    case CompressFormat::SevenZ:
      script += "if command -v 7zz >/dev/null 2>&1; then 7zz a -t7z " + qout + args +
                "; elif command -v 7z >/dev/null 2>&1; then 7z a -t7z " + qout + args +
                "; else echo '[desktop] compress: 7z/7zz not found' >&2; exit 1; fi\n";
      break;
  }

  xdg::spawn_sh_lc_detached(script);
}

}
