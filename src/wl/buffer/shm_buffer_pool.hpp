#pragma once

#include <wayland-client.h>

#include <cstddef>
#include <vector>

namespace eh::wayland {

class ShmBufferPool {
public:
  static ShmBufferPool& instance();

  struct Slot {
    void* data = nullptr;
    size_t offset = 0;
    size_t size = 0;
    int generation = 0;
  };

  [[nodiscard]] Slot acquire(size_t size);
  void release(Slot& slot);

private:
  ShmBufferPool();
  ~ShmBufferPool();
  ShmBufferPool(const ShmBufferPool&) = delete;
  ShmBufferPool& operator=(const ShmBufferPool&) = delete;
  ShmBufferPool(ShmBufferPool&&) = delete;
  ShmBufferPool& operator=(ShmBufferPool&&) = delete;

  bool grow(size_t needed);

  struct Block {
    int fd = -1;
    void* data = nullptr;
    size_t capacity = 0;
    size_t top = 0;
  };

  std::vector<Block> blocks_;
  size_t usedBytes_ = 0;
};

}
