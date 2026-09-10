#if defined(EH_HAVE_POLKIT_AGENT)

#include "services/polkit/service/polkit_auth_service.hpp"

#include "services/polkit/listener/polkit_bridge_capi.h"
#include "services/polkit/listener/eh_polkit_listener.h"

#include <polkit/polkit.h>
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#define _POLKIT_AGENT_COMPILATION
#include <polkitagent/polkitagent.h>

#include <glib.h>
#include <gio/gio.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

namespace eh::polkit {

constexpr const char* k_agentObjectPath = "/org/eventhorizon/polkit1/AuthenticationAgent";

struct PolkitAuthService::Impl {
  GMainContext* ctx = nullptr;
  gpointer reg = nullptr;
  PolkitAgentListener* listener = nullptr;
  PolkitAgentSession* active_session = nullptr;

  std::thread register_thread;
  bool register_done = false;
  bool register_ok = false;
  GCancellable* cancellable = nullptr;

  std::vector<GPollFD> glib_fds;
  gint glib_priority = G_PRIORITY_DEFAULT;
  gint glib_timeout = -1;

  mutable std::mutex mtx;
  bool started = false;
  ChangeCallback on_change;

  // Auth state (protected by mtx)
  AuthState state = AuthState::Idle;
  std::string action_id;
  std::string message;
  std::string cookie;
  std::string input_prompt;
  std::string supplementary_error;
  bool echo_on = true;
  bool failed_flag = false;
  bool prompt_active = false;

  // Pending actions for dispatch_glib
  bool pending_begin = false;
  bool pending_end = false;
  bool state_changed = false;

  static void registration_thread_main(PolkitSubject* subject, Impl* impl) {
     
    GError* err = nullptr;
    GCancellable* cancel = g_cancellable_new();
    impl->cancellable = cancel;
    gpointer handle = polkit_agent_listener_register(
        POLKIT_AGENT_LISTENER(impl->listener),
        POLKIT_AGENT_REGISTER_FLAGS_NONE,
        subject,
        k_agentObjectPath,
        cancel,
        &err);
    g_object_unref(subject);

    {
      std::lock_guard<std::mutex> lock(impl->mtx);
      if (g_cancellable_is_cancelled(cancel)) {
        std::cerr << "[polkit] listener registration cancelled\n";
        impl->register_ok = false;
      } else if (!handle) {
        std::cerr << "[polkit] listener_register failed: "
                  << (err && err->message ? err->message : "?") << "\n";
        if (err) g_error_free(err);
        impl->register_ok = false;
      } else {
        std::cerr << "[polkit] session agent registered at " << k_agentObjectPath << "\n";
        impl->register_ok = true;
      }
      impl->reg = handle;
      impl->register_done = true;
    }
  }

  static void on_session_completed(PolkitAgentSession* session, gboolean gained_authorization, gpointer user_data) {
     
    auto* impl = static_cast<Impl*>(user_data);
    (void)session;
    (void)gained_authorization;
    impl->pending_end = true;
    if (impl->active_session) {
      g_object_unref(impl->active_session);
      impl->active_session = nullptr;
    }
  }

  static void on_session_request(PolkitAgentSession* session, gchar* request, gboolean echo_on, gpointer user_data) {
     
    auto* impl = static_cast<Impl*>(user_data);
    (void)session;
    std::lock_guard<std::mutex> lock(impl->mtx);
    impl->echo_on = echo_on != FALSE;
    impl->input_prompt = request ? request : "";
    impl->prompt_active = true;
    impl->state = AuthState::Active;
  }

  static void on_session_show_error(PolkitAgentSession* session, gchar* text, gpointer user_data) {
     
    auto* impl = static_cast<Impl*>(user_data);
    (void)session;
    std::lock_guard<std::mutex> lock(impl->mtx);
    impl->supplementary_error = text ? text : "";
    impl->failed_flag = true;
  }

  static void on_session_show_info(PolkitAgentSession* session, gchar* text, gpointer user_data) {
     
    auto* impl = static_cast<Impl*>(user_data);
    (void)session;
    std::lock_guard<std::mutex> lock(impl->mtx);
    impl->input_prompt = text ? text : "";
  }

  void begin_prompt(const char* act, const char* msg, const char* ck) {
     
    std::lock_guard<std::mutex> lock(mtx);
    action_id = act ? act : "";
    message = msg ? msg : "";
    cookie = ck ? ck : "";
    supplementary_error.clear();
    input_prompt.clear();
    failed_flag = false;
    state = AuthState::Pending;
    pending_begin = true;
    state_changed = true;
  }

