#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <poll.h>
#include <vector>
#include <thread>
#include <filesystem>
#include "helpers/timing.h"

namespace fs = std::filesystem;

static std::string wm_sock;

static void setup_echo_server() {
    char tmpl[] = "/tmp/eh-bench-wm-XXXXXX";
    char* dir = mkdtemp(tmpl);
    wm_sock = std::string(dir) + "/wm.sock";

    int srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, wm_sock.c_str(), sizeof(addr.sun_path) - 1);
    bind(srv_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(srv_fd, 128);

    std::thread([srv_fd]() {
        while (true) {
            int client = accept(srv_fd, nullptr, nullptr);
            if (client < 0) break;
            std::thread([client]() {
                char buf[256];
                while (true) {
                    ssize_t n = read(client, buf, sizeof(buf) - 1);
                    if (n <= 0) break;
                    buf[n] = '\0';
                    write(client, buf, n);
                }
                close(client);
            }).detach();
        }
    }).detach();
}

static int wm_connect() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, wm_sock.c_str(), sizeof(addr.sun_path) - 1);
    ::connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    return fd;
}

static void wm_send_recv(int fd, const char* msg) {
    write(fd, msg, strlen(msg));
    char buf[256];
    struct pollfd pfd{fd, POLLIN, 0};
    poll(&pfd, 1, 100);
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    (void)n;
}

static void bench(const char* name, int iters, std::function<void()> fn) {
    WallClock wall;
    for (int i = 0; i < iters; ++i) fn();
    double total_ms = wall.elapsed_sec() * 1000.0;
    double per_us = (total_ms * 1000.0) / iters;
    printf("  %-35s %8d iters  %8.1f ms total  %8.1f us/iter\n", name, iters, total_ms, per_us);
}

int main() {
    printf("=== Window Lifecycle Benchmarks ===\n\n");
    setup_echo_server();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    bench("window_lifecycle", 10000, []() {
        int fd = wm_connect();
        wm_send_recv(fd, "window create\n");
        wm_send_recv(fd, "window destroy\n");
        close(fd);
    });

    bench("window_move", 10000, []() {
        int fd = wm_connect();
        wm_send_recv(fd, "window create\n");
        for (int i = 0; i < 100; ++i) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "window move %d %d\n", i % 1920, i % 1080);
            wm_send_recv(fd, cmd);
        }
        wm_send_recv(fd, "window destroy\n");
        close(fd);
    });

    bench("window_burst_50", 500, []() {
        std::vector<int> fds;
        for (int i = 0; i < 50; ++i) {
            int fd = wm_connect();
            fds.push_back(fd);
            wm_send_recv(fd, "window create\n");
        }
        for (int fd : fds) {
            wm_send_recv(fd, "window destroy\n");
            close(fd);
        }
    });

    printf("\nDone.\n");
    fs::remove_all(fs::path(wm_sock).parent_path());
    return 0;
}
