#pragma once

#include "wl/core/protocols.hpp"

#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct wl_surface;
struct wl_seat;

namespace eh::wayland {

class ActivationService {
public:
  using TokenCallback = std::function<void(std::string token)>;

  void bind(xdg_activation_v1* activation) { activation_ = activation; }
  [[nodiscard]] bool is_available() const noexcept { return activation_ != nullptr; }

  bool request_token(wl_seat* seat, std::uint32_t serial, wl_surface* surface, TokenCallback cb);

  bool activate(const std::string& token, wl_surface* surface);

private:
  struct PendingToken {
    xdg_activation_token_v1* obj = nullptr;
    TokenCallback cb{};
    ActivationService* service = nullptr;
  };

  static void token_done(void* data, xdg_activation_token_v1* token, const char* tokenStr);
  static constexpr xdg_activation_token_v1_listener kTokenListener_ = {
      .done = token_done,
  };

  std::vector<std::unique_ptr<PendingToken>> pending_tokens_{};
  xdg_activation_v1* activation_ = nullptr;
};

}
