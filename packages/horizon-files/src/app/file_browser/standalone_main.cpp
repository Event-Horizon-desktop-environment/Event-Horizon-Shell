#include "app/file_browser/embed/embed.hpp"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--select-directory") == 0) {
    std::string path;
    int ret = eh::file_browser::run_select_directory(path);
    if (ret == 0 && !path.empty()) {
      std::printf("%s\n", path.c_str());
      std::fflush(stdout);
    }
    return ret;
  }
  if (argc > 1 && std::strcmp(argv[1], "--select-file") == 0) {
    std::string path;
    int ret = eh::file_browser::run_select_file(path);
    if (ret == 0 && !path.empty()) {
      std::printf("%s\n", path.c_str());
      std::fflush(stdout);
    }
    return ret;
  }
  return eh::file_browser::run_standalone();
}