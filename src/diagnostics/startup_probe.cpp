#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

static std::atomic<bool> g_stop{false};
static void on_sig(int) { g_stop = true; }

static std::string now_str() {
  char buf[64];
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tmv;
  localtime_r(&ts.tv_sec, &tmv);
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
                tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000);
  return buf;
}

static std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) return std::string();
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static std::string tail_file(const std::string& path, long long limit) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::string();
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size <= limit) {
    in.seekg(0);
    std::string body((std::size_t)size, '\0');
    if (!body.empty()) in.read(body.data(), (std::streamsize)size);
    return body;
  }
  in.seekg(-limit, std::ios::end);
  std::string chunk((std::size_t)limit, '\0');
  in.read(chunk.data(), (std::streamsize)limit);
  return chunk;
}

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return std::string();
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Monotonic time between samples, in ms.
static std::chrono::steady_clock::time_point g_timer_t0 = std::chrono::steady_clock::now();
static long long timer_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - g_timer_t0)
      .count();
}

static std::string exec_capture(const std::string& cmd, int timeout_ms = 3000) {
  std::string out;
  int fds[2];
  if (pipe(fds) != 0) return out;
  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return out;
  }
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);
    execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
    _exit(127);
  }
  close(fds[1]);
  fcntl(fds[0], F_SETFL, O_NONBLOCK);
  char buf[4096];
  auto start = std::chrono::steady_clock::now();
  while (true) {
    struct pollfd p{};
    p.fd = fds[0];
    p.events = POLLIN;
    int pr = poll(&p, 1, 50);
    if (pr > 0 && (p.revents & POLLIN)) {
      ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
      if (n > 0) {
        buf[n] = '\0';
        out += buf;
      }
    }
    int status = 0;
    pid_t wr = waitpid(pid, &status, WNOHANG);
    if (wr == pid) break;
    if (std::chrono::steady_clock::now() - start >
        std::chrono::milliseconds(timeout_ms)) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      break;
    }
  }
  close(fds[0]);
  return out;
}

struct ProcInfo {
  int pid = -1;
  int ppid = -1;
  char state = '?';
  std::string wchan;
  std::string cmd;
};

static bool interesting_cmd(const std::string& argv0) {
  return argv0.find("horizon-") != std::string::npos ||
         argv0.find("EventHorizon") != std::string::npos ||
         argv0 == "hyprland" || argv0 == "Hyprland" ||
         argv0.find("hyprland") != std::string::npos;
}

static std::vector<ProcInfo> scan_procs() {
  std::vector<ProcInfo> out;
  for (const auto& entry : fs::directory_iterator("/proc")) {
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] < '0' || name[0] > '9') continue;
    const int pid = std::atoi(name.c_str());
    const std::string fcmd = entry.path().string() + "/cmdline";
    std::ifstream in(fcmd);
    if (!in) continue;
    std::string cmdline;
    {
      std::ostringstream ss;
      ss << in.rdbuf();
      cmdline = ss.str();
    }
    std::string argv0;
    for (char c : cmdline) {
      if (c == '\0') break;
      argv0 += c;
    }
    if (!interesting_cmd(argv0)) continue;
    ProcInfo pi;
    pi.pid = pid;
    pi.cmd = argv0;
    const std::string fstat = entry.path().string() + "/stat";
    const std::string stat = read_file(fstat);
    {
      const size_t closeParen = stat.rfind(')');
      if (closeParen != std::string::npos && closeParen + 2 < stat.size()) {
        pi.state = stat[closeParen + 2];
        const std::string rest = stat.substr(closeParen + 1);
        std::sscanf(rest.c_str(), " %*c %d", &pi.ppid);
      }
    }
    pi.wchan = trim(read_file(entry.path().string() + "/wchan"));
    out.push_back(std::move(pi));
  }
  return out;
}

static std::string proc_section(const std::vector<ProcInfo>& procs) {
  std::ostringstream ss;
  for (const auto& p : procs) {
    ss << "  [" << p.cmd << "] pid=" << p.pid << " ppid=" << p.ppid
       << " state=" << p.state << " wchan=" << p.wchan << "\n";
  }
  return ss.str();
}

// Per-process detail: threads, syscall, memory, and I/O counters.
struct ThreadInfo {
  int tid = -1;
  char state = '?';
  std::string wchan;
};

