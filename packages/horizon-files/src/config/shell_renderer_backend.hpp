#pragma once

#include <cstdint>

namespace eh::config {

enum class ShellRendererBackend : std::uint8_t {
  Cairo = 0,
  Vulkan = 1,
};

}
