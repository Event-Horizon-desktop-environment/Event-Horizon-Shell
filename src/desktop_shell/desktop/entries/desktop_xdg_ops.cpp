#define _GNU_SOURCE 1
#include "desktop_shell/desktop/entries/desktop_xdg_ops.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include <cstdio>
#include <iostream>

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

extern "C" char** environ;

namespace eh::shell::desktop::xdg {

namespace {

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

void spawn_sh_lc_detached_impl(const std::string& script) {
   
  const std::string cmd = script + " &";
  (void)std::system(cmd.c_str());
}

bool sync_shell_ok(const std::string& script) {
   
  const std::string full = script + " >/dev/null 2>&1";
  return std::system(full.c_str()) == 0;
}

std::string dbus_escape_double_quotes(std::string_view s) {
   
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}

static int hex_digit(char c) {
   
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// Decode `%HH` in a path segment (used after stripping `file://`).
static std::string uri_percent_decode_path(std::string_view in) {
   
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      const int hi = hex_digit(static_cast<char>(in[i + 1]));
      const int lo = hex_digit(static_cast<char>(in[i + 2]));
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(in[i]);
  }
  return out;
}

// After `file://`, yields absolute path for typical `file:///foo/bar`.
std::string path_from_file_uri_loose(const std::string& uri) {
   
  if (uri.rfind("file://", 0) != 0) return {};
  std::string p = uri.substr(7);
  if (p.empty()) return {};
  if (p[0] != '/') {
    const auto slash = p.find('/');
    if (slash != std::string::npos) p = p.substr(slash);
    else return {};
  }
  return uri_percent_decode_path(p);
}

// One clipboard offer only: each `wl-copy` replaces the previous Wayland selection entirely.
void pipe_bytes_to_clipboard(const std::string& bytes, const char* wl_copy_type_arg) {
   
  int pipefd[2];
  if (pipe(pipefd) != 0) return;

  std::string inner;
  if (wl_copy_type_arg) {
    inner = std::string("wl-copy --type ") + sh_single_quote(std::string_view(wl_copy_type_arg)) +
            " 2>/dev/null || exec xclip -selection clipboard -t " + sh_single_quote(std::string_view(wl_copy_type_arg));
  } else {
    inner = "wl-copy 2>/dev/null || exec xclip -selection clipboard";
  }
  std::vector<char> arg_lc(inner.begin(), inner.end());
  arg_lc.push_back('\0');

  posix_spawn_file_actions_t fa{};
  if (posix_spawn_file_actions_init(&fa) != 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return;
  }
  if (posix_spawn_file_actions_adddup2(&fa, pipefd[0], STDIN_FILENO) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[0]);
    close(pipefd[1]);
    return;
  }
  if (pipefd[0] != STDIN_FILENO && posix_spawn_file_actions_addclose(&fa, pipefd[0]) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[0]);
    close(pipefd[1]);
    return;
  }
  if (posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_RDWR, 0) != 0 ||
      posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_RDWR, 0) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[0]);
    close(pipefd[1]);
    return;
  }

  char argv0[] = "/bin/sh";
  char argv1[] = "sh";
  char argv2[] = "-c";
  char* argv[] = {argv0, argv1, argv2, arg_lc.data(), nullptr};

  pid_t pid = -1;
  const int spawn_err = posix_spawnp(&pid, "/bin/sh", &fa, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&fa);
  close(pipefd[0]);
  if (spawn_err != 0 || pid < 0) {
    close(pipefd[1]);
    return;
  }
  (void)write(pipefd[1], bytes.data(), bytes.size());
  close(pipefd[1]);
  int st = 0;
  (void)waitpid(pid, &st, 0);
}

}  // namespace

std::string canonical_absolute_path(const std::string& path) {
   
  std::error_code ec;
  fs::path p(path);
  fs::path c = fs::weakly_canonical(p, ec);
  if (!ec && !c.empty()) return c.string();
  return fs::absolute(p, ec).string();
}

