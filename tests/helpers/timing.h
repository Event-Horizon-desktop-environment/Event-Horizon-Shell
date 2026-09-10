#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

class WallClock {
    using clock = std::chrono::steady_clock;
    clock::time_point m_start;
public:
    WallClock() : m_start(clock::now()) {}
    int64_t elapsed_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - m_start).count();
    }
    int64_t elapsed_us() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - m_start).count();
    }
    int64_t elapsed_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - m_start).count();
    }
    double elapsed_sec() const {
        return std::chrono::duration<double>(clock::now() - m_start).count();
    }
    void reset() { m_start = clock::now(); }
};

struct TimingStats {
    std::vector<double> samples;

    void add(double ms) { samples.push_back(ms); }

    double mean() const {
        if (samples.empty()) return 0;
        return std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    }

    double median() const {
        if (samples.empty()) return 0;
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        size_t n = sorted.size();
        if (n % 2 == 0) return (sorted[n/2 - 1] + sorted[n/2]) / 2.0;
        return sorted[n/2];
    }

    double p95() const { return percentile(95); }
    double p99() const { return percentile(99); }
    double min() const { return samples.empty() ? 0 : *std::min_element(samples.begin(), samples.end()); }
    double max() const { return samples.empty() ? 0 : *std::max_element(samples.begin(), samples.end()); }

    double percentile(int p) const {
        if (samples.empty()) return 0;
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = (sorted.size() * p) / 100;
        return sorted[std::min(idx, sorted.size() - 1)];
    }

    std::string to_json(const std::string& name) const {
        char buf[512];
        snprintf(buf, sizeof(buf),
            "\"%s\":{\"mean\":%.2f,\"median\":%.2f,\"p95\":%.2f,\"p99\":%.2f,\"min\":%.2f,\"max\":%.2f,\"count\":%zu}",
            name.c_str(), mean(), median(), p95(), p99(), min(), max(), samples.size());
        return buf;
    }
};
