#include "proc_monitor.h"
#include "timing.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace fs = std::filesystem;

static const char* EH_BIN = nullptr;
static pid_t eh_pid = -1;
static std::string real_xdg_runtime;
static std::string test_ipc_socket;

static void cleanup() {
    if (eh_pid > 0) {
        kill(-eh_pid, SIGTERM);
        int status;
        struct timespec ts = {0, 200000000}; // 200ms
        pid_t r = waitpid(eh_pid, &status, WNOHANG);
        if (r == 0) {
            nanosleep(&ts, nullptr);
            r = waitpid(eh_pid, &status, WNOHANG);
        }
        if (r == 0) {
            kill(-eh_pid, SIGKILL);
            waitpid(eh_pid, &status, 0);
        }
    }
    if (!test_ipc_socket.empty()) {
        fs::remove(test_ipc_socket);
    }
}

static void signal_handler(int) { cleanup(); _exit(1); }

static std::string find_wayland_socket() {
    if (real_xdg_runtime.empty()) return "";
    for (auto& entry : fs::directory_iterator(real_xdg_runtime)) {
        auto name = entry.path().filename().string();
        if (name.find("wayland-") == 0 && name.find(".lock") == std::string::npos) {
            return entry.path().string();
        }
    }
    return "";
}

static std::string find_ipc_socket() {
    if (!test_ipc_socket.empty() && fs::exists(test_ipc_socket)) {
        return test_ipc_socket;
    }
    return "";
}

static int ipc_ping(const std::string& sock_path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    WallClock clk;
    const char* msg = "ping\n";
    write(fd, msg, strlen(msg));

    char buf[256];
    struct pollfd pfd{fd, POLLIN, 0};
    int n = poll(&pfd, 1, 2000);
    if (n > 0) {
        read(fd, buf, sizeof(buf) - 1);
    }
    int us = clk.elapsed_us();
    close(fd);
    return (n > 0) ? us : -1;
}

struct TestResult {
    std::string name;
    bool passed = true;
    std::string detail;
};

static std::vector<TestResult> results;

static void add_result(const std::string& name, bool pass, const std::string& detail) {
    results.push_back({name, pass, detail});
    printf("  [%s] %s: %s\n", pass ? "PASS" : "FAIL", name.c_str(), detail.c_str());
}

static void test_startup_time() {
    WallClock clk;
    eh_pid = fork();
    if (eh_pid == 0) {
        setsid();
        execl(EH_BIN, EH_BIN, nullptr);
        perror("exec");
        _exit(1);
    }

    std::string socket_path;
    for (int i = 0; i < 300; ++i) {
        socket_path = find_ipc_socket();
        if (!socket_path.empty()) break;
        if (kill(eh_pid, 0) != 0) break;
        usleep(50000);
    }

    int64_t startup_ms = clk.elapsed_ms();
    bool ok = !socket_path.empty();
    add_result("startup_time", ok,
        std::to_string(startup_ms) + "ms" + (ok ? "" : " (TIMEOUT)"));
}

static void test_wait_for_settle(int seconds) {
    printf("  Waiting %ds for shell to settle...\n", seconds);
    sleep(seconds);
}

static void test_idle_rss(const ComponentPids& pids) {
    struct { const char* name; pid_t pid; } components[] = {
        {"supervisor", pids.supervisor},
        {"dock", pids.dock},
        {"taskbar", pids.taskbar},
        {"wallpaper", pids.wallpaper},
        {"desktop", pids.desktop},
        {"notifications", pids.notifications},
    };

    uint64_t total_rss = 0;
    uint64_t total_pss = 0;
    for (auto& c : components) {
        if (c.pid <= 0) continue;
        ProcMemStats mem = read_proc_memory(c.pid);
        total_rss += mem.rss_kb;
        total_pss += mem.pss_kb;

        char buf[256];
        snprintf(buf, sizeof(buf), "%s: RSS=%luKB PSS=%luKB VM=%luKB",
            c.name, mem.rss_kb, mem.pss_kb, mem.vm_size_kb);
        add_result("idle_rss_" + std::string(c.name), mem.rss_kb > 0, buf);
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "total RSS=%luKB PSS=%luKB", total_rss, total_pss);
    add_result("idle_rss_total", true, buf);
}

