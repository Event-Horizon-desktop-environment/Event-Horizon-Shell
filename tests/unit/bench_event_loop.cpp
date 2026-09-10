#include <cstdio>
#include <cstdlib>
#include <functional>
#include <signal.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include <thread>
#include <atomic>
#include <vector>
#include "helpers/timing.h"

static void bench(const char* name, int iters, std::function<void()> fn) {
    WallClock wall;
    for (int i = 0; i < iters; ++i) fn();
    double total_ms = wall.elapsed_sec() * 1000.0;
    double per_us = (total_ms * 1000.0) / iters;
    printf("  %-35s %8d iters  %8.1f ms total  %8.1f us/iter\n", name, iters, total_ms, per_us);
}

int main() {
    printf("=== Event Loop Benchmarks ===\n\n");

    bench("ppoll_single_timer", 50000, []() {
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        struct itimerspec ts{};
        ts.it_value = {0, 1000};
        timerfd_settime(tfd, 0, &ts, nullptr);
        sigset_t mask; sigemptyset(&mask);
        struct pollfd pfd{tfd, POLLIN, 0};
        ppoll(&pfd, 1, nullptr, &mask);
        uint64_t exp; read(tfd, &exp, sizeof(exp));
        close(tfd);
    });

    bench("ppoll_5_fds", 20000, []() {
        std::vector<int> efds;
        for (int i = 0; i < 5; ++i) efds.push_back(eventfd(0, EFD_NONBLOCK));
        std::atomic<bool> done{false};
        std::thread producer([&]() {
            int idx = 0;
            while (!done) {
                uint64_t v = 1;
                write(efds[idx % 5], &v, sizeof(v));
                idx++;
                usleep(10);
            }
        });
        sigset_t mask; sigemptyset(&mask);
        std::vector<struct pollfd> pfds;
        for (auto fd : efds) pfds.push_back({fd, POLLIN, 0});
        ppoll(pfds.data(), pfds.size(), nullptr, &mask);
        for (auto& p : pfds) { if (p.revents & POLLIN) { uint64_t v; read(p.fd, &v, sizeof(v)); } }
        done = true;
        producer.join();
        for (auto fd : efds) close(fd);
    });

    bench("ppoll_with_timeout_1us", 50000, []() {
        int efd = eventfd(0, EFD_NONBLOCK);
        struct pollfd pfd{efd, POLLIN, 0};
        sigset_t mask; sigemptyset(&mask);
        struct timespec timeout{0, 1000};
        ppoll(&pfd, 1, &timeout, &mask);
        close(efd);
    });

    bench("timerfd_create_destroy", 100000, []() {
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        close(tfd);
    });

    bench("eventfd_wakeup", 50000, []() {
        int efd = eventfd(0, EFD_NONBLOCK);
        uint64_t v = 1;
        write(efd, &v, sizeof(v));
        read(efd, &v, sizeof(v));
        close(efd);
    });

    printf("\nDone.\n");
    return 0;
}
