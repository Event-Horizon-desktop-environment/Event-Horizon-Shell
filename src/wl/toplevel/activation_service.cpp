#include "wl/toplevel/activation_service.hpp"

#include <wayland-client.h>

namespace eh::wayland {

void ActivationService::token_done(void* data, xdg_activation_token_v1* token, const char* tokenStr) {
   
  auto* pending = static_cast<PendingToken*>(data);
  if (pending->cb) pending->cb(tokenStr ? std::string(tokenStr) : std::string{});
  if (token) xdg_activation_token_v1_destroy(token);
  auto& tokens = pending->service->pending_tokens_;
  for (auto it = tokens.begin(); it != tokens.end(); ++it) {
    if (it->get() == pending) { tokens.erase(it); break; }
  }
}

bool ActivationService::request_token(wl_seat* seat, std::uint32_t serial, wl_surface* surface, TokenCallback cb) {
   
  if (!activation_ || !seat || serial == 0) return false;

  auto* token = xdg_activation_v1_get_activation_token(activation_);
  if (!token) return false;

  auto pending = std::make_unique<PendingToken>(PendingToken{.obj = token, .cb = std::move(cb), .service = this});
  xdg_activation_token_v1_add_listener(token, &kTokenListener_, pending.get());
  xdg_activation_token_v1_set_serial(token, serial, seat);
  if (surface) xdg_activation_token_v1_set_surface(token, surface);
  xdg_activation_token_v1_commit(token);
  pending_tokens_.push_back(std::move(pending));
  return true;
}

bool ActivationService::activate(const std::string& token, wl_surface* surface) {
   
  if (!activation_ || token.empty() || !surface) return false;
  xdg_activation_v1_activate(activation_, token.c_str(), surface);
  return true;
}

}