  void end_prompt() {
     
    std::lock_guard<std::mutex> lock(mtx);
    state = AuthState::Idle;
    prompt_active = false;
    pending_end = true;
    state_changed = true;
  }

  void store_session(PolkitAgentSession* /*session*/, int echo) {
     
    std::lock_guard<std::mutex> lock(mtx);
    echo_on = echo != 0;
    prompt_active = true;
    state = AuthState::Active;
    state_changed = true;
  }

  void deliver_response(const char* response) {
     
    if (active_session) {
      polkit_agent_session_response(active_session, response);
      active_session = nullptr;
    }
  }

  void cancel_session() {
     
    if (active_session) {
      polkit_agent_session_cancel(active_session);
      active_session = nullptr;
    }
  }
};

// C bridge functions
extern "C" void eh_polkit_bridge_begin_prompt(const char* action_id, const char* message,
                                              const char* cookie) {
   
  auto& svc = PolkitAuthService::instance();
  svc.impl()->begin_prompt(action_id, message, cookie);
}

extern "C" void eh_polkit_bridge_end_prompt() {
   
  auto& svc = PolkitAuthService::instance();
  svc.impl()->end_prompt();
}

extern "C" void eh_polkit_bridge_show_error_line(const char* text) {
   
  auto& svc = PolkitAuthService::instance();
  std::lock_guard<std::mutex> lock(svc.impl()->mtx);
  svc.impl()->supplementary_error = text ? text : "";
  svc.impl()->failed_flag = true;
  svc.impl()->state_changed = true;
}

extern "C" void eh_polkit_bridge_show_info_line(const char* text) {
   
  auto& svc = PolkitAuthService::instance();
  std::lock_guard<std::mutex> lock(svc.impl()->mtx);
  svc.impl()->input_prompt = text ? text : "";
  svc.impl()->state_changed = true;
}

extern "C" void eh_polkit_bridge_store_session(PolkitAgentSession* session, int echo_on) {
   
  auto& svc = PolkitAuthService::instance();
  svc.impl()->active_session = session;
  svc.impl()->store_session(session, echo_on);
}

extern "C" void eh_polkit_bridge_deliver_response(const char* response) {
   
  auto& svc = PolkitAuthService::instance();
  svc.impl()->deliver_response(response);
}

extern "C" void eh_polkit_bridge_cancel_session() {
   
  auto& svc = PolkitAuthService::instance();
  svc.impl()->cancel_session();
}

PolkitAuthService::PolkitAuthService()
  : impl_(std::make_unique<Impl>()) {
   
}

PolkitAuthService::~PolkitAuthService() {
   
  stop();
}

PolkitAuthService& PolkitAuthService::instance() {
   
  static PolkitAuthService svc;
  return svc;
}

void PolkitAuthService::start() {
   
  std::lock_guard<std::mutex> lock(impl_->mtx);
  if (impl_->started) return;
  impl_->started = true;

  impl_->ctx = g_main_context_default();

  if (const char* dbus = std::getenv("DBUS_SESSION_BUS_ADDRESS")) {
    std::cerr << "[polkit] DBUS_SESSION_BUS_ADDRESS present (len=" << std::strlen(dbus) << ")\n";
  } else {
    std::cerr << "[polkit] DBUS_SESSION_BUS_ADDRESS unset\n";
  }

  impl_->listener = eh_polkit_listener_new();

  const char* sessionId = std::getenv("XDG_SESSION_ID");
  PolkitSubject* subject = nullptr;
  if (sessionId && sessionId[0]) {
    subject = polkit_unix_session_new(sessionId);
    if (subject) {
      std::cerr << "[polkit] using XDG_SESSION_ID=" << sessionId << "\n";
    }
  }
  if (!subject) {
    GError* err = nullptr;
    subject = polkit_unix_session_new_for_process_sync(
        static_cast<gint>(::getpid()), nullptr, &err);
    if (!subject) {
      std::cerr << "[polkit] polkit_unix_session_new_for_process_sync failed: "
                << (err && err->message ? err->message : "?") << "\n";
      if (err) g_error_free(err);
      impl_->ctx = nullptr;
      return;
    }
  }

  impl_->register_thread = std::thread(Impl::registration_thread_main, subject, impl_.get());
}

void PolkitAuthService::stop() {
   
  if (impl_->cancellable) {
    g_cancellable_cancel(impl_->cancellable);
  }
  if (impl_->register_thread.joinable()) {
    impl_->register_thread.join();
  }
  g_clear_object(&impl_->cancellable);

  if (impl_->reg) {
    polkit_agent_listener_unregister(impl_->reg);
    impl_->reg = nullptr;
  }
  if (impl_->listener) {
    g_object_unref(impl_->listener);
    impl_->listener = nullptr;
  }

  std::lock_guard<std::mutex> lock(impl_->mtx);
  impl_->ctx = nullptr;
  impl_->started = false;
}

void PolkitAuthService::set_change_callback(ChangeCallback cb) {
  impl_->on_change = std::move(cb);
}

Snapshot PolkitAuthService::snapshot() const {
   
  std::lock_guard<std::mutex> lock(impl_->mtx);
  Snapshot s;
  s.state = impl_->state;
  s.action_id = impl_->action_id;
  s.message = impl_->message;
  s.prompt = impl_->input_prompt;
  s.error = impl_->supplementary_error;
  s.cookie = impl_->cookie;
  s.echo_on = impl_->echo_on;
  s.has_request = impl_->prompt_active;
  return s;
}

void PolkitAuthService::submit_response(const std::string& pw) {
  impl_->deliver_response(pw.c_str());
}

void PolkitAuthService::cancel_request() {
  impl_->cancel_session();
}

void PolkitAuthService::gather_fds(std::vector<pollfd>& fds) {
   
  if (!impl_->ctx) return;

  impl_->glib_fds.clear();
  impl_->glib_priority = G_PRIORITY_DEFAULT;
  impl_->glib_timeout = -1;

  if (!g_main_context_acquire(impl_->ctx)) return;

  const gboolean ready = g_main_context_prepare(impl_->ctx, &impl_->glib_priority);
  gint timeout = -1;
  const gint count = g_main_context_query(impl_->ctx, impl_->glib_priority, &timeout, nullptr, 0);
  impl_->glib_timeout = ready ? 0 : timeout;
  if (count > 0) {
    impl_->glib_fds.resize(static_cast<std::size_t>(count));
    g_main_context_query(impl_->ctx, impl_->glib_priority, &timeout, impl_->glib_fds.data(), count);
    impl_->glib_timeout = ready ? 0 : timeout;
    for (const GPollFD& gf : impl_->glib_fds) {
      fds.push_back({.fd = gf.fd, .events = static_cast<short>(gf.events), .revents = 0});
    }
  }
  g_main_context_release(impl_->ctx);
}

int PolkitAuthService::poll_timeout() {
   
  bool rd = false;
  bool rok = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    rd = impl_->register_done;
    rok = impl_->register_ok;
  }
  if (!rd && !rok) return 50;
  return impl_->glib_timeout;
}

