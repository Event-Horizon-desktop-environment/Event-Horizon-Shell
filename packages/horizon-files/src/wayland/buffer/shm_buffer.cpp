#include "wayland/buffer/shm_buffer.hpp"

#include "wayland/core/memfd.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>

namespace eh::wayland {

ShmBuffer::~ShmBuffer() {
  MANGOWM_DEBUG("ShmBuffer dtor this=%p", (void*)this);
  destroy();
}

void ShmBuffer::set_release_hook(ReleaseHook hook, void* user) {
  hook_ = hook;
  hookUser_ = user;
}

void ShmBuffer::wl_release(void* data, wl_buffer*  ) {
   
  auto* self = static_cast<ShmBuffer*>(data);
  self->busy_ = false;
  if (self->hook_) self->hook_(self->hookUser_);
}

void ShmBuffer::destroy() {
   
  hook_ = nullptr;
  hookUser_ = nullptr;
  if (cr_) cairo_destroy(cr_);
  cr_ = nullptr;
  if (cairoSurface_) cairo_surface_destroy(cairoSurface_);
  cairoSurface_ = nullptr;
  if (buffer_) wl_buffer_destroy(buffer_);
  buffer_ = nullptr;
  if (data_) {
    madvise(data_, size_, MADV_DONTNEED);
    munmap(data_, size_);
  }
  data_ = nullptr;
  size_ = 0;
  width_ = 0;
  height_ = 0;
  stride_ = 0;
  busy_ = false;
}

bool ShmBuffer::ensure(wl_shm* shm, const char* tag, int width, int height) {
   
  if (!shm || width <= 0 || height <= 0) return false;
  if (buffer_ && width_ == width && height_ == height) return true;

  destroy();

  width_ = width;
  height_ = height;
  stride_ = width * 4;
  size_ = static_cast<size_t>(stride_) * static_cast<size_t>(height);

  int fd = memfd_create_compat(tag ? tag : "event-horizon", 0);
  if (fd < 0) {
    char name[] = "/tmp/event-horizon-shm-XXXXXX";
    fd = mkstemp(name);
    if (fd < 0) return false;
    unlink(name);
  }

  if (ftruncate(fd, static_cast<off_t>(size_)) != 0) {
    close(fd);
    return false;
  }

  void* data = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    close(fd);
    return false;
  }

  wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int>(size_));
  close(fd);
  if (!pool) {
    munmap(data, size_);
    return false;
  }

  wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride_, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  if (!buffer) {
    munmap(data, size_);
    return false;
  }

  data_ = data;
  buffer_ = buffer;
  wl_buffer_add_listener(buffer_, &kListener_, this);

  cairoSurface_ = cairo_image_surface_create_for_data(
      static_cast<unsigned char*>(data_), CAIRO_FORMAT_ARGB32, width, height, stride_);
  cr_ = cairoSurface_ ? cairo_create(cairoSurface_) : nullptr;
  return cr_ != nullptr;
}

}
