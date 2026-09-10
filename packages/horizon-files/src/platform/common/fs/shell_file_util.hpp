#pragma once

#include <array>
#include <string>
#include <unistd.h>

namespace eh::shell {

inline bool file_exists_str(const std::string& p) { return ::access(p.c_str(), R_OK) == 0; }

inline std::string exe_directory() {
  std::array<char, 4096> buf;
  const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (n <= 0) return ".";
  buf[n] = '\0';
  std::string p(buf.data());
  const auto slash = p.find_last_of('/');
  if (slash == std::string::npos) return ".";
  return p.substr(0, slash);
}

}