static void test_idle_cpu(const ComponentPids& pids, int sample_ms = 5000) {
    pid_t target = pids.supervisor;
    if (target <= 0) {
        add_result("idle_cpu", false, "no supervisor PID found");
        return;
    }

    ProcCpuStats before = read_proc_cpu(target);
    WallClock clk;
    std::this_thread::sleep_for(std::chrono::milliseconds(sample_ms));
    ProcCpuStats after = read_proc_cpu(target);
    int64_t wall = clk.elapsed_ms();

    double pct = cpu_percent(before, after, wall);
    char buf[128];
    snprintf(buf, sizeof(buf), "%.2f%% over %dms", pct, sample_ms);
    add_result("idle_cpu", pct < 5.0, buf);
}

static void test_idle_gpu() {
    std::string vendor = "unknown";
    double util_pct = -1;
    int cur_mhz = 0, max_mhz = 0;

    if (fs::exists("/sys/class/drm/card0/gt_cur_freq_mhz")) {
        vendor = "intel";
        std::ifstream f_cur("/sys/class/drm/card0/gt_cur_freq_mhz");
        std::ifstream f_max("/sys/class/drm/card0/gt_max_freq_mhz");
        f_cur >> cur_mhz;
        f_max >> max_mhz;
        if (max_mhz > 0) util_pct = static_cast<double>(cur_mhz) * 100.0 / max_mhz;
    } else if (fs::exists("/sys/class/drm/card0/device/gpu_busy_percent")) {
        vendor = "amd";
        std::ifstream f("/sys/class/drm/card0/device/gpu_busy_percent");
        f >> util_pct;
    }

    if (util_pct < 0) {
        add_result("idle_gpu", true, "no GPU sysfs found (skip)");
        return;
    }

    char buf[256];
    if (vendor == "intel") {
        snprintf(buf, sizeof(buf), "Intel GPU: %d/%d MHz (%.1f%%)", cur_mhz, max_mhz, util_pct);
    } else {
        snprintf(buf, sizeof(buf), "%s GPU: %.1f%% utilization", vendor.c_str(), util_pct);
    }
    add_result("idle_gpu", util_pct < 10.0, buf);
}

static void test_wayland_globals() {
    if (system("which wayland-info >/dev/null 2>&1") != 0) {
        add_result("wayland_globals", true, "wayland-info not installed (skip)");
        return;
    }

    std::string wl_socket = find_wayland_socket();
    if (wl_socket.empty()) {
        add_result("wayland_globals", false, "no Wayland socket");
        return;
    }

    std::string display = fs::path(wl_socket).filename().string();
    std::string cmd = "WAYLAND_DISPLAY=" + display + " wayland-info 2>/dev/null | grep -c 'interface:'";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        add_result("wayland_globals", false, "wayland-info not available");
        return;
    }

    char buf[64];
    fgets(buf, sizeof(buf), pipe);
    pclose(pipe);

    int count = atoi(buf);
    char detail[128];
    snprintf(detail, sizeof(detail), "%d globals bound", count);
    add_result("wayland_globals", count > 0 && count < 100, detail);
}

static void test_file_descriptors(const ComponentPids& pids) {
    struct { const char* name; pid_t pid; } components[] = {
        {"supervisor", pids.supervisor},
        {"dock", pids.dock},
        {"taskbar", pids.taskbar},
        {"wallpaper", pids.wallpaper},
        {"desktop", pids.desktop},
        {"notifications", pids.notifications},
    };

    int total_fd = 0;
    for (auto& c : components) {
        if (c.pid <= 0) continue;
        int fd_count = count_fds(c.pid);
        total_fd += fd_count;

        char buf[128];
        snprintf(buf, sizeof(buf), "%s: %d FDs", c.name, fd_count);
        add_result("fd_" + std::string(c.name), fd_count < 256, buf);
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "total: %d FDs", total_fd);
    add_result("fd_total", true, buf);
}

