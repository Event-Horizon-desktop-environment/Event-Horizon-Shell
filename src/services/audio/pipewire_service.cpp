#include "services/audio/pipewire_service.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <pipewire/extensions/session-manager.h>
#include <spa/utils/hook.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/utils/defs.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <vector>
#include <execinfo.h>

#include "desktop_shell/common/bench/debug_profile.hpp"

extern "C" char** environ;

namespace eh::audio {
namespace {

static const char* eh_pw_metadata_exe() {
   
  if (access("/usr/bin/pw-metadata", X_OK) == 0) return "/usr/bin/pw-metadata";
  if (access("/bin/pw-metadata", X_OK) == 0) return "/bin/pw-metadata";
  return "pw-metadata";
}

static const char* eh_pactl_exe() {
   
  if (access("/usr/bin/pactl", X_OK) == 0) return "/usr/bin/pactl";
  if (access("/bin/pactl", X_OK) == 0) return "/bin/pactl";
  return "pactl";
}

static bool str_eq(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

struct NodeListenerData {
  PipeWireService* self = nullptr;
  std::uint32_t node_id = 0;
};

static std::string prop_str(const spa_dict* props, const char* key) {
   
  if (!props || !key) return {};
  const char* v = spa_dict_lookup(props, key);
  return v ? std::string(v) : std::string{};
}

static int clamp_pct(int v) { return std::clamp(v, 0, 150); }

static int pct_from_norm(double t) { return clamp_pct(static_cast<int>(std::lround(std::clamp(t, 0.0, 1.5) * 100.0))); }

static bool spawn_capture_stdout(const std::vector<const char*>& argv_no_null, std::string* out) {
   
  int fd[2]{};
  if (pipe(fd) < 0) return false;

  posix_spawn_file_actions_t fa{};
  if (posix_spawn_file_actions_init(&fa) != 0) {
    close(fd[0]);
    close(fd[1]);
    return false;
  }
  bool fa_ok = true;
  fa_ok = fa_ok && (posix_spawn_file_actions_addclose(&fa, fd[0]) == 0);
  fa_ok = fa_ok && (posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO) == 0);
  if (fa_ok && fd[1] != STDOUT_FILENO) fa_ok = (posix_spawn_file_actions_addclose(&fa, fd[1]) == 0);
  fa_ok = fa_ok && (posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0) == 0);
  fa_ok = fa_ok && (posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0) == 0);
  if (!fa_ok) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return false;
  }

  std::vector<char*> av;
  av.reserve(argv_no_null.size() + 1);
  for (const char* a : argv_no_null) av.push_back(const_cast<char*>(a));
  av.push_back(nullptr);

  pid_t pid = -1;
  const int spawn_err = posix_spawnp(&pid, argv_no_null[0], &fa, nullptr, av.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  close(fd[1]);
  if (spawn_err != 0 || pid <= 0) {
    close(fd[0]);
    return false;
  }

  std::array<char, 8192> buf{};
  out->clear();
  for (;;) {
    pollfd pfd{fd[0], POLLIN, 0};
    const int pr = poll(&pfd, 1, 10000);
    if (pr <= 0) break;
    const ssize_t n = read(fd[0], buf.data(), buf.size());
    if (n <= 0) break;
    out->append(buf.data(), static_cast<size_t>(n));
  }
  close(fd[0]);
  int st = 0;
  (void)waitpid(pid, &st, 0);
  return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static int spawn_exec_wait(const std::vector<const char*>& argv_no_null) {
   
  posix_spawn_file_actions_t fa{};
  if (posix_spawn_file_actions_init(&fa) != 0) return -1;
  if (posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0) != 0 ||
      posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    return -1;
  }
  std::vector<char*> av;
  av.reserve(argv_no_null.size() + 1);
  for (const char* a : argv_no_null) av.push_back(const_cast<char*>(a));
  av.push_back(nullptr);
  pid_t pid = -1;
  const int spawn_err = posix_spawnp(&pid, argv_no_null[0], &fa, nullptr, av.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  if (spawn_err != 0 || pid < 0) return -1;
  int st = 0;
  (void)waitpid(pid, &st, 0);
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static bool spawn_exec_wait_owned(std::vector<std::string> argv_storage) {
   
  posix_spawn_file_actions_t fa{};
  if (posix_spawn_file_actions_init(&fa) != 0) return false;
  if (posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0) != 0 ||
      posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    return false;
  }
  std::vector<char*> av;
  av.reserve(argv_storage.size() + 1);
  for (auto& s : argv_storage) av.push_back(s.data());
  av.push_back(nullptr);
  pid_t pid = -1;
  const int spawn_err = posix_spawnp(&pid, argv_storage[0].c_str(), &fa, nullptr, av.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  if (spawn_err != 0 || pid < 0) return false;
  int st = 0;
  (void)waitpid(pid, &st, 0);
  return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static bool pw_meta_take_quoted_value(const std::string& line, const char* key, std::string* out) {
   
  const std::string needle = std::string("key:'") + key + "'";
  if (line.find(needle) == std::string::npos) return false;
  constexpr const char* vt = "value:'";
  const auto pos = line.find(vt);
  if (pos == std::string::npos) return false;
  const size_t start = pos + std::strlen(vt);
  const auto end = line.find('\'', start);
  if (end == std::string::npos || end <= start) return false;
  *out = line.substr(start, end - start);
  return true;
}

static int pw_parse_positive_int_value(const std::string& s, int fallback) {
   
  char* endp = nullptr;
  const long v = std::strtol(s.c_str(), &endp, 10);
  if (endp == s.c_str() || v <= 0 || v > 2000000) return fallback;
  return static_cast<int>(v);
}

static int pw_parse_nonneg_int_value(const std::string& s, int fallback) {
   
  char* endp = nullptr;
  const long v = std::strtol(s.c_str(), &endp, 10);
  if (endp == s.c_str() || v < 0 || v > 2000000) return fallback;
  return static_cast<int>(v);
}

static std::vector<int> pw_parse_int_bracket_list(const std::string& s) {
   
  std::vector<int> out;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (!std::isdigit(c)) continue;
    char* endp = nullptr;
    const long v = std::strtol(s.c_str() + i, &endp, 10);
    if (v > 0 && v < 2000000) out.push_back(static_cast<int>(v));
    if (endp && endp > s.c_str() + i) i = static_cast<size_t>(endp - s.c_str()) - 1;
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

static std::string pw_format_allowed_rates_metadata(const std::vector<int>& rates) {
   
  std::string s = "[ ";
  for (size_t i = 0; i < rates.size(); ++i) {
    if (i) s += ' ';
    s += std::to_string(rates[i]);
  }
  s += " ]";
  return s;
}

static std::vector<int> pw_merge_unique_sorted(std::vector<int> v, int hz) {
   
  v.push_back(hz);
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}

static void trim_ascii_ws_inplace(std::string* s) {
   
  if (!s) return;
  while (!s->empty() && std::isspace(static_cast<unsigned char>((*s)[0]))) s->erase(0, 1);
  while (!s->empty() && std::isspace(static_cast<unsigned char>(s->back()))) s->pop_back();
}

static std::vector<std::string> split_pw_line_tabs(const std::string& line) {
   
  std::vector<std::string> out;
  std::string cur;
  for (unsigned char uc : line) {
    const char c = static_cast<char>(uc);
    if (c == '\t') {
      out.push_back(cur);
      cur.clear();
    } else
      cur += c;
  }
  out.push_back(cur);
  return out;
}

static std::string ascii_lower_copy(std::string s) {
   
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

static void parse_pactl_sample_spec_fields(const std::string& spec, std::string* enc_out, int* ch_out, int* rate_out) {
   
  std::istringstream iss(spec);
  std::string enc;
  std::string chTok;
  std::string hzTok;
  iss >> enc >> chTok >> hzTok;
  if (enc_out) *enc_out = ascii_lower_copy(std::move(enc));
  int ch = 2;
  if (chTok.size() > 2 && chTok.compare(chTok.size() - 2, 2, "ch") == 0)
    ch = pw_parse_positive_int_value(chTok.substr(0, chTok.size() - 2), 2);
  int rate = 48000;
  if (hzTok.size() > 2 && hzTok.compare(hzTok.size() - 2, 2, "Hz") == 0)
    rate = pw_parse_positive_int_value(hzTok.substr(0, hzTok.size() - 2), 48000);
  if (ch_out) *ch_out = std::clamp(ch, 1, 64);
  if (rate_out) *rate_out = std::clamp(rate, 8000, 384000);
}

static const char* compat_pcm_choice_encoding(int choice_0_3) {
   
  static const char* kEnc[] = {"s16le", "s24le", "s32le", "f32le"};
  if (choice_0_3 < 0 || choice_0_3 >= 4) return nullptr;
  return kEnc[choice_0_3];
}

static const char* compat_pcm_choice_wp_format(int choice_0_3) {
   
  static const char* kFmt[] = {"S16_LE", "S24_LE", "S32_LE", "F32_LE"};
  if (choice_0_3 < 0 || choice_0_3 >= 4) return nullptr;
  return kFmt[choice_0_3];
}

static int compat_pcm_wp_format_choice(const std::string& spa_fmt) {
   
  static const char* kFmt[] = {"S16_LE", "S24_LE", "S32_LE", "F32_LE"};
  for (int i = 0; i < 4; ++i) {
    if (spa_fmt == kFmt[i]) return i;
  }
  return -1;
}

static std::string eh_wp_pcm_conf_path() {
   
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  std::string base;
  if (xdg && xdg[0]) {
    base = xdg;
  } else if (home && home[0]) {
    base = std::string(home) + "/.config";
  }
  if (base.empty()) return {};
  return base + "/wireplumber/wireplumber.conf.d/51-eh-default-pcm.conf";
}

static bool write_eh_wp_pcm_conf(const std::string& node_name, const std::string& spa_format) {
   
  const std::string path = eh_wp_pcm_conf_path();
  if (path.empty() || node_name.empty() || spa_format.empty()) return false;
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  const std::string tmp = path + ".__ehtmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# Event Horizon: forced PCM format for the default ALSA sink.\n";
    out << "monitor.alsa.rules = [\n";
    out << "    {\n";
    out << "        matches = [\n";
    out << "            {\n";
    out << "                node.name = \"" << node_name << "\"\n";
    out << "            }\n";
    out << "        ]\n";
    out << "        actions = {\n";
    out << "            update-props = {\n";
    out << "                audio.format = \"" << spa_format << "\"\n";
    out << "            }\n";
    out << "        }\n";
    out << "    }\n";
    out << "]\n";
    if (!out.good()) {
      out.close();
      (void)std::filesystem::remove(tmp, ec);
      return false;
    }
    out.flush();
    if (!out.good()) {
      out.close();
      (void)std::filesystem::remove(tmp, ec);
      return false;
    }
    out.close();
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    (void)std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

static bool read_eh_wp_pcm_conf(std::string* node_name_out, std::string* spa_format_out) {
   
  const std::string path = eh_wp_pcm_conf_path();
  if (path.empty()) return false;
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  std::string node_name;
  std::string spa_format;
  const std::string name_key = "node.name";
  const std::string fmt_key = "audio.format";
  while (std::getline(in, line)) {
    const size_t ni = line.find(name_key);
    if (ni != std::string::npos) {
      const size_t start = line.find('"', ni + name_key.size());
      const size_t end = start == std::string::npos ? std::string::npos : line.find('"', start + 1);
      if (start != std::string::npos && end != std::string::npos)
        node_name = line.substr(start + 1, end - start - 1);
    }
    const size_t fi = line.find(fmt_key);
    if (fi != std::string::npos) {
      const size_t start = line.find('"', fi + fmt_key.size());
      const size_t end = start == std::string::npos ? std::string::npos : line.find('"', start + 1);
      if (start != std::string::npos && end != std::string::npos)
        spa_format = line.substr(start + 1, end - start - 1);
    }
  }
  if (node_name_out) *node_name_out = node_name;
  if (spa_format_out) *spa_format_out = spa_format;
  return !node_name.empty() && !spa_format.empty();
}

static std::string pw_first_segment(const std::string& port_id) {
   
  const size_t c = port_id.find(':');
  if (c == std::string::npos) return port_id;
  return port_id.substr(0, c);
}

static void pw_split_lines(const std::string& blob, std::vector<std::string>* lines) {
   
  lines->clear();
  std::istringstream iss(blob);
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines->push_back(std::move(line));
  }
}

static int port_channel_order(const std::string& full) {
   
  const size_t c = full.rfind(':');
  const std::string tail = (c == std::string::npos) ? full : full.substr(c + 1);
  if (tail.find("FL") != std::string::npos) return 0;
  if (tail.find("FR") != std::string::npos) return 1;
  if (tail.find("FC") != std::string::npos) return 2;
  if (tail.find("LFE") != std::string::npos) return 3;
  if (tail.find("SL") != std::string::npos) return 4;
  if (tail.find("SR") != std::string::npos) return 5;
  if (tail.find("MONO") != std::string::npos) return 10;
  if (tail.find("capture") != std::string::npos) return 20;
  if (tail.find("playback") != std::string::npos) return 21;
  return 50;
}

static void pw_sort_ports_audio(std::vector<std::string>* ports) {
   
  std::sort(ports->begin(), ports->end(), [](const std::string& a, const std::string& b) {
    const int oa = port_channel_order(a);
    const int ob = port_channel_order(b);
    if (oa != ob) return oa < ob;
    return a < b;
  });
}

static void pw_disconnect_playback_links_from_stream(const std::string& stream_node_name) {
   
  std::string blob;
  if (!spawn_capture_stdout({"pw-link", "-l"}, &blob)) return;
  const std::string pref = stream_node_name + ":";
  std::string pending;
  std::vector<std::string> lines;
  pw_split_lines(blob, &lines);
  for (const std::string& line : lines) {
    if (line.size() >= 6 && line.compare(0, 6, "  |-> ") == 0) {
      const std::string dest = line.substr(6);
      if (!pending.empty() && pending.compare(0, pref.size(), pref) == 0 && pending.find("output") != std::string::npos) {
        (void)spawn_exec_wait({"pw-link", "-d", pending.c_str(), dest.c_str()});
      }
      continue;
    }
    if (line.size() >= 6 && line.compare(0, 6, "  |<- ") == 0) {
      const std::string src = line.substr(6);
      if (!pending.empty() && pending.find("playback") != std::string::npos &&
          src.compare(0, pref.size(), pref) == 0 && src.find("output") != std::string::npos) {
        (void)spawn_exec_wait({"pw-link", "-d", src.c_str(), pending.c_str()});
      }
      continue;
    }
    if (!line.empty() && line[0] != ' ') pending = line;
  }
}

static void pw_disconnect_capture_links_for_stream(const std::string& stream_node_name) {
   
  std::string blob;
  if (!spawn_capture_stdout({"pw-link", "-l"}, &blob)) return;
  const std::string pref = stream_node_name + ":";
  std::string pending;
  std::vector<std::string> lines;
  pw_split_lines(blob, &lines);
  for (const std::string& line : lines) {
    if (line.size() >= 6 && line.compare(0, 6, "  |-> ") == 0) {
      const std::string dest = line.substr(6);
      if (!pending.empty() &&
          (pending.find("capture") != std::string::npos || pending.find("monitor") != std::string::npos) &&
          dest.compare(0, pref.size(), pref) == 0 && dest.find("input") != std::string::npos) {
        (void)spawn_exec_wait({"pw-link", "-d", pending.c_str(), dest.c_str()});
      }
      continue;
    }
    if (line.size() >= 6 && line.compare(0, 6, "  |<- ") == 0) {
      const std::string src = line.substr(6);
      if (!pending.empty() && pending.compare(0, pref.size(), pref) == 0 && pending.find("input") != std::string::npos &&
          (src.find("capture") != std::string::npos || src.find("monitor") != std::string::npos)) {
        (void)spawn_exec_wait({"pw-link", "-d", src.c_str(), pending.c_str()});
      }
      continue;
    }
    if (!line.empty() && line[0] != ' ') pending = line;
  }
}

static void pw_collect_ports_with_prefix(const std::string& blob, const std::string& prefix,
                                         const std::string& must_contain, std::vector<std::string>* out) {
   
  out->clear();
  std::vector<std::string> lines;
  pw_split_lines(blob, &lines);
  for (std::string& ln : lines) {
    if (ln.empty()) continue;
    if (ln.compare(0, prefix.size(), prefix) != 0) continue;
    if (!must_contain.empty() && ln.find(must_contain) == std::string::npos) continue;
    out->push_back(std::move(ln));
  }
  pw_sort_ports_audio(out);
}

static std::string pw_guess_playback_sink_name_from_blob(const std::string& blob, const std::string& stream_node_name) {
   
  const std::string pref = stream_node_name + ":";
  std::string pending;
  std::vector<std::string> lines;
  pw_split_lines(blob, &lines);
  for (const std::string& line : lines) {
    if (line.size() >= 6 && line.compare(0, 6, "  |-> ") == 0) {
      const std::string dest = line.substr(6);
      if (!pending.empty() && pending.compare(0, pref.size(), pref) == 0 && pending.find("output") != std::string::npos &&
          dest.find("playback") != std::string::npos)
        return pw_first_segment(dest);
      continue;
    }
    if (line.size() >= 6 && line.compare(0, 6, "  |<- ") == 0) {
      const std::string src = line.substr(6);
      if (!pending.empty() && pending.find("playback") != std::string::npos &&
          src.compare(0, pref.size(), pref) == 0 && src.find("output") != std::string::npos)
        return pw_first_segment(pending);
      continue;
    }
    if (!line.empty() && line[0] != ' ') pending = line;
  }
  return {};
}

static std::string pw_guess_capture_source_name_from_blob(const std::string& blob, const std::string& stream_node_name) {
   
  const std::string pref = stream_node_name + ":";
  std::string pending;
  std::vector<std::string> lines;
  pw_split_lines(blob, &lines);
  for (const std::string& line : lines) {
    if (line.size() >= 6 && line.compare(0, 6, "  |-> ") == 0) {
      const std::string dest = line.substr(6);
      if (!pending.empty() &&
          (pending.find("capture") != std::string::npos || pending.find("monitor") != std::string::npos) &&
          dest.compare(0, pref.size(), pref) == 0 && dest.find("input") != std::string::npos)
        return pw_first_segment(pending);
      continue;
    }
    if (line.size() >= 6 && line.compare(0, 6, "  |<- ") == 0) {
      const std::string src = line.substr(6);
      if (!pending.empty() && pending.compare(0, pref.size(), pref) == 0 && pending.find("input") != std::string::npos &&
          (src.find("capture") != std::string::npos || src.find("monitor") != std::string::npos))
        return pw_first_segment(src);
      continue;
    }
    if (!line.empty() && line[0] != ' ') pending = line;
  }
  return {};
}

static std::string pw_guess_playback_sink_name(const std::string& stream_node_name) {
   
  std::string blob;
  if (!spawn_capture_stdout({"pw-link", "-l"}, &blob)) return {};
  return pw_guess_playback_sink_name_from_blob(blob, stream_node_name);
}

static std::string pw_guess_capture_source_name(const std::string& stream_node_name) {
   
  std::string blob;
  if (!spawn_capture_stdout({"pw-link", "-l"}, &blob)) return {};
  return pw_guess_capture_source_name_from_blob(blob, stream_node_name);
}

static void pw_link_playback_stream_to_sink_by_name(const std::string& stream_node_name,
                                                    const std::string& sink_node_name) {
   
  if (stream_node_name.empty() || sink_node_name.empty()) return;
  pw_disconnect_playback_links_from_stream(stream_node_name);

  std::string oblob, iblob;
  if (!spawn_capture_stdout({"pw-link", "-o"}, &oblob)) return;
  if (!spawn_capture_stdout({"pw-link", "-i"}, &iblob)) return;

  const std::string spref = stream_node_name + ":";
  const std::string dpref = sink_node_name + ":";
  std::vector<std::string> outs;
  std::vector<std::string> ins;
  pw_collect_ports_with_prefix(oblob, spref, "output", &outs);
  pw_collect_ports_with_prefix(iblob, dpref, "playback", &ins);
  const size_t n = std::min(outs.size(), ins.size());
  for (size_t i = 0; i < n; ++i) {
    (void)spawn_exec_wait({"pw-link", "-w", outs[i].c_str(), ins[i].c_str()});
  }
}

static void pw_link_capture_stream_to_source_by_name(const std::string& stream_node_name,
                                                      const std::string& source_node_name) {
   
  if (stream_node_name.empty() || source_node_name.empty()) return;
  pw_disconnect_capture_links_for_stream(stream_node_name);

  std::string oblob, iblob;
  if (!spawn_capture_stdout({"pw-link", "-o"}, &oblob)) return;
  if (!spawn_capture_stdout({"pw-link", "-i"}, &iblob)) return;

  const std::string tpref = stream_node_name + ":";
  const std::string spref = source_node_name + ":";
  std::vector<std::string> outs;
  std::vector<std::string> ins;
  pw_collect_ports_with_prefix(oblob, spref, "capture", &outs);
  if (outs.empty())
    pw_collect_ports_with_prefix(oblob, spref, "monitor", &outs);
  pw_collect_ports_with_prefix(iblob, tpref, "input", &ins);
  const size_t n = std::min(outs.size(), ins.size());
  for (size_t i = 0; i < n; ++i) {
    (void)spawn_exec_wait({"pw-link", "-w", outs[i].c_str(), ins[i].c_str()});
  }
}

static int parse_props_volume_mute_channels(const spa_pod* pod, int* out_pct, bool* out_mute, int* out_channels) {
   
  if (!pod) return -1;
  if (SPA_POD_TYPE(pod) != SPA_TYPE_Object) return -1;
  const spa_pod_object* obj =   reinterpret_cast<const spa_pod_object*>(pod);

  int pct = (out_pct ? *out_pct : 0);
  bool mute = (out_mute ? *out_mute : false);
  int channels = (out_channels ? *out_channels : 2);

  const spa_pod_prop* prop = nullptr;
  SPA_POD_OBJECT_FOREACH(const_cast<spa_pod_object*>(obj), prop) {
    if (prop->key == SPA_PROP_mute) {
      bool m = false;
      if (spa_pod_get_bool(&prop->value, &m) == 0) mute = m;
    } else if (prop->key == SPA_PROP_channelVolumes) {
      const spa_pod_array* arr = reinterpret_cast<const spa_pod_array*>(&prop->value);
      if (SPA_POD_ARRAY_VALUE_TYPE(arr) != SPA_TYPE_Float) continue;
      const std::uint32_t count = SPA_POD_ARRAY_N_VALUES(arr);
      if (count == 0) continue;
      channels = static_cast<int>(count);
      const float* vols = static_cast<const float*>(SPA_POD_ARRAY_VALUES(arr));
      float maxv = 0.0f;
      for (std::uint32_t i = 0; i < count; ++i) maxv = std::max(maxv, vols[i]);
      pct = clamp_pct(static_cast<int>(std::lround(maxv * 100.0f)));
    }
  }

  if (out_pct) *out_pct = pct;
  if (out_mute) *out_mute = mute;
  if (out_channels) *out_channels = channels;
  return 0;
}

}

bool pw_link_list_capture_stdout(std::string* out_blob) {
    
  if (!out_blob) return false;
  static std::string cache;
  static std::chrono::steady_clock::time_point last;
  const auto now = std::chrono::steady_clock::now();
  const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - last);
  if (!cache.empty() && age.count() < 200) {
    *out_blob = cache;
    return true;
  }
  if (spawn_capture_stdout({"pw-link", "-l"}, out_blob)) {
    cache = *out_blob;
    last = now;
    return true;
  }
  return false;
}

std::string pw_link_list_playback_sink_for_stream(const std::string& link_blob, const std::string& stream_node_name) {
   
  if (link_blob.empty() || stream_node_name.empty()) return {};
  return pw_guess_playback_sink_name_from_blob(link_blob, stream_node_name);
}

std::string pw_link_list_capture_source_for_stream(const std::string& link_blob, const std::string& stream_node_name) {
   
  if (link_blob.empty() || stream_node_name.empty()) return {};
  return pw_guess_capture_source_name_from_blob(link_blob, stream_node_name);
}

PipeWireService& PipeWireService::instance() {
   
  static PipeWireService s{};
  return s;
}

PipeWireService::PipeWireService() = default;
PipeWireService::~PipeWireService() {
   
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  if (loop_ && started_) pw_thread_loop_stop(loop_);
  for (auto& [id, ns] : nodes_) {
    if (ns.proxy) pw_proxy_destroy(ns.proxy);
    delete ns.hook;
    delete static_cast<NodeListenerData*>(ns.listener_data);
  }
  nodes_.clear();
  if (metadata_)
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(metadata_));
  if (reg_)
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(reg_));
  delete reg_hook_;
  if (core_) pw_core_disconnect(core_);
  delete core_hook_;
  if (ctx_) pw_context_destroy(ctx_);
  if (loop_) pw_thread_loop_destroy(loop_);
  pw_deinit();
}

void PipeWireService::start() {

  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (started_) return;
  }
  if (eh::debug_profile::env_bool("EH_PW_TRACE")) {
    void* bt[10];
    const int n = backtrace(bt, 10);
    char** syms = backtrace_symbols(bt, n);
    for (int i = 1; i < n && i < 7; i++) fprintf(stderr, "[pw-trace] %d %s\n", i, syms[i] ? syms[i] : "?");
    free(syms);
    fflush(stderr);
  }
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  if (started_) return;
  started_ = true;
  init_locked();
}

