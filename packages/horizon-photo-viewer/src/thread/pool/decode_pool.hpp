#pragma once

#include "decode/core/decoder.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace hpv {

class DecoderRegistry;

class DecodePool {
public:
    DecodePool();
    ~DecodePool();

    // Start decoding a file in the background.
    // Replaces any previously queued task.
    void prefetch(const std::string& path, DecoderRegistry& decoders);

    // Try to claim a completed prefetch result.
    // Returns the result if one is ready for the given path, nullopt otherwise.
    std::optional<DecodeResult> try_claim(const std::string& path);

    // Cancel any in-flight prefetch (e.g. when navigating rapidly).
    bool busy() const;
    void cancel();

private:
    void worker();

    std::jthread worker_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::string pending_path_;
    DecoderRegistry* decoders_ = nullptr;
    std::atomic<bool> stop_{false};
    std::atomic<bool> cancel_{false};

    // Completed result (consumed by main thread)
    mutable std::mutex result_mutex_;
    std::string result_path_;
    DecodeResult result_;
    bool result_ready_ = false;
};

} // namespace hpv