namespace {

std::string shell_clip_out(const std::string& cmd) {
   
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return {};
  std::string out;
  char buf[8192];
  while (size_t n = fread(buf, 1, sizeof(buf), f)) out.append(buf, n);
  int st = pclose(f);
  if (st != 0) std::cerr << "[xdg-ops] pclose status=" << st << '\n';
  return out;
}

std::string trim_ws(std::string_view s) {
   
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
  return std::string(s.substr(a, b - a));
}

std::string try_read_clipboard_mime(const std::string& mime) {
   
  const std::string q = sh_single_quote(mime);
  std::string s = shell_clip_out("command -v wl-paste >/dev/null 2>&1 && wl-paste --type " + q + " 2>/dev/null");
  if (!s.empty()) return s;
  return shell_clip_out("command -v xclip >/dev/null 2>&1 && xclip -selection clipboard -t " + mime + " -o 2>/dev/null");
}

std::string try_read_plain_clipboard() {
   
  std::string s = shell_clip_out("command -v wl-paste >/dev/null 2>&1 && wl-paste -n 2>/dev/null");
  if (!s.empty()) return s;
  return shell_clip_out("command -v xclip >/dev/null 2>&1 && xclip -selection clipboard -o 2>/dev/null");
}

// Default clipboard offer (negotiated type); catches apps that omit explicit MIME on read.
std::string try_read_clipboard_primary_offer() {
   
  std::string s = shell_clip_out("command -v wl-paste >/dev/null 2>&1 && wl-paste 2>/dev/null");
  if (!s.empty()) return s;
  return shell_clip_out("command -v xclip >/dev/null 2>&1 && xclip -selection clipboard -o 2>/dev/null");
}

bool parse_nautilus_clipboard_body(const std::string& raw, bool* out_cut, std::vector<std::string>* out_paths) {
   
  out_paths->clear();
  std::istringstream iss(raw);
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  if (lines.empty()) return false;
  size_t i = 0;
  if (lines[0] == "x-special/nautilus-clipboard") i = 1;
  if (i >= lines.size()) return false;
  if (lines[i] == "cut" || lines[i] == "copy") {
    *out_cut = (lines[i] == "cut");
    i++;
  } else
    return false;
  for (; i < lines.size(); ++i) {
    if (lines[i].empty()) continue;
    std::string p = path_from_file_uri_loose(lines[i]);
    if (p.empty() && !lines[i].empty() && lines[i][0] == '/') p = lines[i];
    if (!p.empty()) out_paths->push_back(canonical_absolute_path(p));
  }
  return !out_paths->empty();
}

void append_paths_from_uri_list(const std::string& raw, std::vector<std::string>* out_paths) {
   
  std::istringstream iss(raw);
  std::string line;
  while (std::getline(iss, line)) {
    line = trim_ws(line);
    if (line.empty()) continue;
    std::string p = path_from_file_uri_loose(line);
    if (p.empty() && !line.empty() && line[0] == '/') p = line;
    if (!p.empty()) out_paths->push_back(canonical_absolute_path(p));
  }
}

void append_paths_from_plain_lines(const std::string& raw, std::vector<std::string>* out_paths) {
   
  std::istringstream iss(raw);
  std::string line;
  while (std::getline(iss, line)) {
    line = trim_ws(line);
    if (line.empty()) continue;
    std::string p = path_from_file_uri_loose(line);
    if (p.empty() && !line.empty() && line[0] == '/') p = line;
    if (!p.empty()) out_paths->push_back(canonical_absolute_path(p));
  }
}

fs::path unique_dest_in_dir(const fs::path& dir, fs::path filename) {
   
  fs::path base = dir / filename;
  std::error_code ec;
  if (!fs::exists(base, ec)) return base;
  const std::string stem = filename.stem().string();
  const std::string ext = filename.extension().string();
  for (int n = 2; n < 10000; ++n) {
    fs::path cand = dir / (stem + " (" + std::to_string(n) + ")" + ext);
    if (!fs::exists(cand, ec)) return cand;
  }
  return base;
}

}  // namespace

