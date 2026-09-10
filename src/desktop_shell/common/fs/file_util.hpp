#pragma once

#include <optional>
#include <string>
#include <sys/stat.h>

namespace eh::shell::file_util {

inline std::optional<timespec> file_mtime(const std::string& path) {
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) return std::nullopt;
#if defined(__APPLE__)
  return st.st_mtimespec;
#else
  return st.st_mtim;
#endif
}

}