void PolkitAuthService::dispatch_glib(const pollfd* poll_fds, int nfds) {
   
  // Check if registration thread completed and should be joined
  {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->register_thread.joinable() && impl_->register_done) {
      impl_->register_thread.join();
    }
  }

  // Process pending begin/end from C bridge
  bool did_state_change = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->pending_end) {
      impl_->pending_end = false;
      impl_->state = AuthState::Idle;
      impl_->prompt_active = false;
    }
    if (impl_->pending_begin) {
      impl_->pending_begin = false;
    }
    if (impl_->state_changed) {
      impl_->state_changed = false;
      did_state_change = true;
    }
  }

  if (!impl_->ctx) return;

  if (!g_main_context_acquire(impl_->ctx)) return;

  for (auto& gf : impl_->glib_fds) {
    gf.revents = 0;
    for (int i = 0; i < nfds; ++i) {
      if (poll_fds[i].fd == gf.fd) {
        gf.revents = static_cast<gushort>(poll_fds[i].revents);
        break;
      }
    }
  }

  const gboolean ready =
      g_main_context_check(impl_->ctx, impl_->glib_priority, impl_->glib_fds.data(),
                           static_cast<gint>(impl_->glib_fds.size()));
  g_main_context_release(impl_->ctx);

  if (ready) {
    if (g_main_context_acquire(impl_->ctx)) {
      g_main_context_dispatch(impl_->ctx);
      g_main_context_release(impl_->ctx);
    }
  }

  while (g_main_context_pending(impl_->ctx)) {
    g_main_context_iteration(impl_->ctx, FALSE);
  }

  // Check if GLib dispatch itself triggered a state change
  {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->state_changed) {
      impl_->state_changed = false;
      did_state_change = true;
    }
  }

  // Notify change callback only when state actually transitioned
  if (did_state_change && impl_->on_change) {
    impl_->on_change();
  }
}

}

#endif
