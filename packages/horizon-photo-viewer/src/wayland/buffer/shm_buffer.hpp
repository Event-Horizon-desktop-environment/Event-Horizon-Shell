#pragma once

#include <cstdint>
#include <functional>

struct wl_buffer;
struct wl_shm;
struct wl_shm_pool;

namespace hpv {

class ShmBuffer {
public:
    ShmBuffer() = default;
    ~ShmBuffer();

    ShmBuffer(const ShmBuffer&) = delete;
    ShmBuffer& operator=(const ShmBuffer&) = delete;
    ShmBuffer(ShmBuffer&& other) noexcept;
    ShmBuffer& operator=(ShmBuffer&& other) noexcept;

    bool init(wl_shm* shm, int width, int height);
    void destroy();

    uint8_t* data() { return data_; }
    const uint8_t* data() const { return data_; }
    wl_buffer* buffer() const { return buffer_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }
    bool busy() const { return busy_; }

    void set_release_callback(std::function<void()> cb) {
        release_cb_ = std::move(cb);
    }

    void mark_busy() { busy_ = true; }

private:
    static void handle_release(void* data, wl_buffer* buffer);

    wl_buffer* buffer_ = nullptr;
    wl_shm_pool* pool_ = nullptr;
    uint8_t* data_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;
    size_t size_ = 0;
    int fd_ = -1;
    bool busy_ = false;
    std::function<void()> release_cb_;
};

}
