#include "thread/pool/decode_pool.hpp"
#include "decode/core/decoder.hpp"

#include <cstdio>
#include <vector>

namespace hpv {

DecodePool::DecodePool() = default;

DecodePool::~DecodePool() {
    stop_ = true;
    cancel_ = true;
    cv_.notify_one();
}

void DecodePool::prefetch(const std::string& path, DecoderRegistry& decoders) {
    cancel();
    {
        std::lock_guard lock(mutex_);
        pending_path_ = path;
        decoders_ = &decoders;
        cancel_ = false;
    }
    cv_.notify_one();
}

void DecodePool::cancel() {
    cancel_ = true;
    {
        std::lock_guard lock(mutex_);
        pending_path_.clear();
    }
}

bool DecodePool::busy() const {
    std::lock_guard lock(mutex_);
    return !pending_path_.empty();
}

std::optional<DecodeResult> DecodePool::try_claim(const std::string& path) {
    std::lock_guard lock(result_mutex_);
    if (result_ready_ && result_path_ == path) {
        result_ready_ = false;
        return std::move(result_);
    }
    return std::nullopt;
}

void DecodePool::worker() {
    while (!stop_) {
        std::string path;
        DecoderRegistry* decoders = nullptr;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return stop_ || (!pending_path_.empty());
            });
            if (stop_) return;
            path = pending_path_;
            decoders = decoders_;
        }

        // Read file
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); continue; }

        std::vector<uint8_t> buf((size_t)sz);
        if (fread(buf.data(), 1, (size_t)sz, f) != (size_t)sz) {
            fclose(f);
            continue;
        }
        fclose(f);

        // Decode (check cancellation before proceeding)
        if (cancel_) {
            cancel_ = false;
            {
                std::lock_guard lk(mutex_);
                if (pending_path_ == path) pending_path_.clear();
            }
            continue;
        }
        auto result = decoders->decode(buf.data(), buf.size());
        if (cancel_) {
            cancel_ = false;
            {
                std::lock_guard lk(mutex_);
                if (pending_path_ == path) pending_path_.clear();
            }
            continue;
        }

        // Store result
        {
            std::lock_guard lock(result_mutex_);
            result_path_ = path;
            result_ = std::move(result);
            result_ready_ = true;
        }

        // Clear pending so we don't re-process
        {
            std::lock_guard lock(mutex_);
            if (pending_path_ == path) {
                pending_path_.clear();
            }
        }
    }
}

} // namespace hpv
