#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <dirent.h>

struct ProcMemStats {
    uint64_t rss_kb = 0;
    uint64_t pss_kb = 0;
    uint64_t private_dirty_kb = 0;
    uint64_t vm_size_kb = 0;
};

struct ProcCpuStats {
    uint64_t utime_ticks = 0;
    uint64_t stime_ticks = 0;
    uint64_t total_ticks() const { return utime_ticks + stime_ticks; }
};

inline uint64_t parse_field(const std::string& line, const std::string& prefix) {
    if (line.compare(0, prefix.size(), prefix) == 0) {
        std::istringstream iss(line.substr(prefix.size()));
        uint64_t val = 0;
        iss >> val;
        return val;
    }
    return 0;
}

inline ProcMemStats read_proc_memory(pid_t pid) {
    ProcMemStats s;
    {
        std::ifstream f("/proc/" + std::to_string(pid) + "/status");
        std::string line;
        while (std::getline(f, line)) {
            s.vm_size_kb = std::max(s.vm_size_kb, parse_field(line, "VmSize:\t"));
            s.rss_kb = std::max(s.rss_kb, parse_field(line, "VmRSS:\t"));
        }
    }
    {
        std::ifstream f("/proc/" + std::to_string(pid) + "/smaps_rollup");
        std::string line;
        while (std::getline(f, line)) {
            s.pss_kb = std::max(s.pss_kb, parse_field(line, "Pss:\t"));
            s.private_dirty_kb = std::max(s.private_dirty_kb, parse_field(line, "Private_Dirty:\t"));
        }
    }
    return s;
}

inline ProcCpuStats read_proc_cpu(pid_t pid) {
    ProcCpuStats s;
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (std::getline(f, line)) {
        auto pos = line.rfind(')');
        if (pos != std::string::npos) {
            std::istringstream iss(line.substr(pos + 2));
            uint64_t dummy;
            for (int i = 0; i < 11; ++i) iss >> dummy;
            iss >> s.utime_ticks >> s.stime_ticks;
        }
    }
    return s;
}

inline long get_clk_tck() {
    static long clk = sysconf(_SC_CLK_TCK);
    return clk;
}

inline double cpu_percent(const ProcCpuStats& before, const ProcCpuStats& after, int64_t wall_ms) {
    if (wall_ms <= 0) return 0.0;
    int64_t ticks = static_cast<int64_t>(after.total_ticks() - before.total_ticks());
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(get_clk_tck()) * 100.0 / static_cast<double>(wall_ms);
}

inline int count_fds(pid_t pid) {
    DIR* d = opendir(("/proc/" + std::to_string(pid) + "/fd").c_str());
    if (!d) return 0;
    int count = 0;
    while (readdir(d)) ++count;
    closedir(d);
    return count - 2;
}

inline std::vector<pid_t> find_child_pids(pid_t parent) {
    std::vector<pid_t> children;
    std::ifstream ps("/proc");
    std::string entry;
    while (ps >> entry) {
        if (entry.empty() || !std::isdigit(entry[0])) continue;
        pid_t pid = std::stoi(entry);
        std::ifstream stat("/proc/" + entry + "/stat");
        std::string line;
        if (std::getline(stat, line)) {
            auto ppos = line.find('(');
            auto rpos = line.rfind(')');
            if (ppos != std::string::npos && rpos != std::string::npos) {
                std::istringstream iss(line.substr(rpos + 2));
                int ppid;
                iss >> ppid;
                if (ppid == parent) children.push_back(pid);
            }
        }
    }
    return children;
}

inline std::string read_proc_comm(pid_t pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/comm");
    std::string comm;
    std::getline(f, comm);
    return comm;
}

struct ComponentPids {
    pid_t supervisor = 0;
    pid_t dock = 0;
    pid_t taskbar = 0;
    pid_t wallpaper = 0;
    pid_t desktop = 0;
    pid_t notifications = 0;
};

inline ComponentPids discover_components(pid_t root_pid = 0) {
    ComponentPids pids;

    std::vector<pid_t> descendants;
    if (root_pid > 0) {
        descendants.push_back(root_pid);
        std::vector<pid_t> frontier = {root_pid};
        while (!frontier.empty()) {
            pid_t cur = frontier.back();
            frontier.pop_back();
            auto children = find_child_pids(cur);
            for (pid_t c : children) {
                descendants.push_back(c);
                frontier.push_back(c);
            }
        }
    }

    DIR* proc = opendir("/proc");
    if (!proc) return pids;
    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        if (!std::isdigit(entry->d_name[0])) continue;
        pid_t pid = std::stoi(entry->d_name);

        if (root_pid > 0) {
            bool is_descendant = false;
            for (pid_t d : descendants) {
                if (d == pid) { is_descendant = true; break; }
            }
            if (!is_descendant) continue;
        }

        std::string comm = read_proc_comm(pid);
        if (pids.supervisor == 0 && (comm.find("EventHorizon") != std::string::npos || comm.find("event-horizon") != std::string::npos)) {
            pids.supervisor = pid;
        } else if (pids.dock == 0 && (comm.find("eh-dock") != std::string::npos || comm.find("horizon-dock") != std::string::npos)) {
            pids.dock = pid;
        } else if (pids.taskbar == 0 && (comm.find("eh-taskbar") != std::string::npos || comm.find("horizon-taskbar") != std::string::npos)) {
            pids.taskbar = pid;
        } else if (pids.wallpaper == 0 && (comm.find("eh-wallpaper") != std::string::npos || comm.find("horizon-wallpaper") != std::string::npos)) {
            pids.wallpaper = pid;
        } else if (pids.desktop == 0 && (comm.find("eh-desktop") != std::string::npos || comm.find("horizon-desktop") != std::string::npos)) {
            pids.desktop = pid;
        } else if (pids.notifications == 0 && (comm.find("eh-notif") != std::string::npos || comm.find("horizon-notif") != std::string::npos)) {
            pids.notifications = pid;
        }
    }
    closedir(proc);
    return pids;
}