bool PipeWireService::started() const {
   
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  return started_;
}

void PipeWireService::set_change_callback(ChangeCallback cb) {
   
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  on_change_ = std::move(cb);
}

Snapshot PipeWireService::snapshot() const {
   
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  return snap_;
}

void PipeWireService::emit_change_locked() {
   
  rebuild_snapshot_locked();
  if (on_change_) on_change_();
}

void PipeWireService::sync_and_wait_locked() {
   
  if (!core_ || !loop_) return;
  const int seq = pw_core_sync(core_, PW_ID_CORE, 0);
  (void)seq;

}

void PipeWireService::init_locked() {
   
  pw_init(nullptr, nullptr);
  loop_ = pw_thread_loop_new("eh-pw", nullptr);
  if (!loop_) return;
  ctx_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
  if (!ctx_) return;
  core_ = pw_context_connect(ctx_, nullptr, 0);
  if (!core_) return;
  reg_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
  if (!reg_) return;

  static pw_registry_events reg_events;
  static pw_core_events core_events;
  static pw_node_events node_events;
  static pw_metadata_events md_events;
  reg_hook_ = new spa_hook{};
  core_hook_ = new spa_hook{};

  std::memset(&reg_events, 0, sizeof(reg_events));
  std::memset(&core_events, 0, sizeof(core_events));
  std::memset(&node_events, 0, sizeof(node_events));
  std::memset(&md_events, 0, sizeof(md_events));

  reg_events.version = PW_VERSION_REGISTRY_EVENTS;
  reg_events.global = +[](void* data, std::uint32_t id, std::uint32_t  , const char* type, std::uint32_t  ,
                          const spa_dict* props) {
    auto& self = *static_cast<PipeWireService*>(data);
    std::lock_guard<std::recursive_mutex> lock(self.mtx_);

    if (!type) return;
    if (str_eq(type, PW_TYPE_INTERFACE_Client)) {
      ClientInfo ci;
      ci.app_name = prop_str(props, "application.name");
      ci.app_id = prop_str(props, "application.id");
      ci.process_binary = prop_str(props, "application.process.binary");
      ci.process_path = prop_str(props, "application.process.path");
      ci.icon_name = prop_str(props, "application.icon-name");
      if (ci.icon_name.empty()) ci.icon_name = prop_str(props, "application.icon");
      if (ci.icon_name.empty()) ci.icon_name = prop_str(props, "media.icon-name");
      if (ci.icon_name.empty()) ci.icon_name = prop_str(props, "window.icon-name");
      self.clients_[id] = std::move(ci);
      return;
    }
    if (str_eq(type, PW_TYPE_INTERFACE_Metadata)) {
      if (self.metadata_) return;
      auto* proxy =
          static_cast<pw_proxy*>(pw_registry_bind(self.reg_, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
      if (!proxy) return;
      self.metadata_ = reinterpret_cast<pw_metadata*>(proxy);
      auto* hook = new spa_hook{};
      pw_metadata_add_listener(self.metadata_, hook, &md_events, &self);
      self.emit_change_locked();
      return;
    }
    if (!str_eq(type, PW_TYPE_INTERFACE_Node)) return;

    NodeState ns{};
    ns.id = id;
    ns.name = prop_str(props, "node.name");
    ns.media_class = prop_str(props, "media.class");
    ns.description = prop_str(props, "node.description");
    if (ns.description.empty()) ns.description = prop_str(props, "node.nick");
    ns.nick = prop_str(props, "node.nick");
    ns.app_name = prop_str(props, "application.name");
    ns.process_binary = prop_str(props, "application.process.binary");
    ns.process_path = prop_str(props, "application.process.path");
    ns.app_id = prop_str(props, "application.id");
    if (ns.app_id.empty()) ns.app_id = ns.process_binary;
    ns.icon_name = prop_str(props, "application.icon-name");
    if (ns.icon_name.empty()) ns.icon_name = prop_str(props, "application.icon");
    if (ns.icon_name.empty()) ns.icon_name = prop_str(props, "media.icon-name");
    if (ns.icon_name.empty()) ns.icon_name = prop_str(props, "window.icon-name");

    {
      const char* cid_str = spa_dict_lookup(props, "client.id");
      if (cid_str) {
        char* end = nullptr;
        auto cid = static_cast<std::uint32_t>(std::strtoul(cid_str, &end, 10));
        if (end && *end == '\0') ns.client_id = cid;
      }
    }
    if (ns.client_id) {
      auto it = self.clients_.find(ns.client_id);
      if (it != self.clients_.end()) {
        auto& ci = it->second;
        if (ns.app_name.empty()) ns.app_name = ci.app_name;
        if (ns.app_id.empty()) ns.app_id = ci.app_id;
        if (ns.process_binary.empty()) ns.process_binary = ci.process_binary;
        if (ns.process_path.empty()) ns.process_path = ci.process_path;
        if (ns.icon_name.empty()) ns.icon_name = ci.icon_name;
      }
    }

    ns.proxy = static_cast<pw_proxy*>(pw_registry_bind(self.reg_, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
    if (ns.proxy) {
      ns.hook = new spa_hook{};
      ns.listener_data = new NodeListenerData{&self, id};
      pw_proxy_add_object_listener(ns.proxy, ns.hook, &node_events, ns.listener_data);

      pw_node_enum_params(reinterpret_cast<pw_node*>(ns.proxy), 0, SPA_PARAM_Props, 0, -1, nullptr);
    }
    self.nodes_[id] = std::move(ns);
    self.refresh_default_nodes_locked();
    self.emit_change_locked();
  };
  reg_events.global_remove = +[](void* data, std::uint32_t id) {
    auto& self = *static_cast<PipeWireService*>(data);
    std::lock_guard<std::recursive_mutex> lock(self.mtx_);
    {
      auto it = self.nodes_.find(id);
      if (it != self.nodes_.end()) {
        if (it->second.proxy) pw_proxy_destroy(it->second.proxy);
        if (it->second.hook) delete it->second.hook;
        if (it->second.listener_data) delete static_cast<NodeListenerData*>(it->second.listener_data);
        self.nodes_.erase(it);
      }
    }
    self.clients_.erase(id);
    if (self.default_sink_ == id) self.default_sink_ = 0;
    if (self.default_source_ == id) self.default_source_ = 0;
    self.emit_change_locked();
  };

  core_events.version = PW_VERSION_CORE_EVENTS;
  core_events.info = +[](void* data, const pw_core_info* info) {
    auto& self = *static_cast<PipeWireService*>(data);
    std::lock_guard<std::recursive_mutex> lock(self.mtx_);

    if (info && info->props) {
      const char* ds = spa_dict_lookup(info->props, "default.audio.sink");
      const char* di = spa_dict_lookup(info->props, "default.audio.source");

      if (ds) {
        for (auto& [id, n] : self.nodes_) {
          if (n.name == ds) self.default_sink_ = id;
        }
      }
      if (di) {
        for (auto& [id, n] : self.nodes_) {
          if (n.name == di) self.default_source_ = id;
        }
      }
      self.emit_change_locked();
    }
  };

  node_events.version = PW_VERSION_NODE_EVENTS;
  node_events.param = +[](void* data, int  , std::uint32_t  , std::uint32_t  , std::uint32_t  ,
                          const spa_pod* param) {
    if (!data || !param) return;
    auto* nd = static_cast<NodeListenerData*>(data);
    if (!nd->self) return;
    std::lock_guard<std::recursive_mutex> lock(nd->self->mtx_);
    auto it = nd->self->nodes_.find(nd->node_id);
    if (it == nd->self->nodes_.end()) return;
    int pct = it->second.volume_pct;
    bool mute = it->second.muted;
    int ch = it->second.channels;
    if (parse_props_volume_mute_channels(param, &pct, &mute, &ch) == 0) {
      it->second.volume_pct = pct;
      it->second.muted = mute;
      it->second.channels = std::max(1, ch);
      nd->self->emit_change_locked();
    }
  };

  md_events.version = PW_VERSION_METADATA_EVENTS;
  md_events.property = +[](void* data, std::uint32_t  , const char* key, const char*  , const char* value) -> int {
    auto& self = *static_cast<PipeWireService*>(data);
    if (!key) return 0;
    std::lock_guard<std::recursive_mutex> lock(self.mtx_);
    auto resolve_name = [&](const char* name) -> std::uint32_t {
      if (!name) return 0;
      for (const auto& [id, n] : self.nodes_) {
        if (n.name == name) return id;
      }
      return 0;
    };
    if (std::strcmp(key, "default.audio.sink") == 0) self.default_sink_ = resolve_name(value);
    if (std::strcmp(key, "default.audio.source") == 0) self.default_source_ = resolve_name(value);
    self.emit_change_locked();
    return 0;
  };

  pw_registry_add_listener(reg_, reg_hook_, &reg_events, this);
  pw_core_add_listener(core_, core_hook_, &core_events, this);

  pw_thread_loop_start(loop_);
  snap_.available = true;
  emit_change_locked();
}

void PipeWireService::refresh_default_nodes_locked() {
   
  if (default_sink_ == 0) {
    for (const auto& [id, n] : nodes_) {
      if (n.media_class == "Audio/Sink") {
        default_sink_ = id;
        break;
      }
    }
  }
  if (default_source_ == 0) {
    for (const auto& [id, n] : nodes_) {
      if (n.media_class == "Audio/Source") {
        default_source_ = id;
        break;
      }
    }
  }
}

void PipeWireService::rebuild_snapshot_locked() {
   
  Snapshot s{};
  s.available = snap_.available;
  s.default_sink = default_sink_;
  s.default_source = default_source_;

  for (const auto& [id, n] : nodes_) {
    if (n.media_class == "Audio/Sink") {
      Device d{};
      d.node_id = id;
      d.name = n.name;
      d.display_name = !n.description.empty() ? n.description : (!n.nick.empty() ? n.nick : n.name);
      d.is_default = (id == default_sink_);
      d.muted = n.muted;
      d.volume_pct = n.volume_pct;
      s.sinks.push_back(std::move(d));
    } else if (n.media_class == "Audio/Source") {
      Device d{};
      d.node_id = id;
      d.name = n.name;
      d.display_name = !n.description.empty() ? n.description : (!n.nick.empty() ? n.nick : n.name);
      d.is_default = (id == default_source_);
      d.muted = n.muted;
      d.volume_pct = n.volume_pct;
      s.sources.push_back(std::move(d));
    } else if (n.media_class == "Stream/Output/Audio") {
      Stream st{};
      st.node_id = id;
      st.node_name = n.name;
      st.volume_pct = n.volume_pct;
      st.muted = n.muted;
      if (!n.app_name.empty()) st.app_name = n.app_name;
      else if (!n.nick.empty()) st.app_name = n.nick;
      else if (!n.description.empty()) st.app_name = n.description;
      else st.app_name = n.name;
      st.icon_name = n.icon_name;
      st.app_id = n.app_id;
      st.process_binary = n.process_binary;
      st.process_path = n.process_path;
      st.description = n.description;
      s.output_streams.push_back(std::move(st));
    } else if (n.media_class == "Stream/Input/Audio") {
      Stream st{};
      st.node_id = id;
      st.node_name = n.name;
      st.volume_pct = n.volume_pct;
      st.muted = n.muted;
      if (!n.app_name.empty()) st.app_name = n.app_name;
      else if (!n.nick.empty()) st.app_name = n.nick;
      else if (!n.description.empty()) st.app_name = n.description;
      else st.app_name = n.name;
      st.icon_name = n.icon_name;
      st.app_id = n.app_id;
      st.process_binary = n.process_binary;
      st.process_path = n.process_path;
      st.description = n.description;
      s.input_streams.push_back(std::move(st));
    }
  }

  auto by_default_then_name = [](const auto& a, const auto& b) {
    if (a.is_default != b.is_default) return a.is_default > b.is_default;
    return a.display_name < b.display_name;
  };
  std::sort(s.sinks.begin(), s.sinks.end(), by_default_then_name);
  std::sort(s.sources.begin(), s.sources.end(), by_default_then_name);

  auto by_node_id = [](const Stream& a, const Stream& b) { return a.node_id < b.node_id; };
  std::sort(s.output_streams.begin(), s.output_streams.end(), by_node_id);
  std::sort(s.input_streams.begin(), s.input_streams.end(), by_node_id);

  snap_ = std::move(s);
}

void PipeWireService::set_node_volume(std::uint32_t node_id, double normalized_0_1) {
   
  pw_proxy* proxy = nullptr;
  std::uint8_t buf[512]{};
  spa_pod_builder b{};
  const spa_pod* pod = nullptr;

  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return;
    if (!it->second.proxy) return;
    const int pct = pct_from_norm(normalized_0_1);
    it->second.volume_pct = pct;
    const float v = std::clamp(static_cast<float>(pct) / 100.0f, 0.0f, 1.5f);
    const int ch = std::max(1, it->second.channels);
    std::vector<float> vols(static_cast<size_t>(ch), v);

    spa_pod_builder_init(&b, buf, sizeof(buf));
    pod = reinterpret_cast<const spa_pod*>(spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_mute, SPA_POD_Bool(it->second.muted),
        SPA_PROP_channelVolumes,
        SPA_POD_Array(sizeof(float), SPA_TYPE_Float, static_cast<uint32_t>(vols.size()), vols.data())));

    proxy = it->second.proxy;
    pw_proxy_ref(proxy);
  }

  if (loop_) {
    pw_thread_loop_lock(loop_);
    pw_node_set_param(reinterpret_cast<pw_node*>(proxy), SPA_PARAM_Props, 0, pod);
    pw_proxy_unref(proxy);
    pw_thread_loop_unlock(loop_);
  } else {
    pw_node_set_param(reinterpret_cast<pw_node*>(proxy), SPA_PARAM_Props, 0, pod);
    pw_proxy_unref(proxy);
  }

  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

void PipeWireService::set_node_mute(std::uint32_t node_id, bool muted) {
   
  pw_proxy* proxy = nullptr;
  std::uint8_t buf[512]{};
  spa_pod_builder b{};
  const spa_pod* pod = nullptr;

  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return;
    if (!it->second.proxy) return;
    it->second.muted = muted;
    const float v = std::clamp(static_cast<float>(it->second.volume_pct) / 100.0f, 0.0f, 1.5f);
    const int ch = std::max(1, it->second.channels);
    std::vector<float> vols(static_cast<size_t>(ch), v);

    spa_pod_builder_init(&b, buf, sizeof(buf));
    pod = reinterpret_cast<const spa_pod*>(spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_mute, SPA_POD_Bool(muted), SPA_PROP_channelVolumes,
        SPA_POD_Array(sizeof(float), SPA_TYPE_Float, static_cast<uint32_t>(vols.size()), vols.data())));

    proxy = it->second.proxy;
    pw_proxy_ref(proxy);
  }

  if (loop_) {
    pw_thread_loop_lock(loop_);
    pw_node_set_param(reinterpret_cast<pw_node*>(proxy), SPA_PARAM_Props, 0, pod);
    pw_proxy_unref(proxy);
    pw_thread_loop_unlock(loop_);
  } else {
    pw_node_set_param(reinterpret_cast<pw_node*>(proxy), SPA_PARAM_Props, 0, pod);
    pw_proxy_unref(proxy);
  }

  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

void PipeWireService::set_default_sink(std::uint32_t node_id) {
   
  pw_metadata* md = nullptr;
  std::string sink_name;

  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    default_sink_ = node_id;
    auto it = nodes_.find(node_id);
    if (metadata_ && it != nodes_.end() && !it->second.name.empty()) {
      md = metadata_;
      sink_name = it->second.name;
      pw_proxy_ref(reinterpret_cast<pw_proxy*>(md));
    }
  }

  if (md) {
    if (loop_) {
      pw_thread_loop_lock(loop_);
      pw_metadata_set_property(md, 0, "default.audio.sink", nullptr, sink_name.c_str());
      pw_proxy_unref(reinterpret_cast<pw_proxy*>(md));
      pw_thread_loop_unlock(loop_);
    } else {
      pw_metadata_set_property(md, 0, "default.audio.sink", nullptr, sink_name.c_str());
      pw_proxy_unref(reinterpret_cast<pw_proxy*>(md));
    }
  }

  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

void PipeWireService::set_default_source(std::uint32_t node_id) {
   
  pw_metadata* md = nullptr;
  std::string src_name;

  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    default_source_ = node_id;
    auto it = nodes_.find(node_id);
    if (metadata_ && it != nodes_.end() && !it->second.name.empty()) {
      md = metadata_;
      src_name = it->second.name;
      pw_proxy_ref(reinterpret_cast<pw_proxy*>(md));
    }
  }

  if (md) {
    if (loop_) {
      pw_thread_loop_lock(loop_);
      pw_metadata_set_property(md, 0, "default.audio.source", nullptr, src_name.c_str());
      pw_proxy_unref(reinterpret_cast<pw_proxy*>(md));
      pw_thread_loop_unlock(loop_);
    } else {
      pw_metadata_set_property(md, 0, "default.audio.source", nullptr, src_name.c_str());
      pw_proxy_unref(reinterpret_cast<pw_proxy*>(md));
    }
  }

  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

void PipeWireService::route_playback_stream_to_sink(std::uint32_t stream_node_id, std::uint32_t sink_node_id) {
   
  std::string sn;
  std::string dn;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = nodes_.find(stream_node_id);
    auto jt = nodes_.find(sink_node_id);
    if (it == nodes_.end() || jt == nodes_.end()) return;
    sn = it->second.name;
    dn = jt->second.name;
  }
  pw_link_playback_stream_to_sink_by_name(sn, dn);
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

void PipeWireService::route_capture_stream_to_source(std::uint32_t stream_node_id, std::uint32_t source_node_id) {
   
  std::string tn;
  std::string sn;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = nodes_.find(stream_node_id);
    auto jt = nodes_.find(source_node_id);
    if (it == nodes_.end() || jt == nodes_.end()) return;
    tn = it->second.name;
    sn = jt->second.name;
  }
  pw_link_capture_stream_to_source_by_name(tn, sn);
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

std::string PipeWireService::playback_route_sink_name(std::uint32_t stream_node_id) const {
   
  std::string sn;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = nodes_.find(stream_node_id);
    if (it == nodes_.end()) return {};
    sn = it->second.name;
  }
  return pw_guess_playback_sink_name(sn);
}

std::string PipeWireService::capture_route_source_name(std::uint32_t stream_node_id) const {
   
  std::string tn;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = nodes_.find(stream_node_id);
    if (it == nodes_.end()) return {};
    tn = it->second.name;
  }
  return pw_guess_capture_source_name(tn);
}