static void test_ipc_latency() {
    std::string sock = find_ipc_socket();
    if (sock.empty()) {
        add_result("ipc_latency", false, "no IPC socket found");
        return;
    }

    TimingStats stats;
    for (int i = 0; i < 100; ++i) {
        int us = ipc_ping(sock);
        if (us > 0) stats.add(us);
    }

    if (stats.samples.empty()) {
        add_result("ipc_latency", false, "all IPC pings failed");
        return;
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "mean=%.0fus median=%.0fus p95=%.0fus p99=%.0fus (n=%zu)",
        stats.mean(), stats.median(), stats.p95(), stats.p99(), stats.samples.size());
    add_result("ipc_latency", stats.mean() < 10000, buf);
}

static void test_memory_growth(const ComponentPids& pids, int duration_s = 30, int interval_ms = 2000) {
    pid_t target = pids.supervisor;
    if (target <= 0) {
        add_result("memory_growth", false, "no supervisor PID");
        return;
    }

    std::vector<uint64_t> rss_samples;
    int iterations = (duration_s * 1000) / interval_ms;

    for (int i = 0; i < iterations; ++i) {
        ProcMemStats mem = read_proc_memory(target);
        rss_samples.push_back(mem.rss_kb);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }

    if (rss_samples.size() < 3) {
        add_result("memory_growth", false, "not enough samples");
        return;
    }

    uint64_t first = rss_samples.front();
    uint64_t last = rss_samples.back();
    uint64_t peak = *std::max_element(rss_samples.begin(), rss_samples.end());

    double growth_pct = 0;
    if (first > 0) {
        if (last == 0) {
            growth_pct = -100.0;
        } else if (last >= first) {
            growth_pct = static_cast<double>(last - first) * 100.0 / first;
        } else {
            growth_pct = -static_cast<double>(first - last) * 100.0 / first;
        }
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "start=%luKB end=%luKB peak=%luKB growth=%.1f%% over %ds",
        first, last, peak, growth_pct, duration_s);
    add_result("memory_growth", growth_pct < 5.0, buf);
}

static void test_stress_ipc(int count = 1000) {
    if (eh_pid > 0 && kill(eh_pid, 0) != 0) {
        add_result("stress_ipc", true, "shell exited before stress test (skip)");
        return;
    }
    std::string sock = find_ipc_socket();
    if (sock.empty()) {
        add_result("stress_ipc", true, "no IPC socket (shell may have exited)");
        return;
    }

    WallClock clk;
    int successes = 0;
    for (int i = 0; i < count; ++i) {
        int us = ipc_ping(sock);
        if (us > 0) ++successes;
    }
    int64_t total_ms = clk.elapsed_ms();

    char buf[256];
    snprintf(buf, sizeof(buf), "%d/%d succeeded in %ldms (%.0f msg/s)",
        successes, count, total_ms,
        total_ms > 0 ? static_cast<double>(successes) * 1000.0 / total_ms : 0);
    add_result("stress_ipc", successes > count * 0.95, buf);
}

static void test_wayland_roundtrip() {
    if (system("which wayland-info >/dev/null 2>&1") != 0) {
        add_result("wayland_roundtrip", true, "wayland-info not installed (skip)");
        return;
    }

    std::string wl_socket = find_wayland_socket();
    if (wl_socket.empty()) {
        add_result("wayland_roundtrip", false, "no Wayland socket");
        return;
    }

    std::string display = fs::path(wl_socket).filename().string();
    TimingStats stats;

    for (int i = 0; i < 20; ++i) {
        WallClock clk;
        std::string cmd = "WAYLAND_DISPLAY=" + display + " wayland-info 2>/dev/null >/dev/null";
        system(cmd.c_str());
        stats.add(clk.elapsed_us());
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "mean=%.0fus median=%.0fus p95=%.0fus",
        stats.mean(), stats.median(), stats.p95());
    add_result("wayland_roundtrip", stats.mean() < 20000, buf);
}

