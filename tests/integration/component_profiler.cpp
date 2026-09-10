#include "proc_monitor.h"
#include "timing.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <thread>
#include <algorithm>

namespace fs = std::filesystem;

struct ComponentProfile {
    std::string name;
    pid_t pid;
    int fd_count;
    uint64_t rss_kb;
    uint64_t pss_kb;
    uint64_t private_dirty_kb;
    uint64_t vm_size_kb;
    double cpu_pct;
    std::string state;
};

static std::string read_proc_state(pid_t pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (std::getline(f, line)) {
        auto pos = line.rfind(')');
        if (pos != std::string::npos) {
            std::istringstream iss(line.substr(pos + 2));
            char state;
            iss >> state;
            std::string states = "RSDTtXZWI";
            if (states.find(state) != std::string::npos) {
                switch (state) {
                    case 'R': return "running";
                    case 'S': return "sleeping";
                    case 'D': return "disk_sleep";
                    case 'T': return "stopped";
                    case 't': return "tracing_stop";
                    case 'Z': return "zombie";
                    default: return std::string(1, state);
                }
            }
        }
    }
    return "unknown";
}

static ComponentProfile profile_component(pid_t pid, const std::string& name, int sample_ms = 3000) {
    ComponentProfile p{};
    p.name = name;
    p.pid = pid;

    ProcCpuStats before = read_proc_cpu(pid);
    std::this_thread::sleep_for(std::chrono::milliseconds(sample_ms));
    ProcCpuStats after = read_proc_cpu(pid);

    p.cpu_pct = cpu_percent(before, after, sample_ms);

    ProcMemStats mem = read_proc_memory(pid);
    p.rss_kb = mem.rss_kb;
    p.pss_kb = mem.pss_kb;
    p.private_dirty_kb = mem.private_dirty_kb;
    p.vm_size_kb = mem.vm_size_kb;
    p.fd_count = count_fds(pid);
    p.state = read_proc_state(pid);

    return p;
}