static std::vector<ThreadInfo> scan_threads(int pid) {
  std::vector<ThreadInfo> th;
  std::error_code ec;
  const fs::path td = "/proc/" + std::to_string(pid) + "/task";
  for (const auto& entry : fs::directory_iterator(td, ec)) {
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] < '0' || name[0] > '9') continue;
    ThreadInfo t;
    t.tid = std::atoi(name.c_str());
    const std::string stat = read_file(entry.path().string() + "/stat");
    const size_t cp = stat.rfind(')');
    if (cp != std::string::npos && cp + 2 < stat.size()) t.state = stat[cp + 2];
    t.wchan = trim(read_file(entry.path().string() + "/wchan"));
    th.push_back(std::move(t));
  }
  return th;
}

static std::vector<std::pair<std::string, long long>> parse_keyvals(const std::string& txt) {
  std::vector<std::pair<std::string, long long>> out;
  std::istringstream ss(txt);
  std::string line;
  while (std::getline(ss, line)) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = line.substr(0, colon);
    const std::string val = trim(line.substr(colon + 1));
    out.push_back({key, std::atoll(val.c_str())});
  }
  return out;
}

static std::string detail_section(const std::vector<ProcInfo>& procs) {
  std::ostringstream ss;
  for (const auto& p : procs) {
    if (p.cmd.find("horizon-") == std::string::npos && p.cmd != "EventHorizon")
      continue;
    const std::string base = "/proc/" + std::to_string(p.pid);
    ss << "  [" << p.cmd << "] pid=" << p.pid << "\n";

    const std::vector<ThreadInfo> th = scan_threads(p.pid);
    std::vector<std::pair<std::string, long long>> stalls;
    const char* snapsrc = nullptr;
    {
      const std::string snap = read_file(base + "/syscall");
      if (snap == "running") snapsrc = "syscall=RUNNING on cpu";
      else if (!snap.empty()) snapsrc = nullptr;
      ss << "    syscall_pending: " << (snap.empty() ? "(unreadable)" : snap) << "\n";
    }
    for (const auto& thread : th) {
      ss << "    tid=" << thread.tid << " state=" << thread.state
         << " wchan=" << thread.wchan << "\n";
      if (thread.state == 'D') {
        stalls.push_back({thread.wchan.empty() ? "?" : thread.wchan, 0});
      }
    }
    if (stalls.size()) {
      ss << "    D-state threads: ";
      for (const auto& s : stalls) ss << s.first << " ";
      ss << "\n";
    }

    const std::string status = read_file(base + "/status");
    if (!status.empty()) {
      const auto fields = parse_keyvals(status);
      for (const auto& kv : fields) {
        if (kv.first == "State" || kv.first == "Threads" || kv.first == "VmRSS" ||
            kv.first == "voluntary_ctxt_switches" ||
            kv.first == "nonvoluntary_ctxt_switches") {
          ss << "    " << kv.first << ": " << kv.second << "\n";
        }
      }
    }
    (void)snapsrc;

    const std::string io = read_file(base + "/io");
    if (!io.empty()) {
      const auto iof = parse_keyvals(io);
      for (const auto& kv : iof) {
        if (kv.first == "rchar" || kv.first == "wchar" ||
            kv.first == "read_bytes" || kv.first == "write_bytes" ||
            kv.first == "canceled_write_bytes") {
          ss << "    io." << kv.first << "=" << kv.second << "\n";
        }
      }
    }
  }
  return ss.str();
}

// Memory + disk activity, deltas between samples.
static long long g_dirty[4] = {0, 0, 0, 0};  // dirty, writeback, nfs, wbtmp
static long long g_sects_rd = -1, g_sects_wr = -1, g_ms_rd = -1, g_ms_wr = -1, g_io_inflight = -1;

