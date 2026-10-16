#if defined(EH_HAVE_POLKIT_AGENT)

#include "services/polkit/service/polkit_auth_service.hpp"

#include "services/polkit/listener/polkit_bridge_capi.h"
#include "services/polkit/listener/eh_polkit_listener.h"
#include "services/polkit/polkit_log.hpp"

#include <polkit/polkit.h>
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#define _POLKIT_AGENT_COMPILATION
#include <polkitagent/polkitagent.h>

#include <glib.h>
#include <gio/gio.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <pwd.h>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

namespace eh::polkit {

constexpr const char* k_agentObjectPath = "/org/eventhorizon/polkit1/AuthenticationAgent";

namespace {

// Reference agents (mate-polkit, GNOME Shell) never show a bare "Password:"
// when authenticating as someone else — mate rewrites it to "Password for
// <user>:". The identity here is human-readable ("user" / "Full Name (user)");
static std::string prompt_label_for(const std::string& identity, const std::string& prompt) {
  if (prompt.empty()) return prompt;
  // Match "password:" + optional trailing space, case-insensitive.
  constexpr const char* kPw = "password:";
  constexpr std::size_t kPwLen = 9;
  if (prompt.size() < kPwLen) return prompt;
  for (std::size_t i = 0; i < kPwLen; ++i) {
    if (std::tolower(static_cast<unsigned char>(prompt[i])) != kPw[i]) return prompt;
  }
  for (std::size_t i = kPwLen; i < prompt.size(); ++i) {
    if (prompt[i] != ' ' && prompt[i] != '\t') return prompt;
  }
  // Short name: "Full Name (user)" -> "user".
  std::string who = identity;
  if (!who.empty() && who.back() == ')') {
    const std::size_t open = who.rfind(" (");
    if (open != std::string::npos) who = who.substr(open + 2, who.size() - open - 3);
  }
  if (who.empty()) return prompt;
  // Authenticating as yourself keeps the bare prompt.
  char buf[2048];
  struct passwd pwbuf {};
  struct passwd* result = nullptr;
  if (getpwuid_r(getuid(), &pwbuf, buf, sizeof(buf), &result) == 0 && result != nullptr &&
      who == result->pw_name)
    return prompt;
  return "Password for " + who + ":";
}

}  // namespace

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
  StateChangeHook on_change;

  // Auth state (protected by mtx)
  AuthState state = AuthState::Idle;
  std::string action_id;
  std::string message;
  std::string cookie;
  std::string input_prompt;   // session request ("Password:") — field label
  std::string identity;       // who polkit is asking about (dialog subtitle)
  std::string icon_name;      // themed icon for the action (dialog glyph)
  std::string details;        // serialized PolkitDetails
  std::string info_line;      // show-info text (session notes)
  std::string supplementary_error;
  bool echo_on = true;
  bool failed_flag = false;
  bool prompt_active = false;
  // Set when the user cancels. Late `request` signals from the dying session
  // are dropped (see store path), and completion of the cancelled session
  // simply resolves an already-hidden dialog. Cleared on the next
  // begin/end_prompt so a fresh auth is unaffected.
  bool cancel_armed = false;

  std::atomic<bool> pending_begin{false};
  std::atomic<bool> pending_end{false};
  std::atomic<bool> state_changed{false};