static void discover_and_profile() {
    DIR* proc = opendir("/proc");
    if (!proc) { fprintf(stderr, "Cannot open /proc\n"); return; }

    std::vector<std::pair<pid_t, std::string>> found;
    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        if (!std::isdigit(entry->d_name[0])) continue;
        pid_t pid = std::stoi(entry->d_name);
        std::ifstream f("/proc/" + std::string(entry->d_name) + "/comm");
        std::string comm;
        std::getline(f, comm);

        if (comm.find("EventHorizon") != std::string::npos || comm.find("event-horizon") != std::string::npos)
            found.emplace_back(pid, "supervisor");
        else if (comm.find("eh-dock") != std::string::npos)
            found.emplace_back(pid, "dock");
        else if (comm.find("eh-taskbar") != std::string::npos)
            found.emplace_back(pid, "taskbar");
        else if (comm.find("eh-wallpaper") != std::string::npos)
            found.emplace_back(pid, "wallpaper");
        else if (comm.find("eh-desktop") != std::string::npos)
            found.emplace_back(pid, "desktop");
        else if (comm.find("eh-notif") != std::string::npos)
            found.emplace_back(pid, "notifications");
        else if (comm.find("eh-control") != std::string::npos)
            found.emplace_back(pid, "controlcenter");
        else if (comm.find("eh-clipboard") != std::string::npos)
            found.emplace_back(pid, "clipboard");
    }
    closedir(proc);

    if (found.empty()) {
        fprintf(stderr, "No EventHorizon processes found. Is the shell running?\n");
        return;
    }

    printf("Found %zu components. Profiling (3s each)...\n\n", found.size());

    std::vector<ComponentProfile> profiles;
    for (auto& [pid, name] : found) {
        printf("  Profiling %s (PID %d)...\n", name.c_str(), pid);
        profiles.push_back(profile_component(pid, name));
    }

    // Print table
    printf("\n=== Component Profiles ===\n\n");
    printf("%-15s %8s %8s %10s %10s %10s %8s %8s %8s\n",
        "Component", "PID", "State", "RSS", "PSS", "PrivDirty", "VM", "FDs", "CPU%");
    printf("%-15s %8s %8s %10s %10s %10s %8s %8s %8s\n",
        "─────────", "───", "─────", "───", "───", "─────────", "──", "───", "────");

    uint64_t total_rss = 0, total_pss = 0, total_vm = 0;
    int total_fd = 0;
    double total_cpu = 0;

    for (auto& p : profiles) {
        printf("%-15s %8d %8s %8luKB %8luKB %8luKB %8luKB %8d %7.2f%%\n",
            p.name.c_str(), p.pid, p.state.c_str(),
            p.rss_kb, p.pss_kb, p.private_dirty_kb,
            p.vm_size_kb, p.fd_count, p.cpu_pct);

        total_rss += p.rss_kb;
        total_pss += p.pss_kb;
        total_vm += p.vm_size_kb;
        total_fd += p.fd_count;
        total_cpu += p.cpu_pct;
    }

    printf("%-15s %8s %8s %8luKB %8luKB %8luKB %8luKB %8d %7.2f%%\n",
        "TOTAL", "", "",
        total_rss, total_pss, total_pss, total_vm, total_fd, total_cpu);

    // Alerts
    printf("\n=== Alerts ===\n");
    bool has_alerts = false;
    for (auto& p : profiles) {
        if (p.rss_kb > 200000) {
            printf("  [!] %s uses >200MB RSS (%luKB) — investigate memory usage\n", p.name.c_str(), p.rss_kb);
            has_alerts = true;
        }
        if (p.fd_count > 128) {
            printf("  [!] %s has >128 FDs (%d) — possible FD leak\n", p.name.c_str(), p.fd_count);
            has_alerts = true;
        }
        if (p.cpu_pct > 5.0) {
            printf("  [!] %s uses >5%% CPU while idle (%.2f%%) — unnecessary work\n", p.name.c_str(), p.cpu_pct);
            has_alerts = true;
        }
        if (p.private_dirty_kb > 50000) {
            printf("  [!] %s has >50MB private dirty memory (%luKB) — not shared with other processes\n", p.name.c_str(), p.private_dirty_kb);
            has_alerts = true;
        }
    }
    if (!has_alerts) {
        printf("  All components within normal thresholds.\n");
    }

    // JSON output
    std::string json_path = "tests/results/component_profiles.json";
    std::ofstream json(json_path);
    json << "{\n  \"components\": [\n";
    for (size_t i = 0; i < profiles.size(); ++i) {
        auto& p = profiles[i];
        json << "    {\"name\":\"" << p.name << "\",\"pid\":" << p.pid
             << ",\"state\":\"" << p.state << "\""
             << ",\"rss_kb\":" << p.rss_kb
             << ",\"pss_kb\":" << p.pss_kb
             << ",\"private_dirty_kb\":" << p.private_dirty_kb
             << ",\"vm_size_kb\":" << p.vm_size_kb
             << ",\"fd_count\":" << p.fd_count
             << ",\"cpu_pct\":" << p.cpu_pct << "}";
        if (i + 1 < profiles.size()) json << ",";
        json << "\n";
    }
    json << "  ],\n  \"totals\": {\n"
         << "    \"rss_kb\":" << total_rss << ",\n"
         << "    \"pss_kb\":" << total_pss << ",\n"
         << "    \"vm_size_kb\":" << total_vm << ",\n"
         << "    \"fd_count\":" << total_fd << ",\n"
         << "    \"cpu_pct\":" << total_cpu << "\n"
         << "  }\n}\n";
    json.close();
    printf("\nJSON saved to: %s\n", json_path.c_str());
}

int main() {
    printf("=== Event Horizon Shell Component Profiler ===\n\n");
    discover_and_profile();
    return 0;
}