std::string file_uri_for_path(const std::string& abs_path) {
   
  std::string out = "file://";
  for (unsigned char c : abs_path) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' ||
        c == '.' || c == '~')
      out += static_cast<char>(c);
    else if (c == ' ')
      out += "%20";
    else {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string expand_desktop_exec_tokens(std::string exec, const std::string& desktop_abs_path, const std::string& display_name) {
   
  const std::string canon_desktop = canonical_absolute_path(desktop_abs_path);
  const std::string uri = file_uri_for_path(canon_desktop);
  std::string out;
  out.reserve(exec.size() + 64);
  for (size_t i = 0; i < exec.size();) {
    if (exec[i] == '%' && i + 1 < exec.size()) {
      const char k = exec[i + 1];
      if (k == '%') {
        out += '%';
        i += 2;
        continue;
      }
      if (k == 'f' || k == 'F') {
        out += canon_desktop;
        i += 2;
        continue;
      }
      if (k == 'u' || k == 'U') {
        out += uri;
        i += 2;
        continue;
      }
      if (k == 'k') {
        out += uri;
        i += 2;
        continue;
      }
      if (k == 'c') {
        out += display_name;
        i += 2;
        continue;
      }
      if (k == 'i' || k == 'v' || k == 'm') {
        i += 2;
        continue;
      }
    }
    out += exec[i++];
  }
  return out;
}

void spawn_sh_lc_detached(const std::string& script) {   spawn_sh_lc_detached_impl(script); }

void open_path_in_default_application(const std::string& abs_path) {
   
  const std::string q = sh_single_quote(canonical_absolute_path(abs_path));
  spawn_sh_lc_detached("command -v xdg-open >/dev/null 2>&1 && xdg-open " + q);
}

void open_uri(const std::string& uri) {
   
  const std::string q = sh_single_quote(uri);
  spawn_sh_lc_detached("command -v gio >/dev/null 2>&1 && gio open " + q
                       + " || (command -v xdg-open >/dev/null 2>&1 && xdg-open " + q + ")");
}

bool dbus_filemanager_show_items_select_uri(const std::string& file_uri) {
   
  const std::string esc = dbus_escape_double_quotes(file_uri);
  std::string cmd = "dbus-send --session --type=method_call --dest=org.freedesktop.FileManager1 "
                    "/org/freedesktop/FileManager1 org.freedesktop.FileManager1.ShowItems "
                    "array:string:\"" +
                    esc + "\" string:\"\"";
  if (sync_shell_ok(cmd)) return true;

  const std::string arr = "['" + file_uri + "']";
  cmd = "gdbus call --session -d org.freedesktop.FileManager1 -o /org/freedesktop/FileManager1 "
        "-m org.freedesktop.FileManager1.ShowItems " +
        sh_single_quote(arr) + " ''";
  return sync_shell_ok(cmd);
}

static bool dbus_show_properties_uri(const std::string& file_uri) {
   
  const std::string esc = dbus_escape_double_quotes(file_uri);
  std::string cmd = "dbus-send --session --type=method_call --dest=org.freedesktop.FileManager1 "
                    "/org/freedesktop/FileManager1 org.freedesktop.FileManager1.ShowItemProperties "
                    "array:string:\"" +
                    esc + "\" string:\"\"";
  if (sync_shell_ok(cmd)) return true;
  const std::string arr = "['" + file_uri + "']";
  cmd = "gdbus call --session -d org.freedesktop.FileManager1 -o /org/freedesktop/FileManager1 "
        "-m org.freedesktop.FileManager1.ShowItemProperties " +
        sh_single_quote(arr) + " ''";
  return sync_shell_ok(cmd);
}

void open_properties_for_desktop_file(const std::string& desktop_abs_path) {
   
  const std::string abs = canonical_absolute_path(desktop_abs_path);
  const std::string uri = file_uri_for_path(abs);
  const std::string parent = fs::path(abs).parent_path().string();

  if (dbus_show_properties_uri(uri)) return;

  spawn_sh_lc_detached("(command -v dolphin >/dev/null 2>&1 && dolphin --select " + sh_single_quote(abs) + " &) || "
                       "(command -v nautilus >/dev/null 2>&1 && nautilus --select " + sh_single_quote(abs) + " &) || "
                       "(command -v thunar >/dev/null 2>&1 && thunar " + sh_single_quote(parent) + " &) || "
                       "xdg-open " + sh_single_quote(parent));
}

void open_file_location(const std::string& desktop_abs_path, const DesktopEntryInfo* entry) {
   
  std::string target_abs = canonical_absolute_path(desktop_abs_path);
  if (entry && entry->type == "link" && entry->url.rfind("file://", 0) == 0) {
    const std::string tgt = path_from_file_uri_loose(entry->url);
    if (!tgt.empty()) target_abs = canonical_absolute_path(tgt);
  }
  const std::string highlight_uri = file_uri_for_path(target_abs);
  if (dbus_filemanager_show_items_select_uri(highlight_uri)) return;

  fs::path tp(target_abs);
  const std::string folder = tp.has_parent_path() ? tp.parent_path().string() : target_abs;
  spawn_sh_lc_detached("(command -v gio >/dev/null 2>&1 && gio open " + sh_single_quote(file_uri_for_path(folder)) + ") || "
                       "xdg-open " + sh_single_quote(folder));
}

void clipboard_files_cut_copy(bool cut, const std::string& abs_path) {
   
  const std::string canon = canonical_absolute_path(abs_path);
  const std::string uri = file_uri_for_path(canon);
  const std::string gnome_clip = std::string("x-special/nautilus-clipboard\n") + (cut ? "cut\n" : "copy\n") + uri + "\n";
  pipe_bytes_to_clipboard(gnome_clip, "x-special/nautilus-clipboard");
}

void clipboard_files_cut_copy_multi(bool cut, const std::vector<std::string>& abs_paths) {
   
  if (abs_paths.empty()) return;
  std::string gnome = "x-special/nautilus-clipboard\n";
  gnome += cut ? "cut\n" : "copy\n";
  size_t added = 0;
  for (const std::string& p : abs_paths) {
    const std::string canon = canonical_absolute_path(p);
    if (canon.empty()) continue;
    gnome += file_uri_for_path(canon) + "\n";
    ++added;
  }
  if (added == 0) return;
  pipe_bytes_to_clipboard(gnome, "x-special/nautilus-clipboard");
}

bool clipboard_paste_into_directory(const std::string& dest_dir_abs, std::vector<std::string>* out_created_paths,
                                    bool* out_was_cut) {
   
  if (out_created_paths) out_created_paths->clear();
  if (out_was_cut) *out_was_cut = false;
  std::error_code ec;
  fs::path dest_path(dest_dir_abs);
  fs::create_directories(dest_path, ec);
  const std::string dest = canonical_absolute_path(dest_dir_abs);
  if (!fs::is_directory(fs::path(dest), ec)) return false;

  bool cut = false;
  std::vector<std::string> paths;

  const std::string nav = try_read_clipboard_mime("x-special/nautilus-clipboard");
  if (!parse_nautilus_clipboard_body(nav, &cut, &paths)) {
    paths.clear();
    append_paths_from_uri_list(try_read_clipboard_mime("text/uri-list"), &paths);
  }
  if (paths.empty()) append_paths_from_plain_lines(try_read_plain_clipboard(), &paths);
  if (paths.empty()) {
    const std::string any = try_read_clipboard_primary_offer();
    append_paths_from_uri_list(any, &paths);
    append_paths_from_plain_lines(any, &paths);
  }

  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

  if (out_was_cut) *out_was_cut = cut;

  std::vector<std::string> pasted_sources;
  bool any = false;
  const fs::path dest_p(dest);
  for (const std::string& src_raw : paths) {
    const fs::path sp(src_raw);
    std::error_code epath;
    if (!fs::exists(sp, epath)) continue;
    const std::string src_canon = canonical_absolute_path(src_raw);
    const fs::path src_p(src_canon);

    if (fs::is_directory(src_p)) {
      const fs::path target = unique_dest_in_dir(dest_p, src_p.filename());
      std::error_code ec2;
      fs::create_directories(target.parent_path(), ec2);
      fs::copy(src_p, target, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec2);
      if (!ec2) {
        any = true;
        pasted_sources.push_back(src_canon);
        if (out_created_paths && !cut)
          out_created_paths->push_back(canonical_absolute_path(target.string()));
      }
      continue;
    }
    if (!fs::is_regular_file(src_p, epath) && !fs::is_symlink(src_p, epath)) continue;

    const fs::path target = unique_dest_in_dir(dest_p, src_p.filename());
    if (canonical_absolute_path(target.string()) == src_canon) continue;

    std::error_code ec3;
    fs::copy_file(src_p, target, fs::copy_options::copy_symlinks, ec3);
    if (!ec3) {
      any = true;
      pasted_sources.push_back(src_canon);
      if (out_created_paths && !cut)
        out_created_paths->push_back(canonical_absolute_path(target.string()));
    }
  }

  if (cut && any) {
    for (const std::string& s : pasted_sources) {
      std::error_code ex;
      const fs::path pp(s);
      if (fs::exists(pp, ex)) (void)trash_file(s);
    }
  }
  return any;
}

bool trash_file(const std::string& abs_path) {
   
  const std::string q = sh_single_quote(canonical_absolute_path(abs_path));
  return sync_shell_ok("(command -v gio >/dev/null 2>&1 && gio trash " + q + ")") ||
         sync_shell_ok("(command -v trash-put >/dev/null 2>&1 && trash-put " + q + ")");
}

void launch_expanded_exec_line(const std::string& expanded_exec, bool wrap_terminal) {
   
  if (expanded_exec.empty()) return;
  std::string inner = expanded_exec;
  if (wrap_terminal)
    inner = std::string("x-terminal-emulator -e /bin/sh -c ") + sh_single_quote(expanded_exec);
  spawn_sh_lc_detached(inner);
}

void launch_pkexec_exec_raw(const std::string& exec_raw, const std::string& desktop_abs_path, const std::string& display_name,
                              bool terminal) {
   
  const std::string expanded = expand_desktop_exec_tokens(exec_raw, desktop_abs_path, display_name);
  if (expanded.empty()) return;
  std::string inner = expanded;
  if (terminal) inner = std::string("x-terminal-emulator -e /bin/sh -c ") + sh_single_quote(expanded);

  std::string env = "pkexec env ";
  for (const char* evn : {"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS", "PATH", "HOME", "USER"}) {
    if (const char* v = std::getenv(evn))
      if (*v) env += std::string(evn) + "=" + sh_single_quote(std::string(v)) + " ";
  }
  env += "/bin/sh -c " + sh_single_quote(inner);
  spawn_sh_lc_detached(env);
}

void open_desktop_default(const std::string& desktop_abs_path, const std::optional<DesktopEntryInfo>& entry,
                           const std::string& fallback_exec_line_from_icon_cache) {
   
  const fs::path p(desktop_abs_path);
  const std::string stem = p.stem().string();
  const std::string abs_q = sh_single_quote(canonical_absolute_path(desktop_abs_path));
  const std::string stem_q = sh_single_quote(stem);

  std::string exec_src = fallback_exec_line_from_icon_cache;
  bool terminal = false;
  std::string name;
  if (entry) {
    if (!entry->exec.empty()) exec_src = entry->exec;
    terminal = entry->terminal;
    name = entry->name;
  }
  if (name.empty()) name = stem;
  const std::string exp = expand_desktop_exec_tokens(exec_src, desktop_abs_path, name);

  // Never use std::system() here: it keeps the launcher utility under this process as the supervising shell's

  std::string script =
      "(command -v gio >/dev/null 2>&1 && gio launch " + abs_q + ") || "
      "(command -v gtk-launch >/dev/null 2>&1 && gtk-launch " + stem_q + ")";
  if (!exp.empty()) {
    std::string inner = exp;
    if (terminal) inner = std::string("x-terminal-emulator -e /bin/sh -c ") + sh_single_quote(exp);
    script += " || /bin/sh -c " + sh_single_quote(inner);
  }
  spawn_sh_lc_detached(script);
}

void launch_action_exec(const std::string& action_exec_raw, const std::string& desktop_abs_path, const std::string& display_name,
                         bool terminal) {
   
  const std::string exp = expand_desktop_exec_tokens(action_exec_raw, desktop_abs_path, display_name);
  launch_expanded_exec_line(exp, terminal);
}

std::string prompt_rename_text(const std::string& title, const std::string& initial_value) {
   
  const std::string script =
      "(command -v zenity >/dev/null 2>&1 && zenity --entry --title " + sh_single_quote(title) + " --entry-text " +
      sh_single_quote(initial_value) + ") || "
      "(command -v kdialog >/dev/null 2>&1 && kdialog --title " + sh_single_quote(title) + " --inputbox \"Name:\" " +
      sh_single_quote(initial_value) + ") || "
      "(command -v yad >/dev/null 2>&1 && yad --entry --title " + sh_single_quote(title) + " --text=\"Name:\" --entry-text=" +
      sh_single_quote(initial_value) + ")";
  FILE* f = popen((script + " 2>/dev/null").c_str(), "r");
  if (!f) return {};
  std::string out;
  char buf[4096];
  while (fgets(buf, sizeof(buf), f)) out += buf;
  const int st = pclose(f);
  if (st != 0) return {};
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

}