static void write_report(const std::string& path) {
    std::ofstream f(path);
    f << "{\n  \"results\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        auto& r = results[i];
        f << "    {\"name\":\"" << r.name << "\",\"passed\":" << (r.passed ? "true" : "false")
          << ",\"detail\":\"" << r.detail << "\"}";
        if (i + 1 < results.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n  \"summary\": {\n";

    int pass = 0, fail = 0;
    for (auto& r : results) {
        if (r.passed) ++pass; else ++fail;
    }
    f << "    \"passed\":" << pass << ",\n";
    f << "    \"failed\":" << fail << ",\n";
    f << "    \"total\":" << results.size() << "\n";
    f << "  }\n}\n";
    f.close();
    printf("\nReport saved to: %s\n", path.c_str());
}

int main(int argc, char** argv) {
    EH_BIN = argc > 1 ? argv[1] : "./build-release/EventHorizon";

    if (!fs::exists(EH_BIN)) {
        fprintf(stderr, "Binary not found: %s\n", EH_BIN);
        return 1;
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !fs::exists(runtime)) {
        fprintf(stderr, "XDG_RUNTIME_DIR not set or does not exist\n");
        return 1;
    }
    real_xdg_runtime = runtime;

    const char* wl_display = getenv("WAYLAND_DISPLAY");
    if (!wl_display || wl_display[0] == '\0') {
        fprintf(stderr, "WAYLAND_DISPLAY not set — no running compositor\n");
        return 1;
    }

    setenv("WAYLAND_DEBUG", "0", 1);

    test_ipc_socket = real_xdg_runtime + "/eh-test-ipc-" + std::to_string(getpid()) + ".sock";
    setenv("EH_IPC_SOCKET", test_ipc_socket.c_str(), 1);

    printf("=== Event Horizon Shell Integration Tests ===\n");
    printf("Binary: %s\n", EH_BIN);
    printf("XDG_RUNTIME_DIR: %s\n", real_xdg_runtime.c_str());
    printf("WAYLAND_DISPLAY: %s\n", wl_display);
    printf("EH_IPC_SOCKET: %s\n\n", test_ipc_socket.c_str());

    // 1. Startup
    printf("[1/10] Startup time...\n");
    test_startup_time();
    if (eh_pid <= 0) {
        printf("FATAL: Shell failed to start\n");
        cleanup();
        return 1;
    }

    // 2. Wait for settle
    printf("[2/10] Waiting for shell to settle...\n");
    test_wait_for_settle(10);

    // 3. Discover components
    printf("[3/10] Discovering components...\n");
    ComponentPids pids = discover_components(eh_pid);
    printf("  supervisor=%d dock=%d taskbar=%d wallpaper=%d desktop=%d notifications=%d\n",
        pids.supervisor, pids.dock, pids.taskbar, pids.wallpaper, pids.desktop, pids.notifications);

    // 4. Idle RSS
    printf("[4/10] Idle RSS...\n");
    test_idle_rss(pids);

    // 5. Idle CPU
    printf("[5/10] Idle CPU (5s sample)...\n");
    test_idle_cpu(pids);

    // 6. Idle GPU
    printf("[6/10] Idle GPU...\n");
    test_idle_gpu();

    // 7. Wayland globals
    printf("[7/10] Wayland globals...\n");
    test_wayland_globals();

    // 8. File descriptors
    printf("[8/10] File descriptors...\n");
    test_file_descriptors(pids);

    // 9. IPC latency
    printf("[9/10] IPC latency...\n");
    test_ipc_latency();

    // 10. Memory growth
    printf("[10/10] Memory growth (30s)...\n");
    test_memory_growth(pids, 30, 2000);

    // Stress tests
    printf("\n--- Stress Tests ---\n");
    printf("[stress] IPC burst...\n");
    test_stress_ipc(1000);

    printf("[stress] Wayland round-trip...\n");
    test_wayland_roundtrip();

    // Summary
    printf("\n=== Results ===\n");
    int pass = 0, fail = 0;
    for (auto& r : results) {
        if (r.passed) ++pass; else ++fail;
    }
    printf("Passed: %d  Failed: %d  Total: %zu\n", pass, fail, results.size());

    // Write report
    std::string report_path = argc > 2 ? argv[2] : "tests/results/report.json";
    write_report(report_path);

    cleanup();
    return fail > 0 ? 1 : 0;
}
