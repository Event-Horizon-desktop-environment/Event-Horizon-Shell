#include "desktop_shell/common/system_theming/system_theming.hpp"
#include "desktop_shell/common/system_theming/system_theming_utils.hpp"

#include "desktop_shell/common/fs/string_util.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern "C" char** environ;

namespace eh::theming::detail {

using eh::shell::str::split_colon_list;
using eh::shell::str::trim;

std::string run_cmd_capture(const std::string& cmd) {
   
  std::array<int, 2> fd{};
  if (pipe(fd.data()) < 0) return {};

  std::vector<char> arg_c(cmd.begin(), cmd.end());
  arg_c.push_back('\0');

  posix_spawn_file_actions_t fa{};
  if (posix_spawn_file_actions_init(&fa) != 0) {
    close(fd[0]);
    close(fd[1]);
    return {};
  }
  if (posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return {};
  }
  if (fd[0] != STDOUT_FILENO && posix_spawn_file_actions_addclose(&fa, fd[0]) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return {};
  }
  if (fd[1] != STDOUT_FILENO && posix_spawn_file_actions_addclose(&fa, fd[1]) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return {};
  }
  if (posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return {};
  }

  char argv0[] = "/bin/sh";
  char argv1[] = "sh";
  char argv2[] = "-c";
  char* argv[] = {argv0, argv1, argv2, arg_c.data(), nullptr};

  pid_t pid = -1;
  const int spawn_err = posix_spawnp(&pid, "/bin/sh", &fa, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&fa);
  close(fd[1]);
  if (spawn_err != 0 || pid < 0) {
    close(fd[0]);
    return {};
  }
  std::string out;
  std::array<char, 256> buf{};
  for (;;) {
    pollfd pfd{fd[0], POLLIN, 0};
    const int pr = poll(&pfd, 1, 5000);
    if (pr <= 0) break;
    const ssize_t n = read(fd[0], buf.data(), buf.size());
    if (n <= 0) break;
    out.append(buf.data(), static_cast<size_t>(n));
  }
  close(fd[0]);
  (void)waitpid(pid, nullptr, 0);
  return out;
}

std::string read_ini_kv(const std::string& path, const std::string& wantSection, const std::string& wantKey) {
   
  std::ifstream f(path);
  if (!f.is_open()) return {};
  std::string line;
  bool inSec = wantSection.empty();
  std::string out;
  while (std::getline(f, line)) {
    std::string s = trim(line);
    if (s.empty() || s[0] == '#') continue;
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
      inSec = (s == wantSection);
      continue;
    }
    if (!inSec) continue;
    if (s.rfind(wantKey + "=", 0) == 0) {
      out = s.substr(wantKey.size() + 1);
      break;
    }
  }
  return out;
}

std::vector<std::string> icon_base_dirs() {
   
  std::vector<std::string> bases;
  auto push_unique = [&](const std::string& s) {
    if (s.empty()) return;
    if (std::find(bases.begin(), bases.end(), s) != bases.end()) return;
    bases.push_back(s);
  };

  const char* home = std::getenv("HOME");
  if (home) {

    push_unique(std::string(home) + "/.icons");

    push_unique(std::string(home) + "/.themes");

    push_unique(std::string(home) + "/.local/share/icons");

    push_unique(std::string(home) + "/.local/share/flatpak/exports/share/icons");
  }

  if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
    push_unique(std::string(xdg) + "/icons");
  }

  auto dataDirs = split_colon_list(std::getenv("XDG_DATA_DIRS"));
  if (dataDirs.empty()) dataDirs = {"/usr/local/share", "/usr/share"};
  for (const auto& d : dataDirs) {
    push_unique(d + "/icons");

    push_unique(d + "/themes");
  }

  push_unique("/var/lib/flatpak/exports/share/icons");

  push_unique("/run/flatpak/exports/share/icons");
  return bases;
}

void update_ini_key(const std::string& path, const std::string& section,
                           const std::string& key, const std::string& value) {
   
  // Read existing
  std::string existing;
  {
    FILE* ef = fopen(path.c_str(), "rb");
    if (ef) {
      fseek(ef, 0, SEEK_END);
      long sz = ftell(ef);
      if (sz > 0) {
        fseek(ef, 0, SEEK_SET);
        existing.resize(static_cast<size_t>(sz));
        (void)fread(&existing[0], 1, static_cast<size_t>(sz), ef);
      }
      fclose(ef);
    }
  }

  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;

  bool foundSection = false;
  bool wroteKey = false;
  std::istringstream iss(existing);
  std::string line;
  while (std::getline(iss, line)) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (line.rfind('[', 0) == 0) {
      if (foundSection && !wroteKey) {
        // We were in the target section but didn't write the key yet
        fprintf(f, "%s=%s\n", key.c_str(), value.c_str());
        wroteKey = true;
      }
      foundSection = (line.find(section) != std::string::npos);
      fprintf(f, "%s\n", line.c_str());
    } else if (foundSection) {
      const size_t eq = line.find('=');
      if (eq != std::string::npos) {
        const std::string existingKey = line.substr(0, eq);
        // Trim whitespace from key
        size_t ke = existingKey.size();
        while (ke > 0 && (existingKey[ke-1] == ' ' || existingKey[ke-1] == '\t')) ke--;
        if (existingKey.compare(0, ke, key) == 0 && ke == key.size()) {
          if (!wroteKey) {
            fprintf(f, "%s=%s\n", key.c_str(), value.c_str());
            wroteKey = true;
          }
          continue;
        }
      }
      fprintf(f, "%s\n", line.c_str());
    } else {
      fprintf(f, "%s\n", line.c_str());
    }
  }
  if (foundSection && !wroteKey) {
    fprintf(f, "%s=%s\n", key.c_str(), value.c_str());
  }
  if (!foundSection) {
    fprintf(f, "\n%s\n%s=%s\n", section.c_str(), key.c_str(), value.c_str());
  }
  fclose(f);
}

} // namespace eh::theming::detail
