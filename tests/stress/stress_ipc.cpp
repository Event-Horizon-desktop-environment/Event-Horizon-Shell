#include "proc_monitor.h"
#include "timing.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace fs = std::filesystem;

static std::string find_ipc_socket() {
    const char* xdg = getenv("XDG_RUNTIME_DIR");
    if (!xdg) return "";
    for (auto& entry : fs::directory_iterator(xdg)) {
        auto name = entry.path().filename().string();
        if (name.find("eh-") == 0 || name.find("event-horizon") == 0) {
            return entry.path().string();
        }
    }
    return "";
}

static int ipc_connect(const std::string& sock_path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool ipc_ping_fd(int fd) {
    const char* msg = "ping\n";
    if (write(fd, msg, strlen(msg)) < 0) return false;
    char buf[256];
    struct pollfd pfd{fd, POLLIN, 0};
    if (poll(&pfd, 1, 2000) > 0) {
        read(fd, buf, sizeof(buf) - 1);
        return true;
    }
    return false;
}

struct StressResult {
    std::string name;
    int64_t duration_ms;
    int operations;
    int successes;
    double ops_per_sec;
    double avg_latency_us;
    uint64_t rss_before_kb;
    uint64_t rss_after_kb;
    double cpu_before_pct;
    double cpu_after_pct;
};

static StressResult run_stress(const std::string& name,
    pid_t supervisor_pid, int operations, std::function<bool(int)> op)
{
    StressResult r{};
    r.name = name;
    r.operations = operations;

    ProcMemStats mem_before = read_proc_memory(supervisor_pid);
    r.rss_before_kb = mem_before.rss_kb;
    ProcCpuStats cpu_before = read_proc_cpu(supervisor_pid);

    TimingStats latency;
    WallClock total_clk;
    int successes = 0;

    for (int i = 0; i < operations; ++i) {
        WallClock op_clk;
        if (op(i)) {
            latency.add(op_clk.elapsed_us());
            ++successes;
        }
    }

    r.duration_ms = total_clk.elapsed_ms();
    r.successes = successes;
    r.ops_per_sec = r.duration_ms > 0 ? static_cast<double>(successes) * 1000.0 / r.duration_ms : 0;
    r.avg_latency_us = latency.mean();

    ProcCpuStats cpu_after = read_proc_cpu(supervisor_pid);
    r.cpu_before_pct = cpu_percent(cpu_before, cpu_before, 1);
    r.cpu_after_pct = cpu_percent(cpu_before, cpu_after, r.duration_ms);

    ProcMemStats mem_after = read_proc_memory(supervisor_pid);
    r.rss_after_kb = mem_after.rss_kb;

    return r;
}

int main() {
    printf("=== Event Horizon Shell Stress Test ===\n\n");

    std::string sock_path = find_ipc_socket();
    if (sock_path.empty()) {
        fprintf(stderr, "No IPC socket found. Is EventHorizon running?\n");
        fprintf(stderr, "Set XDG_RUNTIME_DIR if running in a custom environment.\n");
        return 1;
    }
    printf("IPC socket: %s\n\n", sock_path.c_str());

    pid_t supervisor_pid = 0;
    DIR* proc = opendir("/proc");
    if (proc) {
        struct dirent* entry;
        while ((entry = readdir(proc)) != nullptr) {
            if (!std::isdigit(entry->d_name[0])) continue;
            std::ifstream f("/proc/" + std::string(entry->d_name) + "/comm");
            std::string comm;
            std::getline(f, comm);
            if (comm.find("EventHorizon") != std::string::npos || comm.find("event-horizon") != std::string::npos) {
                supervisor_pid = std::stoi(entry->d_name);
                break;
            }
        }
        closedir(proc);
    }

    if (supervisor_pid <= 0) {
        fprintf(stderr, "Could not find EventHorizon supervisor PID\n");
        return 1;
    }
    printf("Supervisor PID: %d\n\n", supervisor_pid);

    std::vector<StressResult> all_results;

    // Test 1: IPC throughput — serial
    printf("[1/5] IPC throughput (serial, 1000 pings)...\n");
    all_results.push_back(run_stress("ipc_serial", supervisor_pid, 1000,
        [&](int) {
            int fd = ipc_connect(sock_path);
            if (fd < 0) return false;
            bool ok = ipc_ping_fd(fd);
            close(fd);
            return ok;
        }));

    // Test 2: IPC throughput — parallel
    printf("[2/5] IPC throughput (parallel, 2000 pings, 10 threads)...\n");
    {
        std::atomic<int> successes{0};
        WallClock clk;
        std::vector<std::thread> threads;
        for (int t = 0; t < 10; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < 200; ++i) {
                    int fd = ipc_connect(sock_path);
                    if (fd < 0) continue;
                    if (ipc_ping_fd(fd)) successes.fetch_add(1);
                    close(fd);
                }
            });
        }
        for (auto& t : threads) t.join();
        int64_t ms = clk.elapsed_ms();
        StressResult r{};
        r.name = "ipc_parallel";
        r.operations = 2000;
        r.successes = successes.load();
        r.duration_ms = ms;
        r.ops_per_sec = ms > 0 ? static_cast<double>(r.successes) * 1000.0 / ms : 0;
        printf("  %d/%d succeeded in %ldms\n", r.successes, r.operations, r.duration_ms);
        all_results.push_back(r);
    }

    // Test 3: Rapid reconnects
    printf("[3/5] Rapid reconnect (500 connect/disconnect cycles)...\n");
    all_results.push_back(run_stress("rapid_reconnect", supervisor_pid, 500,
        [&](int) {
            int fd = ipc_connect(sock_path);
            if (fd < 0) return false;
            close(fd);
            return true;
        }));

    // Test 4: Message burst
    printf("[4/5] Message burst (single connection, 5000 messages)...\n");
    all_results.push_back(run_stress("message_burst", supervisor_pid, 5000,
        [&](int) {
            int fd = ipc_connect(sock_path);
            if (fd < 0) return false;
            const char* msg = "workspace next\n";
            bool ok = write(fd, msg, strlen(msg)) > 0;
            close(fd);
            return ok;
        }));

    // Test 5: Config reload storm
    printf("[5/5] Config reload storm (100 reloads)...\n");
    all_results.push_back(run_stress("config_reload", supervisor_pid, 100,
        [&](int) {
            int fd = ipc_connect(sock_path);
            if (fd < 0) return false;
            const char* msg = "config reload\n";
            bool ok = write(fd, msg, strlen(msg)) > 0;
            if (ok) {
                char buf[256];
                struct pollfd pfd{fd, POLLIN, 0};
                poll(&pfd, 1, 3000);
                read(fd, buf, sizeof(buf) - 1);
            }
            close(fd);
            return ok;
        }));

    // Print results
    printf("\n=== Stress Test Results ===\n");
    printf("%-20s %8s %8s %10s %10s %12s\n",
        "Test", "Ops", "OK", "Duration", "Ops/sec", "Avg Latency");
    printf("%-20s %8s %8s %10s %10s %12s\n",
        "────", "───", "──", "────────", "───────", "───────────");

    for (auto& r : all_results) {
        printf("%-20s %8d %8d %7ldms %9.0f %9.0fus\n",
            r.name.c_str(), r.operations, r.successes,
            r.duration_ms, r.ops_per_sec, r.avg_latency_us);
    }

    printf("\n--- Memory Impact ---\n");
    for (auto& r : all_results) {
        if (r.rss_before_kb > 0) {
            int64_t delta = static_cast<int64_t>(r.rss_after_kb) - static_cast<int64_t>(r.rss_before_kb);
            printf("%s: %luKB -> %luKB (%+ldKB)\n",
                r.name.c_str(), r.rss_before_kb, r.rss_after_kb, delta);
        }
    }

    printf("\n--- CPU Impact ---\n");
    for (auto& r : all_results) {
        if (r.cpu_after_pct > 0) {
            printf("%s: CPU %.1f%%\n", r.name.c_str(), r.cpu_after_pct);
        }
    }

    return 0;
}