EngineSettings PipeWireService::query_engine_settings() const {
   
  EngineSettings es{};
  std::string blob;
  if (!spawn_capture_stdout({eh_pw_metadata_exe(), "-n", "settings"}, &blob)) return es;

  std::string rate_s;
  std::string force_s;
  std::string allowed_s;
  std::vector<std::string> lines;
  pw_split_lines(blob, &lines);
  for (const std::string& line : lines) {
    if (!rate_s.empty() && !force_s.empty() && !allowed_s.empty()) break;
    std::string tmp;
    if (rate_s.empty() && pw_meta_take_quoted_value(line, "clock.rate", &tmp)) rate_s = std::move(tmp);
    else if (force_s.empty() && pw_meta_take_quoted_value(line, "clock.force-rate", &tmp))
      force_s = std::move(tmp);
    else if (allowed_s.empty() && pw_meta_take_quoted_value(line, "clock.allowed-rates", &tmp))
      allowed_s = std::move(tmp);
  }
  if (rate_s.empty()) return es;

  es.available = true;
  es.clock_rate_hz = pw_parse_positive_int_value(rate_s, 48000);
  es.clock_force_rate_hz = force_s.empty() ? 0 : pw_parse_nonneg_int_value(force_s, 0);
  if (!allowed_s.empty()) es.allowed_rates_hz = pw_parse_int_bracket_list(allowed_s);
  return es;
}

