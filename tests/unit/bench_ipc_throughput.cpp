#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <filesystem>
#include <thread>
#include "helpers/timing.h"

namespace fs = std::filesystem;

static std::string ipc_sock;

static void echo_server() {
    char tmpl[] = "/tmp/eh-bench-ipc-XXXXXX";
    char* dir = mkdtemp(tmpl);
    ipc_sock = std::string(dir) + "/bench.sock";
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ipc_sock.c_str(), sizeof(addr.sun_path) - 1);
    bind(srv, (struct sockaddr*)&addr, sizeof(addr));
    listen(srv, 16);
    std::thread([srv]() {
        while (true) {
            int c = accept(srv, nullptr, nullptr);
            if (c < 0) break;
            std::thread([c]() {
                char buf[256];
                while (true) {
                    ssize_t n = read(c, buf, sizeof(buf) - 1);
                    if (n <= 0) break;
                    buf[n] = '\0';
                    write(c, buf, n);
                }
                close(c);
            }).detach();
        }
    }).detach();
}

static int ipc_connect() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ipc_sock.c_str(), sizeof(addr.sun_path) - 1);
    ::connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    return fd;
}

static bool ipc_ping(int fd) {
    write(fd, "ping\n", 5);
    char buf[256];
    struct pollfd pfd{fd, POLLIN, 0};
    if (poll(&pfd, 1, 1000) > 0) { read(fd, buf, sizeof(buf) - 1); return true; }
    return false;
}

static void bench(const char* name, int iters, std::function<void()> fn) {
    WallClock wall;
    for (int i = 0; i < iters; ++i) fn();
    double total_ms = wall.elapsed_sec() * 1000.0;
    double per_us = (total_ms * 1000.0) / iters;
    printf("  %-35s %8d iters  %8.1f ms total  %8.1f us/iter\n", name, iters, total_ms, per_us);
}

int main() {
    printf("=== IPC Throughput Benchmarks ===\n\n");
    echo_server();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    bench("ping_pong", 5000, []() {
        int fd = ipc_connect();
        ipc_ping(fd);
        close(fd);
    });

    bench("ping_pong_persistent", 5000, []() {
        static int fd = -1;
        if (fd < 0) fd = ipc_connect();
        ipc_ping(fd);
    });

    bench("connect_disconnect", 5000, []() {
        int fd = ipc_connect();
        close(fd);
    });

    bench("128byte_msg", 3000, []() {
        int fd = ipc_connect();
        std::string msg(128, 'x');
        write(fd, msg.c_str(), msg.size());
        char buf[256];
        struct pollfd pfd{fd, POLLIN, 0};
        poll(&pfd, 1, 1000);
        read(fd, buf, sizeof(buf) - 1);
        close(fd);
    });

    bench("batch_100_msgs", 200, []() {
        int fd = ipc_connect();
        for (int i = 0; i < 100; ++i) write(fd, "x\n", 2);
        int received = 0;
        char buf[4096];
        while (received < 200) {
            struct pollfd pfd{fd, POLLIN, 0};
            if (poll(&pfd, 1, 1000) <= 0) break;
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            received += n;
        }
        close(fd);
    });

    printf("\nDone.\n");
    fs::remove_all(fs::path(ipc_sock).parent_path());
    return 0;
}
