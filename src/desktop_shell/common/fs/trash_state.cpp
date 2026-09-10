#include "desktop_shell/common/fs/trash_state.hpp"

#include <dirent.h>
#include <cstdlib>
#include <string>

namespace eh::shell::fs {

bool trash_has_files() {
  const char* home = std::getenv("HOME");
  if (!home) return false;
  const std::string trashPath = std::string(home) + "/.local/share/Trash/files";
  DIR* d = opendir(trashPath.c_str());
  if (!d) return false;
  bool hasFiles = false;
  struct dirent* de;
  while ((de = readdir(d)) != nullptr) {
    const std::string name(de->d_name);
    if (name != "." && name != "..") { hasFiles = true; break; }
  }
  closedir(d);
  return hasFiles;
}

}
