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
#include <numeric>

namespace fs = std::filesystem;

struct Sample {
    int64_t time_ms;
    pid_t pid;
    std::string component;
    uint64_t rss_kb;
    uint64_t pss_kb;
    uint64_t private_dirty_kb;
};

int main(int argc, char** argv) {
    int duration_s = argc > 1 ? atoi(argv[1]) : 60;
    int interval_ms = argc > 2 ? atoi(argv[2]) : 1000;

    printf("=== Event Horizon Shell Memory Monitor ===\n");
    printf("Duration: %ds  Interval: %dms\n\n", duration_s, interval_ms);

    // Find EventHorizon and children
    DIR* proc = opendir("/proc");
    if (!proc) { fprintf(stderr, "Cannot open /proc\n"); return 1; }

    std::vector<std::pair<pid_t, std::string>> components;
    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        if (!std::isdigit(entry->d_name[0])) continue;
        pid_t pid = std::stoi(entry->d_name);
        std::ifstream f("/proc/" + std::string(entry->d_name) + "/comm");
        std::string comm;
        std::getline(f, comm);
        if (comm.find("EventHorizon") != std::string::npos || comm.find("event-horizon") != std::string::npos) {
            components.emplace_back(pid, "supervisor");
        } else if (comm.find("eh-dock") != std::string::npos) {
            components.emplace_back(pid, "dock");
        } else if (comm.find("eh-taskbar") != std::string::npos) {
            components.emplace_back(pid, "taskbar");
        } else if (comm.find("eh-wallpaper") != std::string::npos) {
            components.emplace_back(pid, "wallpaper");
        } else if (comm.find("eh-desktop") != std::string::npos) {
            components.emplace_back(pid, "desktop");
        } else if (comm.find("eh-notif") != std::string::npos) {
            components.emplace_back(pid, "notifications");
        }
    }
    closedir(proc);

    if (components.empty()) {
        fprintf(stderr, "No EventHorizon processes found\n");
        return 1;
    }

    printf("Found %zu components:\n", components.size());
    for (auto& [pid, name] : components) {
        printf("  %s (PID %d)\n", name.c_str(), pid);
    }
    printf("\n");

    // CSV output
    std::string csv_path = argc > 3 ? argv[3] : "tests/results/memory_trace.csv";
    std::ofstream csv(csv_path);
    csv << "time_ms,component,pid,rss_kb,pss_kb,private_dirty_kb\n";

    // Collect samples
    std::vector<Sample> all_samples;
    int iterations = (duration_s * 1000) / interval_ms;

    printf("Sampling...\n");
    for (int i = 0; i < iterations; ++i) {
        int64_t t = i * interval_ms;
        for (auto& [pid, name] : components) {
            ProcMemStats mem = read_proc_memory(pid);
            Sample s{t, pid, name, mem.rss_kb, mem.pss_kb, mem.private_dirty_kb};
            all_samples.push_back(s);
            csv << t << "," << name << "," << pid << ","
                << mem.rss_kb << "," << mem.pss_kb << "," << mem.private_dirty_kb << "\n";
        }
        if (i % 10 == 0) {
            printf("  [%d/%d] %ldms elapsed\n", i + 1, iterations, t);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    csv.close();

    // Analyze per component
    printf("\n=== Memory Analysis ===\n");
    printf("%-15s %10s %10s %10s %10s %8s\n",
        "Component", "Start RSS", "End RSS", "Peak RSS", "Growth", "Status");
    printf("%-15s %10s %10s %10s %10s %8s\n",
        "─────────", "─────────", "───────", "────────", "──────", "──────");

    for (auto& [pid, name] : components) {
        std::vector<Sample*> comp_samples;
        for (auto& s : all_samples) {
            if (s.pid == pid) comp_samples.push_back(&s);
        }
        if (comp_samples.empty()) continue;

        uint64_t start_rss = comp_samples.front()->rss_kb;
        uint64_t end_rss = comp_samples.back()->rss_kb;
        uint64_t peak_rss = 0;
        for (auto* s : comp_samples) {
            peak_rss = std::max(peak_rss, s->rss_kb);
        }

        double growth_pct = start_rss > 0
            ? static_cast<double>(end_rss - start_rss) * 100.0 / start_rss
            : 0;

        const char* status = growth_pct > 5.0 ? "LEAK?" : "OK";

        printf("%-15s %8luKB %8luKB %8luKB %7.1f%% %8s\n",
            name.c_str(), start_rss, end_rss, peak_rss, growth_pct, status);
    }

    // Growth rate analysis
    printf("\n=== Growth Rate (last 10 samples) ===\n");
    for (auto& [pid, name] : components) {
        std::vector<Sample*> comp_samples;
        for (auto& s : all_samples) {
            if (s.pid == pid) comp_samples.push_back(&s);
        }
        if (comp_samples.size() < 10) continue;

        auto last_10 = std::vector<Sample*>(comp_samples.end() - 10, comp_samples.end());
        uint64_t first_rss = last_10.front()->rss_kb;
        uint64_t last_rss = last_10.back()->rss_kb;
        double dt_s = static_cast<double>(last_10.back()->time_ms - last_10.front()->time_ms) / 1000.0;
        double kb_per_sec = dt_s > 0 ? static_cast<double>(last_rss - first_rss) / dt_s : 0;

        printf("%s: %.1f KB/s (%s)\n", name.c_str(), kb_per_sec,
            kb_per_sec > 10 ? "POSSIBLE LEAK" : "stable");
    }

    printf("\nCSV saved to: %s\n", csv_path.c_str());
    return 0;
}
