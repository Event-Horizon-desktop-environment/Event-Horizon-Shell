#include "common/file_utils/trash.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace hpv {

static std::string trash_dir() {
    const char* data_home = getenv("XDG_DATA_HOME");
    if (data_home && data_home[0]) {
        return std::string(data_home) + "/Trash";
    }
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.local/share/Trash";
    }
    return "/tmp/Trash";
}

static bool ensure_dir(const std::string& dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        return true;
    if (mkdir(dir.c_str(), 0700) == 0)
        return true;
    std::cerr << "trash: failed to create " << dir << ": " << strerror(errno) << "\n";
    return false;
}

static std::string unique_name(const std::string& trash_files, const std::string& basename) {
    std::string candidate = basename;
    int attempt = 1;
    while (true) {
        struct stat st;
        std::string full = trash_files + "/" + candidate;
        if (stat(full.c_str(), &st) != 0)
            return candidate;
        // Append counter before extension
        auto dot = basename.rfind('.');
        if (dot == std::string::npos) {
            candidate = basename + " (" + std::to_string(attempt) + ")";
        } else {
            candidate = basename.substr(0, dot) + " (" + std::to_string(attempt) + ")" + basename.substr(dot);
        }
        attempt++;
    }
}

bool trash_file(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        std::cerr << "trash: cannot stat " << path << ": " << strerror(errno) << "\n";
        return false;
    }

    std::string base = trash_dir();
    std::string files_dir = base + "/files";
    std::string info_dir = base + "/info";
    if (!ensure_dir(files_dir) || !ensure_dir(info_dir))
        return false;

    // Extract filename from path
    std::string filename = path;
    auto slash = path.rfind('/');
    if (slash != std::string::npos)
        filename = path.substr(slash + 1);

    std::string unique = unique_name(files_dir, filename);
    std::string dest = files_dir + "/" + unique;

    if (rename(path.c_str(), dest.c_str()) != 0) {
        std::cerr << "trash: rename " << path << " -> " << dest << " failed: "
                   << strerror(errno) << "\n";
        return false;
    }

    // Write .trashinfo file
    std::ostringstream date_str;
    std::time_t now = std::time(nullptr);
    char buf[64];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&now))) {
        date_str << buf;
    }

    std::string info_path = info_dir + "/" + unique + ".trashinfo";
    std::ofstream info(info_path);
    if (!info) {
        std::cerr << "trash: failed to write " << info_path << "\n";
        return false;
    }
    info << "[Trash Info]\n";
    info << "Path=" << path << "\n";
    info << "DeletionDate=" << date_str.str() << "\n";

    std::cout << "trash: moved " << path << " -> " << dest << "\n";
    return true;
}

} // namespace hpv
