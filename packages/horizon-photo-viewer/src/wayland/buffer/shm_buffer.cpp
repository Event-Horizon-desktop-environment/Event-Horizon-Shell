#include "wayland/buffer/shm_buffer.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

namespace hpv {

void ShmBuffer::handle_release(void* data, wl_buffer* /*buffer*/) {
    auto* self = static_cast<ShmBuffer*>(data);
    self->busy_ = false;
    if (self->release_cb_) {
        self->release_cb_();
    }
}

}

namespace hpv {

namespace {

int create_tmpfile() {
    // Try memfd_create first (Linux 3.17+)
    int fd = memfd_create("hpv-shm", MFD_CLOEXEC);
    if (fd >= 0) return fd;

    // Fallback: create anonymous file in shm dir
    const char* dirs[] = {"/dev/shm", "/tmp", nullptr};
    for (const char** d = dirs; *d; d++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/hpv-shm-XXXXXX", *d);
        fd = mkstemp(path);
        if (fd >= 0) {
            unlink(path); // Remove on close
            return fd;
        }
    }
    return -1;
}

}

ShmBuffer::ShmBuffer(ShmBuffer&& other) noexcept
    : buffer_(other.buffer_), pool_(other.pool_), data_(other.data_),
      width_(other.width_), height_(other.height_), stride_(other.stride_),
      size_(other.size_), fd_(other.fd_), busy_(other.busy_),
      release_cb_(std::move(other.release_cb_)) {
    other.buffer_ = nullptr;
    other.pool_ = nullptr;
    other.data_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
    other.fd_ = -1;
    other.busy_ = false;
}

ShmBuffer& ShmBuffer::operator=(ShmBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        buffer_ = other.buffer_;
        pool_ = other.pool_;
        data_ = other.data_;
        width_ = other.width_;
        height_ = other.height_;
        stride_ = other.stride_;
        size_ = other.size_;
        fd_ = other.fd_;
        busy_ = other.busy_;
        release_cb_ = std::move(other.release_cb_);
        other.buffer_ = nullptr;
        other.pool_ = nullptr;
        other.data_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
        other.fd_ = -1;
        other.busy_ = false;
    }
    return *this;
}

ShmBuffer::~ShmBuffer() {
    destroy();
}

bool ShmBuffer::init(wl_shm* shm, int width, int height) {
    if (!shm || width <= 0 || height <= 0) return false;

    destroy();

    stride_ = width * 4; // RGBA 4 bytes per pixel
    size_ = (size_t)stride_ * height;
    fd_ = create_tmpfile();
    if (fd_ < 0) {
        std::cerr << "shm: failed to create temp file\n";
        return false;
    }

    // Ensure the file is big enough
    if (ftruncate(fd_, (off_t)size_) < 0) {
        std::cerr << "shm: ftruncate failed: " << strerror(errno) << "\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    data_ = static_cast<uint8_t*>(mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd_, 0));
    if (data_ == MAP_FAILED) {
        std::cerr << "shm: mmap failed: " << strerror(errno) << "\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    pool_ = wl_shm_create_pool(shm, fd_, (int32_t)size_);
    if (!pool_) {
        std::cerr << "shm: failed to create wl_shm_pool\n";
        munmap(data_, size_);
        close(fd_);
        fd_ = -1;
        return false;
    }

    buffer_ = wl_shm_pool_create_buffer(pool_, 0, width, height, stride_,
                                         WL_SHM_FORMAT_ARGB8888);
    if (!buffer_) {
        std::cerr << "shm: failed to create wl_buffer\n";
        wl_shm_pool_destroy(pool_);
        munmap(data_, size_);
        close(fd_);
        fd_ = -1;
        return false;
    }

    width_ = width;
    height_ = height;
    busy_ = false;

    static const wl_buffer_listener listener = { .release = handle_release };
    wl_buffer_add_listener(buffer_, &listener, this);

    // We can close the fd now — wl_shm_pool keeps its own reference
    close(fd_);
    fd_ = -1;

    return true;
}

void ShmBuffer::destroy() {
    if (buffer_) {
        wl_buffer_destroy(buffer_);
        buffer_ = nullptr;
    }
    if (pool_) {
        wl_shm_pool_destroy(pool_);
        pool_ = nullptr;
    }
    if (data_ && size_ > 0) {
        munmap(data_, size_);
        data_ = nullptr;
        size_ = 0;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    width_ = 0;
    height_ = 0;
    stride_ = 0;
}

}
