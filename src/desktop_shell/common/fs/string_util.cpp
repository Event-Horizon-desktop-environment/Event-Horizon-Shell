#include "desktop_shell/common/fs/string_util.hpp"

#include <unistd.h>

namespace eh::shell::str {

std::string trim(std::string s) {
   
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i) s.erase(0, i);
  return s;
}

bool file_exists(const std::string& path) {
  return access(path.c_str(), R_OK) == 0;
}

std::vector<std::string> split_colon_list(const char* s) {
   
  std::vector<std::string> out;
  if (!s) return out;
  std::string cur;
  for (const char* p = s; *p; p++) {
    if (*p == ':') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(*p);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

}