void PipeWireService::apply_engine_clock_rate_hz(int hz) {
   
  if (hz <= 0 || hz > 2000000) return;
  EngineSettings cur = query_engine_settings();
  std::vector<int> merged =
      cur.available ? pw_merge_unique_sorted(cur.allowed_rates_hz, hz) : std::vector<int>{hz};
  const std::string allowed_meta = pw_format_allowed_rates_metadata(merged);
  const char* pmd = eh_pw_metadata_exe();
  (void)spawn_exec_wait_owned({pmd, "-n", "settings", "0", "clock.allowed-rates", allowed_meta});
  const std::string hz_s = std::to_string(hz);
  (void)spawn_exec_wait_owned({pmd, "-n", "settings", "0", "clock.rate", hz_s});

  (void)spawn_exec_wait_owned({pmd, "-n", "settings", "0", "clock.force-rate", hz_s});
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

void PipeWireService::apply_engine_force_rate_hz(int hz) {
   
  if (hz < 0 || hz > 2000000) return;
  const std::string hz_s = std::to_string(hz);
  (void)spawn_exec_wait_owned({eh_pw_metadata_exe(), "-n", "settings", "0", "clock.force-rate", hz_s});
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

void PipeWireService::apply_engine_allowed_rates_hz(const std::vector<int>& rates_hz) {
   
  std::vector<int> v = rates_hz;
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  v.erase(std::remove_if(v.begin(), v.end(), [](int x) { return x <= 0 || x > 2000000; }), v.end());
  if (v.empty()) return;
  const std::string allowed_meta = pw_format_allowed_rates_metadata(v);
  (void)spawn_exec_wait_owned({eh_pw_metadata_exe(), "-n", "settings", "0", "clock.allowed-rates", allowed_meta});
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

CompatDefaultSinkFormat PipeWireService::query_compat_default_sink_format() const {
   
  CompatDefaultSinkFormat out{};
  std::string def_blob;
  if (!spawn_capture_stdout({eh_pactl_exe(), "get-default-sink"}, &def_blob)) return out;
  trim_ascii_ws_inplace(&def_blob);
  if (def_blob.empty()) return out;

  std::string list_blob;
  if (!spawn_capture_stdout({eh_pactl_exe(), "list", "sinks", "short"}, &list_blob)) return out;

  std::vector<std::string> lines;
  pw_split_lines(list_blob, &lines);
  for (std::string line : lines) {
    trim_ascii_ws_inplace(&line);
    if (line.empty()) continue;
    const auto cols = split_pw_line_tabs(line);
    if (cols.size() < 4) continue;
    std::string sink_name = cols[1];
    trim_ascii_ws_inplace(&sink_name);
    if (sink_name != def_blob) continue;
    std::uint32_t idx = 0;
    {
      char* endp = nullptr;
      const unsigned long v = std::strtoul(cols[0].c_str(), &endp, 10);
      if (endp == cols[0].c_str() || v > 0xffffffffUL) continue;
      idx = static_cast<std::uint32_t>(v);
    }
    std::string spec = cols[3];
    trim_ascii_ws_inplace(&spec);
    std::string enc;
    int ch = 2;
    int rate = 48000;
    parse_pactl_sample_spec_fields(spec, &enc, &ch, &rate);
    out.available = true;
    out.sink_index = idx;
    out.sample_spec = spec;
    out.format_encoding = enc;
    out.channels = ch;
    out.rate_hz = rate;

    // If we previously wrote a WirePlumber rule pinning the default sink's
    // PCM format, prefer reporting that configured format. The live pactl
    // sample spec only reflects it after the device has reopened.
    std::string cfg_node;
    std::string cfg_fmt;
    if (read_eh_wp_pcm_conf(&cfg_node, &cfg_fmt) && cfg_node == def_blob) {
      const int ci = compat_pcm_wp_format_choice(cfg_fmt);
      const char* enc_cfg = ci >= 0 ? compat_pcm_choice_encoding(ci) : nullptr;
      if (enc_cfg) {
        out.format_encoding = enc_cfg;
        std::ostringstream spec_cfg;
        spec_cfg << enc_cfg << " " << ch << "ch " << rate << "Hz";
        out.sample_spec = spec_cfg.str();
      }
    }
    break;
  }
  return out;
}

void PipeWireService::apply_compat_default_sink_pcm_format(int choice_0_3) {
   
  if (choice_0_3 < 0 || choice_0_3 >= 4) return;
  const char* spa_fmt = compat_pcm_choice_wp_format(choice_0_3);
  if (!spa_fmt) return;

  std::string def_blob;
  if (!spawn_capture_stdout({eh_pactl_exe(), "get-default-sink"}, &def_blob)) return;
  trim_ascii_ws_inplace(&def_blob);
  if (def_blob.empty()) return;

  (void)write_eh_wp_pcm_conf(def_blob, spa_fmt);

  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

void PipeWireService::apply_saved_defaults(const SavedDefaults& defaults) {
    

  std::uint32_t found_sink = 0;
  std::uint32_t found_source = 0;

  {
    std::lock_guard<std::recursive_mutex> lock(mtx_);

    if (!defaults.default_sink_name.empty()) {
      for (const auto& [id, n] : nodes_) {
        if (n.name == defaults.default_sink_name && n.media_class == "Audio/Sink") {
          found_sink = id;
          default_sink_ = id;
          if (defaults.default_sink_volume_pct != n.volume_pct) {
            set_node_volume(id, static_cast<double>(defaults.default_sink_volume_pct) / 100.0);
          }
          if (defaults.default_sink_muted != n.muted) {
            set_node_mute(id, defaults.default_sink_muted);
          }
          break;
        }
      }
    }

    if (!defaults.default_source_name.empty()) {
      for (const auto& [id, n] : nodes_) {
        if (n.name == defaults.default_source_name && n.media_class == "Audio/Source") {
          found_source = id;
          default_source_ = id;
          if (defaults.default_source_volume_pct != n.volume_pct) {
            set_node_volume(id, static_cast<double>(defaults.default_source_volume_pct) / 100.0);
          }
          if (defaults.default_source_muted != n.muted) {
            set_node_mute(id, defaults.default_source_muted);
          }
          break;
        }
      }
    }

    rebuild_snapshot_locked();
  }

  if (found_sink) set_default_sink(found_sink);
  if (found_source) set_default_source(found_source);

  // Graph/engine + PCM format. Each is idempotent: only rewritten when the
  // current pipewire value differs from the saved one.
  const EngineSettings cur = query_engine_settings();
  if (defaults.engine_clock_rate_hz > 0 && (!cur.available || cur.clock_rate_hz != defaults.engine_clock_rate_hz)) {
    apply_engine_clock_rate_hz(defaults.engine_clock_rate_hz);
  }
  if (!cur.available || cur.clock_force_rate_hz != defaults.engine_force_rate_hz) {
    apply_engine_force_rate_hz(defaults.engine_force_rate_hz);
  }
  if (!defaults.engine_allowed_rates_hz.empty() &&
      (!cur.available || cur.allowed_rates_hz != defaults.engine_allowed_rates_hz)) {
    apply_engine_allowed_rates_hz(defaults.engine_allowed_rates_hz);
  }
  if (defaults.compat_pcm_format >= 0 && defaults.compat_pcm_format < 4) {
    apply_compat_default_sink_pcm_format(defaults.compat_pcm_format);
  }
}

static bool extract_next_card_json_object(const std::string& blob, size_t* pos_inout, std::string* card_out) {
   
  const auto p = blob.find("{\"index\":", *pos_inout);
  if (p == std::string::npos) return false;
  int depth = 0;
  for (size_t i = p; i < blob.size(); ++i) {
    if (blob[i] == '{')
      depth++;
    else if (blob[i] == '}') {
      depth--;
      if (depth == 0) {
        *card_out = blob.substr(p, i - p + 1);
        *pos_inout = i + 1;
        return true;
      }
    }
  }
  return false;
}

static std::string json_string_value_quoted(const std::string& s, const char* key) {
   
  const std::string pat = std::string("\"") + key + "\":\"";
  auto p = s.find(pat);
  if (p == std::string::npos) return {};
  p += pat.size();
  const auto q = s.find('"', p);
  if (q == std::string::npos || q <= p) return {};
  return s.substr(p, q - p);
}

static bool json_uint_field(const std::string& s, const char* key, unsigned* out) {
   
  const std::string pat = std::string("\"") + key + "\":";
  auto p = s.find(pat);
  if (p == std::string::npos) return false;
  p += pat.size();
  while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) p++;
  char* endp = nullptr;
  const unsigned long v = std::strtoul(s.c_str() + p, &endp, 10);
  if (endp == s.c_str() + p) return false;
  *out = static_cast<unsigned>(v);
  return true;
}

static std::string pactl_card_profiles_inner_json(const std::string& card) {
   
  auto p = card.find("\"profiles\":");
  if (p == std::string::npos) return {};
  p = card.find('{', p + 11);
  if (p == std::string::npos) return {};
  int depth = 0;
  const size_t inner_start = p + 1;
  for (size_t i = p; i < card.size(); ++i) {
    if (card[i] == '{')
      depth++;
    else if (card[i] == '}') {
      depth--;
      if (depth == 0) return card.substr(inner_start, i - inner_start);
    }
  }
  return {};
}

std::vector<BluetoothCardProfiles> PipeWireService::query_bluetooth_cards() const {
   
  std::vector<BluetoothCardProfiles> out;
  std::string blob;
  if (!spawn_capture_stdout({eh_pactl_exe(), "-f", "json", "list", "cards"}, &blob))
    return out;

  constexpr std::uint32_t kMaxCards = 4;
  size_t scan = 0;
  std::string card;
  while (out.size() < kMaxCards && extract_next_card_json_object(blob, &scan, &card)) {
    const std::string driver = json_string_value_quoted(card, "driver");
    std::string dlow = ascii_lower_copy(driver);
    if (dlow.find("bluez") == std::string::npos) continue;

    BluetoothCardProfiles bp{};
    unsigned idx_u = 0;
    if (!json_uint_field(card, "index", &idx_u)) continue;
    bp.card_index = static_cast<std::uint32_t>(idx_u);
    bp.card_name = json_string_value_quoted(card, "name");
    if (bp.card_name.empty()) bp.card_name = json_string_value_quoted(card, "device.description");
    if (bp.card_name.empty()) bp.card_name = "Bluetooth audio";
    bp.active_profile_key = json_string_value_quoted(card, "active_profile");

    const std::string prof_inner = pactl_card_profiles_inner_json(card);
    if (!prof_inner.empty()) {
      std::size_t pos = 0;
      while (pos < prof_inner.size()) {
        auto key_start = prof_inner.find('"', pos);
        if (key_start == std::string::npos || key_start + 1 >= prof_inner.size()) break;
        auto key_end = prof_inner.find('"', key_start + 1);
        if (key_end == std::string::npos) break;
        const std::string_view key(prof_inner.data() + key_start + 1, key_end - key_start - 1);
        constexpr std::string_view kDescKey = R"({"description":")";
        auto desc_start = prof_inner.find(kDescKey, key_end + 1);
        if (desc_start == std::string::npos) break;
        desc_start += kDescKey.size();
        auto desc_end = prof_inner.find('"', desc_start);
        if (desc_end == std::string::npos) break;
        BluetoothCardProfileOption opt;
        opt.key = key;
        opt.description = std::string(prof_inner.data() + desc_start, desc_end - desc_start);
        if (!opt.key.empty()) bp.profiles.push_back(std::move(opt));
        pos = desc_end + 1;
      }
    }
    if (!bp.profiles.empty()) out.push_back(std::move(bp));
  }
  return out;
}

void PipeWireService::apply_bluetooth_card_profile(std::uint32_t card_index, const std::string& profile_key) {
   
  if (profile_key.empty()) return;
  const std::string idx_s = std::to_string(card_index);
  (void)spawn_exec_wait_owned({eh_pactl_exe(), "set-card-profile", idx_s, profile_key});
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  emit_change_locked();
}

}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