static void kstat_section(std::ostringstream& ss, long long elapsed) {
  const std::string mem = read_file("/proc/meminfo");
  long long dirty = 0, wb = 0, nfs = 0, wbtmp = 0;
  if (!mem.empty()) {
    const auto f = parse_keyvals(mem);
    for (const auto& kv : f) {
      if (kv.first == "Dirty") dirty = kv.second;
      else if (kv.first == "Writeback") wb = kv.second;
      else if (kv.first == "NFS_Unstable") nfs = kv.second;
      else if (kv.first == "WritebackTmp") wbtmp = kv.second;
    }
  }
  ss << "  [mem] Dirty=" << dirty << " Writeback=" << wb << " NFS=" << nfs
     << " WBTmp=" << wbtmp << " kB";
  if (g_dirty[0] >= 0)
    ss << " (ΔDirty=" << (dirty - g_dirty[0]) << " ΔWb=" << (wb - g_dirty[1]) << ")";
  ss << "\n";
  g_dirty[0] = dirty;
  g_dirty[1] = wb;
  g_dirty[2] = nfs;
  g_dirty[3] = wbtmp;

  const std::string ds = read_file("/proc/diskstats");
  if (!ds.empty()) {
    std::istringstream iss(ds);
    std::string line;
    long long sects_rd = -1, sects_wr = -1, ms_rd = -1, ms_wr = -1, inflight = -1;
    while (std::getline(iss, line)) {
      std::istringstream ls(line);
      long long major = 0, minor = 0, a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0;
      std::string dev;
      ls >> major >> minor >> dev >> a >> b >> sects_rd >> ms_rd >> c >> d >>
          sects_wr >> ms_wr >> inflight >> e >> f >> g;
      if (dev == "nvme0n1" || dev == "sda") {
        ss << "  [" << dev << "] sectors_rd=" << sects_rd
           << " sectors_wr=" << sects_wr << " ms_active_rd=" << ms_rd
           << " ms_active_wr=" << ms_wr << " inflight=" << inflight << "\n";
        if (g_sects_rd >= 0 && elapsed > 0) {
          ss << "    Δ rd_mb=" << ((sects_rd - g_sects_rd) * 512 / 1048576)
             << " Δ wr_mb=" << ((sects_wr - g_sects_wr) * 512 / 1048576)
             << " Δ ms_rd=" << (ms_rd - g_ms_rd)
             << " Δ ms_wr=" << (ms_wr - g_ms_wr) << "\n";
        }
        g_sects_rd = sects_rd;
        g_sects_wr = sects_wr;
        g_ms_rd = ms_rd;
        g_ms_wr = ms_wr;
        g_io_inflight = inflight;
        break;
      }
    }
  }
}

struct FlushedChunk { std::string path; long long size; };

static std::string layers_section() {
  if (getenv("WAYLAND_DISPLAY") == nullptr) {
    return "  (no WAYLAND_DISPLAY in env)\n";
  }
  const std::string raw = exec_capture("hyprctl layers 2>&1", 4000);
  if (raw.empty()) return "  (hyprctl layers returned nothing / not available)\n";
  return (raw.size() <= 4000) ? "  " + raw
                              : "  " + raw.substr(0, 4000) + "\n  ... (truncated)\n";
}

static std::string clients_section() {
  if (getenv("WAYLAND_DISPLAY") == nullptr) return "";
  const std::string raw = exec_capture("hyprctl clients 2>&1", 4000);
  if (raw.empty()) return "";
  std::ostringstream ss;
  ss << "  --- hyprctl clients (" << raw.size() << " bytes) ---\n";
  ss << "  " << raw.substr(0, 4000);
  return ss.str() + "\n";
}

static std::string monitors_section() {
  if (getenv("WAYLAND_DISPLAY") == nullptr) return "";
  const std::string raw = exec_capture("hyprctl monitors 2>&1", 4000);
  if (raw.empty()) return "";
  std::ostringstream ss;
  ss << "  --- hyprctl monitors (" << raw.size() << " bytes) ---\n";
  ss << "  " << raw.substr(0, 2000);
  return ss.str() + "\n";
}

static std::string log_tail_section(const std::string& path) {
  std::ostringstream ss;
  struct stat st{};
  if (::stat(path.c_str(), &st) != 0) {
    ss << "  [" << fs::path(path).filename().string() << "] missing\n";
    return ss.str();
  }
  ss << "  [" << fs::path(path).filename().string() << "] size=" << st.st_size;
  const std::string body = tail_file(path, 2200);
  ss << " tail:\n" << body;
  return ss.str() + "\n";
}