  static void registration_thread_main(PolkitSubject* subject, Impl* impl) {
    
    GError* err = nullptr;
    GCancellable* cancel = g_cancellable_new();
    {
      std::lock_guard<std::mutex> lock(impl->mtx);
      impl->cancellable = cancel;
    }
    // One shot is not enough: at login the bus/polkitd may not be ready yet,
    // and across a DE restart the previous agent can still hold the name
    // ("already exists" — seen live in horizon-polkit-agent.log). Retry a few
    // times; if we still fail the standalone main exits non-zero so the
    // supervisor respawns us. A live-but-unregistered agent answers nothing
    // and looks healthy from the outside, which reads as "auth is broken
    // until I restart the DE".
    gpointer handle = nullptr;
    constexpr int kAttempts = 6;
    for (int attempt = 1; attempt <= kAttempts && !handle; ++attempt) {
      if (g_cancellable_is_cancelled(cancel)) break;
      g_clear_error(&err);
      handle = polkit_agent_listener_register(
          POLKIT_AGENT_LISTENER(impl->listener),
          POLKIT_AGENT_REGISTER_FLAGS_NONE,
          subject,
          k_agentObjectPath,
          cancel,
          &err);
      if (!handle && attempt < kAttempts && !g_cancellable_is_cancelled(cancel)) {
        polkit_log("svc registration attempt %d/%d failed: %s; retrying in 1s", attempt,
                   kAttempts, err && err->message ? err->message : "?");
        std::cerr << "[polkit] listener registration attempt " << attempt << "/" << kAttempts
                  << " failed: " << (err && err->message ? err->message : "?")
                  << "; retrying\n";
        g_usleep(1000 * 1000);
      }
    }
    g_object_unref(subject);

    {
      std::lock_guard<std::mutex> lock(impl->mtx);
      if (g_cancellable_is_cancelled(cancel)) {
        std::cerr << "[polkit] listener registration cancelled\n";
        polkit_log("svc registration cancelled");
        impl->register_ok = false;
      } else if (!handle) {
        std::cerr << "[polkit] listener_register failed: "
                  << (err && err->message ? err->message : "?") << "\n";
        polkit_log("svc registration FAILED: %s", err && err->message ? err->message : "?");
        if (err) g_error_free(err);
        impl->register_ok = false;
      } else {
        std::cerr << "[polkit] session agent registered at " << k_agentObjectPath << "\n";
        polkit_log("svc registration ok at %s", k_agentObjectPath);
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
    impl->info_line = text ? text : "";
  }

  void begin_prompt(const char* act, const char* msg, const char* ck, const char* icon,
                    const char* dets) {
    
    PolkitAgentSession* stale = nullptr;
    {
      std::lock_guard<std::mutex> lock(mtx);
      stale = active_session;
      active_session = nullptr;
      action_id = act ? act : "";
      message = msg ? msg : "";
      cookie = ck ? ck : "";
      icon_name = icon ? icon : "";
      details = dets ? dets : "";
      identity.clear();
      info_line.clear();
      supplementary_error.clear();
      input_prompt.clear();
      failed_flag = false;
      cancel_armed = false;
      state = AuthState::Pending;
      pending_begin = true;
      state_changed = true;
    }
    // Single-flight is enforced C-side; a stale ref here is only defensive.
    if (stale) g_object_unref(stale);
  }

  // The listener's chosen identity — the dialog subtitle. Sent as its own
  // channel because it used to share the info-line field with the session
  // request ("Password:"), which overwrote it before anything could render it.
  void set_identity(const char* text) {
    std::lock_guard<std::mutex> lock(mtx);
    identity = text ? text : "";
    state_changed = true;
  }

  // The session's request string, rendered as the password field label.
  void set_prompt(const char* text) {
    std::lock_guard<std::mutex> lock(mtx);
    input_prompt = text ? text : "";
    state_changed = true;
  }

  void end_prompt(int gained) {
    
    PolkitAgentSession* stale = nullptr;
    {
      std::lock_guard<std::mutex> lock(mtx);
      stale = active_session;
      active_session = nullptr;
      state = gained ? AuthState::Success : AuthState::Failed;
      prompt_active = false;
      cancel_armed = false;
      pending_end = true;
      state_changed = true;
    }
    // Completion for a session we already consumed (cancel/deliver raced
    // it): drop our ref; the listener owns its own.
    if (stale) g_object_unref(stale);
  }

  void store_session(PolkitAgentSession* /*session*/, int echo) {
     
    std::lock_guard<std::mutex> lock(mtx);
    echo_on = echo != 0;
    prompt_active = true;
    state = AuthState::Active;
    state_changed = true;
  }

  void deliver_response(const char* response) {
    
    PolkitAgentSession* session = nullptr;
    {
      std::lock_guard<std::mutex> lock(mtx);
      session = active_session;
      active_session = nullptr;
    }
    polkit_log("svc deliver_response have_session=%d", session ? 1 : 0);
    if (session) {
      polkit_agent_session_response(session, response);
      g_object_unref(session);
    }
  }

  void cancel_session() {
    // The dialog holds an EXCLUSIVE keyboard grab while visible: if the
    // `completed` round-trip from polkitd never arrives (session already
    // gone, D-Bus hiccup, cancel raced with completion), the old code left
    // the dialog up forever — a frozen shell. Tear down locally FIRST so
    // the next dispatch hides the dialog even with no completion, then
    // tell polkitd. A late `completed` (end_prompt) is idempotent.
    //
    // Proven by ~/EH-logs/polkit.log: polkit_agent_session_cancel() itself
    // can block indefinitely, wedging the main loop thread. It therefore
    // runs on a detached worker; the transferred service ref keeps the
    // object alive until the worker is done with it.
    PolkitAgentSession* session = nullptr;
    {
      std::lock_guard<std::mutex> lock(mtx);
      session = active_session;
      active_session = nullptr;
      state = AuthState::Failed;
      prompt_active = false;
      cancel_armed = true;
      state_changed = true;
    }
    polkit_log("svc cancel_session enter have_session=%d", session ? 1 : 0);
    if (session) {
      std::thread([session] {
        polkit_log("svc cancel worker: calling session_cancel");
        polkit_agent_session_cancel(session);
        polkit_log("svc cancel worker: session_cancel returned");
        g_object_unref(session);
      }).detach();
    } else {
      polkit_log("svc cancel_session no live session (nothing to tell polkitd)");
    }
  }
};

// C bridge functions
extern "C" void eh_polkit_log_c(const char* msg) {
  polkit_log("c %s", msg ? msg : "");
}

extern "C" void eh_polkit_bridge_begin_prompt(const char* action_id, const char* message,
                                              const char* cookie, const char* icon_name,
                                              const char* details) {
  
  polkit_log("svc begin_prompt action='%s' icon='%s' cookie='%s'", action_id ? action_id : "",
             icon_name ? icon_name : "", cookie ? cookie : "");
  auto& svc = PolkitAuthService::instance();
  svc.impl()->begin_prompt(action_id, message, cookie, icon_name, details);
}

extern "C" void eh_polkit_bridge_end_prompt(int gained_authorization) {
  
  polkit_log("svc end_prompt gained=%d", gained_authorization);
  auto& svc = PolkitAuthService::instance();
  svc.impl()->end_prompt(gained_authorization);
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
  svc.impl()->info_line = text ? text : "";
  svc.impl()->state_changed = true;
}

extern "C" void eh_polkit_bridge_set_identity(const char* text) {
  
  auto& svc = PolkitAuthService::instance();
  svc.impl()->set_identity(text);
}

extern "C" void eh_polkit_bridge_set_prompt(const char* text) {
  
  auto& svc = PolkitAuthService::instance();
  svc.impl()->set_prompt(text);
}

extern "C" void eh_polkit_bridge_store_session(PolkitAgentSession* session, int echo_on) {
  
  auto& svc = PolkitAuthService::instance();
  std::lock_guard<std::mutex> lock(svc.impl()->mtx);
  // Drop prompts from a session the user already cancelled: accepting one
  // would re-show the dialog on a dead session and wedge the UI again.
  // A genuinely new auth always passes through begin_prompt first, which
  // clears cancel_armed.
  if (svc.impl()->cancel_armed) {
    polkit_log("svc store_session DROPPED (cancel_armed)");
    return;
  }
  // The service owns one ref; the C listener keeps its own. Split ownership
  // so a cancel racing a natural completion can't use-after-free.
  if (session) g_object_ref(session);
  if (svc.impl()->active_session) g_object_unref(svc.impl()->active_session);
  polkit_log("svc store_session echo=%d", echo_on);
  svc.impl()->active_session = session;
  svc.impl()->echo_on = echo_on != 0;
  svc.impl()->prompt_active = true;
  svc.impl()->state = AuthState::Active;
  svc.impl()->state_changed = true;
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
  impl_->register_done = false;
  impl_->register_ok = false;
  polkit_log("svc start pid=%d", static_cast<int>(::getpid()));

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
  
  polkit_log("svc stop");
  {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->cancellable) {
      g_cancellable_cancel(impl_->cancellable);
    }
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

  PolkitAgentSession* stale = nullptr;
  {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    stale = impl_->active_session;
    impl_->active_session = nullptr;
    impl_->ctx = nullptr;
    impl_->started = false;
    impl_->state = AuthState::Idle;
    impl_->prompt_active = false;
    impl_->failed_flag = false;
    impl_->cancel_armed = false;
    impl_->action_id.clear();
    impl_->message.clear();
    impl_->cookie.clear();
    impl_->input_prompt.clear();
    impl_->identity.clear();
    impl_->icon_name.clear();
    impl_->details.clear();
    impl_->info_line.clear();
    impl_->supplementary_error.clear();
    impl_->pending_begin = false;
    impl_->pending_end = false;
    impl_->state_changed = false;
  }
  if (stale) g_object_unref(stale);
}

void PolkitAuthService::set_change_callback(StateChangeHook cb) {
  impl_->on_change = std::move(cb);
}

bool PolkitAuthService::registration_done() const {
   
  std::lock_guard<std::mutex> lock(impl_->mtx);
  return impl_->register_done;
}

bool PolkitAuthService::registration_ok() const {
   
  std::lock_guard<std::mutex> lock(impl_->mtx);
  return impl_->register_ok;
}

PolkitState PolkitAuthService::snapshot() const {
   
  std::lock_guard<std::mutex> lock(impl_->mtx);
  PolkitState s;
  s.state = impl_->state;
  s.action_id = impl_->action_id;
  s.message = impl_->message;
  s.identity = impl_->identity;
  s.prompt = prompt_label_for(impl_->identity, impl_->input_prompt);
  s.icon_name = impl_->icon_name;
  s.details = impl_->details;
  s.info = impl_->info_line;
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
  bool went_idle = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->pending_end) {
      impl_->pending_end = false;
      impl_->state = AuthState::Idle;
      impl_->prompt_active = false;
      went_idle = true;
    }
    if (impl_->pending_begin) {
      impl_->pending_begin = false;
    }
    if (impl_->state_changed) {
      impl_->state_changed = false;
      did_state_change = true;
    }
  }

  if (!impl_->ctx) {
    // No GLib pumping (service never fully started), but a queued
    // transition (e.g. cancel teardown) must still notify — otherwise the
    // dialog can stick on a state change nobody delivers.
    if (did_state_change && impl_->on_change) impl_->on_change();
    return;
  }

  if (!g_main_context_acquire(impl_->ctx)) {
    // Context contended elsewhere: still deliver the queued transition so
    // the dialog can't stick; GLib traffic is picked up on the next poll.
    if (did_state_change && impl_->on_change) impl_->on_change();
    return;
  }

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
  if (went_idle) polkit_log("svc dispatch -> Idle (dialog hides)");
  if (did_state_change && impl_->on_change) {
    polkit_log("svc dispatch notifying change");
    impl_->on_change();
    polkit_log("svc dispatch notify done");
  }
}

}

#endif