int main(int argc, char** argv) {
  std::signal(SIGINT, on_sig);
  std::signal(SIGTERM, on_sig);

  int interval_ms = 1500;
  int max_samples = 240;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--interval") == 0 && i + 1 < argc)
      interval_ms = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--samples") == 0 && i + 1 < argc)
      max_samples = std::atoi(argv[++i]);
  }

  const fs::path stateDir = fs::path(getenv("HOME") ? getenv("HOME") : "/tmp") /
                            ".local/state/event-horizon";
  fs::create_directories(stateDir);
  const std::string logPath = (stateDir / "startup-probe.log").string();

  std::ofstream log(logPath, std::ios::app);
  if (!log) {
    std::cerr << "startup-probe: cannot open " << logPath << "\n";
    return 1;
  }

  const std::string debugLog = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") +
                               "/event-horizon-debug.log";

  {
    std::ostringstream hdr;
    hdr << "╌╌ startup-probe v2 — pid=" << getpid() << " start=" << now_str() << "\n";
    hdr << "  interval_ms=" << interval_ms << " max_samples=" << max_samples << "\n";
    hdr << "  WAYLAND_DISPLAY=" << (getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(null)") << "\n";
    hdr << "  XDG_SESSION_TYPE=" << (getenv("XDG_SESSION_TYPE") ? getenv("XDG_SESSION_TYPE") : "(null)") << "\n";
    log << hdr.str() << std::flush;
  }
  std::cerr << "startup-probe: logging to " << logPath << " (pid=" << getpid() << ")\n";

  int sample = 0;
  // Children seen in earlier samples, for respawn detection.
  std::map<std::string, int> prevChildPid;
  std::map<std::string, long long> prevLogSize;
  while (!g_stop && sample < max_samples) {
    const long long elapsed = timer_ms();
    log << "\n=== SAMPLE " << sample << " t=" << elapsed << "ms " << now_str() << " ===\n";

    const std::vector<ProcInfo> procs = scan_procs();
    log << "[processes]\n" << proc_section(procs);

    // Respawn / first-birth detection.
    for (const auto& p : procs) {
      if (p.cmd.find("horizon-") == std::string::npos) continue;
      auto it = prevChildPid.find(p.cmd);
      if (it == prevChildPid.end()) {
        log << "[born] " << p.cmd << " first-seen pid=" << p.pid << ", t=" << elapsed
            << "ms\n";
        prevChildPid[p.cmd] = p.pid;
      } else if (it->second != p.pid) {
        log << "[respawn] " << p.cmd << " pid " << it->second << " -> " << p.pid
            << ", t=" << elapsed << "ms\n";
        it->second = p.pid;
      }
    }

    bool sawDe = false;
    std::string dePids;
    for (const auto& p : procs) {
      if (p.cmd.find("horizon-") != std::string::npos ||
          p.cmd == "EventHorizon") {
        sawDe = true;
        if (!dePids.empty()) dePids += " ";
        dePids += std::to_string(p.pid);
      }
    }
    log << "[de-present] " << (sawDe ? "yes pids=" + dePids : "no") << "\n";

    if (sawDe) {
      log << "[fds]\n";
      for (const auto& p : procs) {
        if (p.cmd.find("horizon-") == std::string::npos &&
            p.cmd != "EventHorizon")
          continue;
        const std::string dir = "/proc/" + std::to_string(p.pid) + "/fd";
        std::vector<std::string> interesting;
        {
          std::error_code ec;
          for (const auto& entry : fs::directory_iterator(dir, ec)) {
            std::string tgt;
            {
              std::string linkbuf(256, '\0');
              const ssize_t n =
                  ::readlink(entry.path().c_str(), linkbuf.data(), linkbuf.size() - 1);
              if (n > 0) {
                linkbuf[n] = '\0';
                tgt = linkbuf.c_str();
              }
            }
            if (tgt.find("wayland") != std::string::npos ||
                tgt.find("event-horizon") != std::string::npos ||
                tgt.find(".log") != std::string::npos ||
                tgt.find("/ipc/") != std::string::npos) {
              interesting.push_back(tgt);
            }
          }
        }
        log << "  " << p.cmd << "(pid=" << p.pid << "):\n";
        for (const auto& t : interesting) log << "    " << t << "\n";
      }
    }

    log << "[proc detail]\n" << detail_section(procs);

    log << "[kstat]\n";
    {
      std::ostringstream ks;
      kstat_section(ks, interval_ms);
      log << ks.str();
    }

    log << "[hyprctl layers]\n" << layers_section();
    log << monitors_section();

    log << "[child logs]\n";
    for (const char* name : {"horizon-wallpaper.log", "horizon-desktop.log",
                             "horizon-dock.log", "horizon-taskbar.log",
                             "horizon-notifications.log"}) {
      const std::string path = (stateDir / name).string();
      struct stat st{};
      const long long size = (::stat(path.c_str(), &st) == 0) ? (long long)st.st_size : 0;
      auto it = prevLogSize.find(name);
      if (it != prevLogSize.end()) {
        const long long delta = size - it->second;
        if (delta > 0)
          log << "  [growth] " << name << " += " << delta << " bytes\n";
      }
      prevLogSize[name] = size;
      log << log_tail_section(path);
    }
    log << "[supervisor debug log]\n";
    {
      struct stat st{};
      if (::stat(debugLog.c_str(), &st) == 0)
        log << "  size=" << st.st_size << " tail:\n" << tail_file(debugLog, 2500) << "\n";
      else
        log << "  missing\n";
    }

    log << clients_section();
    log << "=== END SAMPLE " << sample << " ===\n" << std::flush;
    ++sample;

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
    while (!g_stop && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  log << "╌╌ startup-probe done — end=" << now_str() << " samples=" << sample << "\n"
      << std::flush;
  return 0;
}