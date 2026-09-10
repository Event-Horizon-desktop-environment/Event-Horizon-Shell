#include "desktop_shell/lockscreen/session_lock.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <thread>
#include <sys/mman.h>
#include <unistd.h>

#include "configuration/shell_config.hpp"

#include <pwd.h>
#include <security/pam_appl.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-client.h>

// PAM conversation helper.

struct PamCtx {
  const char* password = nullptr;
};

static int pam_conv_fn(int nmsg, const pam_message** msg, pam_response** resp, void* appdata) {
   
  auto* ctx = static_cast<PamCtx*>(appdata);
  if (nmsg <= 0 || !resp) return PAM_CONV_ERR;
  *resp = static_cast<pam_response*>(calloc(static_cast<size_t>(nmsg), sizeof(pam_response)));
  if (!*resp) return PAM_BUF_ERR;
  for (int i = 0; i < nmsg; ++i) {
    if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
      if (ctx->password) {
        (*resp)[i].resp = strdup(ctx->password);
      }
    }
  }
  return PAM_SUCCESS;
}

static bool pam_authenticate_user(const std::string& password) {
   
  const char* user = getenv("USER");
  if (!user) {
    struct passwd* pw = getpwuid(getuid());
    if (!pw) return false;
    user = pw->pw_name;
  }

  PamCtx ctx{password.c_str()};
  pam_conv conv{&pam_conv_fn, &ctx};

  pam_handle_t* handle = nullptr;
  int ret = pam_start("system-auth", user, &conv, &handle);
  if (ret != PAM_SUCCESS) { pam_end(handle, ret); return false; }

  ret = pam_authenticate(handle, 0);
  if (ret != PAM_SUCCESS) { pam_end(handle, ret); return false; }

  ret = pam_acct_mgmt(handle, 0);
  if (ret != PAM_SUCCESS) { pam_end(handle, ret); return false; }

  pam_end(handle, PAM_SUCCESS);
  return true;
}

// LockScreen implementation.

namespace eh::shell::lockscreen {

// Static listener trampolines.

static void lock_locked_tramp(void* data, ext_session_lock_v1*) {
   
  static_cast<LockScreen*>(data)->on_locked();
}
static void lock_finished_tramp(void* data, ext_session_lock_v1*) {
   
  static_cast<LockScreen*>(data)->on_finished();
}
static constexpr ext_session_lock_v1_listener kLockListener{
  .locked = lock_locked_tramp,
  .finished = lock_finished_tramp,
};

static void surface_configure_tramp(void* data, ext_session_lock_surface_v1* ls,
                                    uint32_t serial, uint32_t w, uint32_t h) {
   
  static_cast<LockScreen*>(data)->on_configure(ls, serial, static_cast<int>(w), static_cast<int>(h));
}
static constexpr ext_session_lock_surface_v1_listener kSurfaceListener{
  .configure = surface_configure_tramp,
};

// Pointer listener trampolines.

void LockScreen::pointer_enter_tramp(void* data, wl_pointer* /*ptr*/, uint32_t /*serial*/,
                                      wl_surface* surface, wl_fixed_t /*sx*/, wl_fixed_t /*sy*/) {
  auto* self = static_cast<LockScreen*>(data);
  self->enteredSurface_ = surface;
  if (self->locked_) self->confine_to_current_surface();
}

void LockScreen::pointer_leave_tramp(void* data, wl_pointer* /*ptr*/, uint32_t /*serial*/,
                                      wl_surface* /*surface*/) {
  auto* self = static_cast<LockScreen*>(data);
  self->enteredSurface_ = nullptr;
}

void LockScreen::pointer_motion_tramp(void* data, wl_pointer* /*ptr*/, uint32_t /*time*/,
                                       wl_fixed_t /*sx*/, wl_fixed_t /*sy*/) {
  // Lock screen doesn't react to pointer motion
  (void)data;
}

void LockScreen::pointer_button_tramp(void* data, wl_pointer* /*ptr*/, uint32_t /*serial*/,
                                       uint32_t /*time*/, uint32_t /*button*/, uint32_t /*state*/) {
  // Lock screen doesn't react to pointer buttons
  (void)data;
}

void LockScreen::pointer_axis_tramp(void* data, wl_pointer* /*ptr*/, uint32_t /*time*/,
                                     uint32_t /*axis*/, wl_fixed_t /*value*/) {
  (void)data;
}

void LockScreen::pointer_frame_tramp(void* data, wl_pointer* /*ptr*/) {
  (void)data;
}

void LockScreen::pointer_axis_source_tramp(void* data, wl_pointer* /*ptr*/, uint32_t /*axis_source*/) {
  (void)data;
}

void LockScreen::pointer_axis_stop_tramp(void* data, wl_pointer* /*ptr*/, uint32_t /*time*/, uint32_t /*axis*/) {
  (void)data;
}

void LockScreen::pointer_axis_discrete_tramp(void* data, wl_pointer* /*ptr*/, uint32_t /*axis*/, int32_t /*discrete*/) {
  (void)data;
}

static void pointer_axis_value120_tramp(void* data, wl_pointer* /*ptr*/, uint32_t /*axis*/, int32_t /*value120*/) {
  (void)data;
}

static void pointer_axis_relative_direction_tramp(void* data, wl_pointer* /*ptr*/, uint32_t /*axis*/, uint32_t /*direction*/) {
  (void)data;
}

#ifdef EH_HAVE_POINTER_WARP
static void pointer_warp_tramp(void* data, wl_pointer* /*ptr*/, wl_fixed_t /*sx*/, wl_fixed_t /*sy*/) {
  (void)data;
}
#endif

static constexpr wl_pointer_listener kPointerListener{
  .enter = LockScreen::pointer_enter_tramp,
  .leave = LockScreen::pointer_leave_tramp,
  .motion = LockScreen::pointer_motion_tramp,
  .button = LockScreen::pointer_button_tramp,
  .axis = LockScreen::pointer_axis_tramp,
  .frame = LockScreen::pointer_frame_tramp,
  .axis_source = LockScreen::pointer_axis_source_tramp,
  .axis_stop = LockScreen::pointer_axis_stop_tramp,
  .axis_discrete = LockScreen::pointer_axis_discrete_tramp,
  .axis_value120 = pointer_axis_value120_tramp,
  .axis_relative_direction = pointer_axis_relative_direction_tramp,
#ifdef EH_HAVE_POINTER_WARP
  .warp = pointer_warp_tramp,
#endif
};

// Keyboard listener trampolines.

void LockScreen::kb_keymap_tramp(void* data, wl_keyboard* /*kb*/, uint32_t format, int32_t fd, uint32_t size) {
  auto* self = static_cast<LockScreen*>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    if (fd >= 0) close(fd);
    return;
  }
  char* map_str = static_cast<char*>(mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (map_str == MAP_FAILED) return;

  if (!self->xkbCtx_) self->xkbCtx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!self->xkbCtx_) {
    munmap(map_str, static_cast<size_t>(size));
    return;
  }

  const size_t map_len = size > 0 ? static_cast<size_t>(size) - 1 : 0;
  xkb_keymap* km = xkb_keymap_new_from_buffer(self->xkbCtx_, map_str, map_len,
                                              XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, static_cast<size_t>(size));
  if (!km) return;

  if (self->xkbKeymap_) xkb_keymap_unref(self->xkbKeymap_);
  if (self->owns_xkb_ && self->xkb_) xkb_state_unref(self->xkb_);
  self->xkbKeymap_ = km;
  self->xkb_ = xkb_state_new(km);
  self->owns_xkb_ = (self->xkb_ != nullptr);
}

void LockScreen::kb_enter_tramp(void* /*data*/, wl_keyboard* /*kb*/, uint32_t /*serial*/,
                                 wl_surface* /*surface*/, wl_array* /*keys*/) {}

void LockScreen::kb_leave_tramp(void* /*data*/, wl_keyboard* /*kb*/, uint32_t /*serial*/,
                                 wl_surface* /*surface*/) {}

void LockScreen::kb_key_tramp(void* data, wl_keyboard* /*kb*/, uint32_t /*serial*/, uint32_t /*time*/,
                               uint32_t keycode, uint32_t state) {
  auto* self = static_cast<LockScreen*>(data);
  if (!self->locked_) return;
  self->keyboard_key(keycode, state);
}

void LockScreen::kb_modifiers_tramp(void* data, wl_keyboard* /*kb*/, uint32_t /*serial*/, uint32_t depressed,
                                     uint32_t latched, uint32_t locked, uint32_t group) {
  auto* self = static_cast<LockScreen*>(data);
  if (!self->xkb_) return;
  const auto gl = static_cast<xkb_layout_index_t>(group);
  xkb_state_update_mask(self->xkb_, depressed, latched, locked, gl, gl, gl);
}

void LockScreen::kb_repeat_info_tramp(void* /*data*/, wl_keyboard* /*kb*/, int32_t /*rate*/, int32_t /*delay*/) {}

static constexpr wl_keyboard_listener kKeyboardListener{
  .keymap = LockScreen::kb_keymap_tramp,
  .enter = LockScreen::kb_enter_tramp,
  .leave = LockScreen::kb_leave_tramp,
  .key = LockScreen::kb_key_tramp,
  .modifiers = LockScreen::kb_modifiers_tramp,
  .repeat_info = LockScreen::kb_repeat_info_tramp,
};

// Constructor / Destructor.

LockScreen::LockScreen(wl_display* display, wl_compositor* compositor, wl_shm* shm,
                       ext_session_lock_manager_v1* lockMgr, wl_seat* seat,
                       xkb_state* xkbState,
                       zwp_pointer_constraints_v1* pointerConstraints)
    : display_(display), compositor_(compositor), shm_(shm),
      lockMgr_(lockMgr), pointerConstraints_(pointerConstraints),
      seat_(seat), xkb_(xkbState) {

  if (seat_) {
    pointer_ = wl_seat_get_pointer(seat_);
    if (pointer_) {
      wl_pointer_add_listener(pointer_, &kPointerListener, this);
    }
    keyboard_ = wl_seat_get_keyboard(seat_);
    if (keyboard_) {
      wl_keyboard_add_listener(keyboard_, &kKeyboardListener, this);
    }
  }
}

LockScreen::~LockScreen() {
   
  MANGOWM_INFO("{}", __func__);
  if (auth_thread_.joinable()) auth_thread_.join();
  unlock();
  if (keyboard_) {
    wl_keyboard_destroy(keyboard_);
    keyboard_ = nullptr;
  }
  if (owns_xkb_ && xkb_) {
    xkb_state_unref(xkb_);
    xkb_ = nullptr;
  }
  if (xkbKeymap_) {
    xkb_keymap_unref(xkbKeymap_);
    xkbKeymap_ = nullptr;
  }
  if (xkbCtx_) {
    xkb_context_unref(xkbCtx_);
    xkbCtx_ = nullptr;
  }
}

// Lock / Unlock.

bool LockScreen::lock() {
   
  if (!lockMgr_ || lockSession_) return false;
  lockSession_ = ext_session_lock_manager_v1_lock(lockMgr_);
  if (!lockSession_) return false;
  ext_session_lock_v1_add_listener(lockSession_, &kLockListener, this);
  locked_ = true;

  // Create lock surfaces for any outputs that were registered before locking
  for (auto* out : known_outputs_) {
    if (find_by_output(out)) continue;
    auto sd = std::make_unique<SurfaceData>();
    sd->output = out;
    sd->surface = wl_compositor_create_surface(compositor_);
    if (!sd->surface) continue;
    sd->lockSurface = ext_session_lock_v1_get_lock_surface(lockSession_, sd->surface, out);
    if (!sd->lockSurface) {
      wl_surface_destroy(sd->surface);
      continue;
    }
    ext_session_lock_surface_v1_add_listener(sd->lockSurface, &kSurfaceListener, this);
    wl_surface_commit(sd->surface);
    surfaces_.push_back(std::move(sd));
  }

  // Confine pointer to the lock surface so the cursor cannot escape
  confine_to_current_surface();

  // Ensure the lock request and initial surface commits are sent immediately
  if (display_) {
    wl_display_flush(display_);
  }
  return true;
}

void LockScreen::unlock() {
    
  if (display_ && wl_display_get_error(display_)) {
    lockSession_ = nullptr;
    locked_ = false;
    surfaces_.clear();
    return;
  }
  unconfine_pointer();
  test_mode_ = false;
  test_unlock_deadline_ms_ = 0;
  if (lockSession_) {
    if (locked_) {
      ext_session_lock_v1_unlock_and_destroy(lockSession_);
    } else {
      for (auto& sd : surfaces_) {
        if (sd->lockSurface) ext_session_lock_surface_v1_destroy(sd->lockSurface);
        sd->lockSurface = nullptr;
      }
      ext_session_lock_v1_destroy(lockSession_);
    }
    lockSession_ = nullptr;
  }
  locked_ = false;
  for (auto& sd : surfaces_) {
    if (sd->surface) wl_surface_destroy(sd->surface);
    sd->surface = nullptr;
  }
  surfaces_.clear();
}

// Pointer confinement helpers.

void LockScreen::confine_to_current_surface() {
  if (!pointer_ || !pointerConstraints_) return;
  wl_surface* target = nullptr;
  // Only confine to surfaces we own
  if (enteredSurface_ && find_by_surface(enteredSurface_))
    target = enteredSurface_;
  else
    target = any_surface();
  if (!target) return;
  pointerConfine_.confine(pointerConstraints_, target, pointer_, nullptr, 1);
}

void LockScreen::unconfine_pointer() {
  pointerConfine_.unconfine();
}

// Output management.

void LockScreen::add_output(wl_output* output) {
   
  if (!output) return;
  if (std::find(known_outputs_.begin(), known_outputs_.end(), output) != known_outputs_.end()) return;
  known_outputs_.push_back(output);
  if (!lockSession_) return;
  if (find_by_output(output)) return;

  auto sd = std::make_unique<SurfaceData>();
  sd->output = output;
  sd->surface = wl_compositor_create_surface(compositor_);
  if (!sd->surface) return;
  sd->lockSurface = ext_session_lock_v1_get_lock_surface(lockSession_, sd->surface, output);
  if (!sd->lockSurface) {
    wl_surface_destroy(sd->surface);
    return;
  }
  ext_session_lock_surface_v1_add_listener(sd->lockSurface, &kSurfaceListener, this);
  wl_surface_commit(sd->surface);
  surfaces_.push_back(std::move(sd));
}

void LockScreen::remove_output(wl_output* output) {
   
  auto kit = std::remove(known_outputs_.begin(), known_outputs_.end(), output);
  known_outputs_.erase(kit, known_outputs_.end());

  auto it = std::remove_if(surfaces_.begin(), surfaces_.end(),
      [output](const auto& sd) { return sd->output == output; });
  surfaces_.erase(it, surfaces_.end());
}

void LockScreen::sync_outputs(const std::vector<wl_output*>& outputs) {
   
  std::vector<wl_output*> toRemove;
  for (wl_output* known : known_outputs_) {
    if (std::find(outputs.begin(), outputs.end(), known) == outputs.end())
      toRemove.push_back(known);
  }
  for (wl_output* out : toRemove) remove_output(out);
  for (wl_output* out : outputs) add_output(out);
}

// Protocol handlers.

void LockScreen::on_locked() {
   
  locked_ = true;
  confine_to_current_surface();
}

void LockScreen::on_finished() {
   
  locked_ = false;
  test_mode_ = false;
  test_unlock_deadline_ms_ = 0;
  unconfine_pointer();
  lockSession_ = nullptr;
  for (auto& sd : surfaces_) {
    if (sd->surface) wl_surface_destroy(sd->surface);
    sd->surface = nullptr;
    sd->lockSurface = nullptr;
  }
  surfaces_.clear();
}

void LockScreen::on_configure(ext_session_lock_surface_v1* ls, uint32_t serial, int w, int h) {
   
  for (auto& sd : surfaces_) {
    if (sd->lockSurface != ls) continue;
    ext_session_lock_surface_v1_ack_configure(ls, serial);
    sd->cfgW = w;
    sd->cfgH = h;
    sd->configured = true;
    paint_surface(*sd);
    return;
  }
}

// Surface lookup.

LockScreen::SurfaceData* LockScreen::find_by_surface(wl_surface* surf) {
   
  for (auto& sd : surfaces_)
    if (sd->surface == surf) return sd.get();
  return nullptr;
}

LockScreen::SurfaceData* LockScreen::find_by_output(wl_output* output) {
   
  for (auto& sd : surfaces_)
    if (sd->output == output) return sd.get();
  return nullptr;
}

wl_surface* LockScreen::any_surface() const {
   
  for (auto& sd : surfaces_)
    if (sd->surface) return sd->surface;
  return nullptr;
}

wl_surface* LockScreen::surface_at(double x, double y) const {
   
  (void)x; (void)y;
  // Any surface covers the whole output
  for (auto& sd : surfaces_)
    if (sd->surface) return sd->surface;
  return nullptr;
}

// Painting.

static void draw_clock(cairo_t* cr, int w, int h) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);

  char timeStr[16], dateStr[64];
  strftime(timeStr, sizeof(timeStr), "%I:%M %p", &lt);
  strftime(dateStr, sizeof(dateStr), "%A, %B %d", &lt);

  // Clock
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 72.0 * us);
  cairo_text_extents_t te;
  cairo_text_extents(cr, timeStr, &te);
  cairo_move_to(cr, (w - te.width) / 2.0, h * 0.42);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
  cairo_show_text(cr, timeStr);

  // Date
  if (dateStr[0]) {
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 20.0 * us);
    cairo_text_extents(cr, dateStr, &te);
    cairo_move_to(cr, (w - te.width) / 2.0, h * 0.42 + 90.0 * us);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
    cairo_show_text(cr, dateStr);
  }
}

static void draw_password_field(cairo_t* cr, int w, int h, const std::string& pw,
                                const std::string& status) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double fieldW = 360.0 * us;
  const double fieldH = 48.0 * us;
  const double fieldX = (w - fieldW) / 2.0;
  const double fieldY = h * 0.62;
  const double r = 10.0 * us;

  // Hint text above field
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 16.0 * us);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.6);
  cairo_text_extents_t te;
  cairo_text_extents(cr, "Enter password to unlock", &te);
  cairo_move_to(cr, (w - te.width) / 2.0, fieldY - 12.0 * us);
  cairo_show_text(cr, "Enter password to unlock");

  // Field background
  cairo_set_source_rgba(cr, 0.12, 0.12, 0.14, 0.9);
  cairo_new_path(cr);
  cairo_arc(cr, fieldX + r, fieldY + r, r, M_PI, 3 * M_PI / 2);
  cairo_arc(cr, fieldX + fieldW - r, fieldY + r, r, 3 * M_PI / 2, 2 * M_PI);
  cairo_arc(cr, fieldX + fieldW - r, fieldY + fieldH - r, r, 0, M_PI / 2);
  cairo_arc(cr, fieldX + r, fieldY + fieldH - r, r, M_PI / 2, M_PI);
  cairo_close_path(cr);
  cairo_fill(cr);

  // Field border
  cairo_set_source_rgba(cr, 1, 1, 1, 0.3);
  cairo_set_line_width(cr, 1.5);
  cairo_new_path(cr);
  cairo_arc(cr, fieldX + r, fieldY + r, r, M_PI, 3 * M_PI / 2);
  cairo_arc(cr, fieldX + fieldW - r, fieldY + r, r, 3 * M_PI / 2, 2 * M_PI);
  cairo_arc(cr, fieldX + fieldW - r, fieldY + fieldH - r, r, 0, M_PI / 2);
  cairo_arc(cr, fieldX + r, fieldY + fieldH - r, r, M_PI / 2, M_PI);
  cairo_close_path(cr);
  cairo_stroke(cr);

  // Password text (bullets)
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 20.0 * us);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.9);

  std::string display(pw.size(), '*');
  if (pw.empty()) {
    cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
    display = "Type password...";
  }

  cairo_text_extents(cr, display.c_str(), &te);
  cairo_move_to(cr, fieldX + 16.0 * us, fieldY + (fieldH - te.height) / 2.0 + te.height);
  cairo_show_text(cr, display.c_str());

  // Status message below field
  if (!status.empty()) {
    cairo_set_font_size(cr, 14.0 * us);
    cairo_text_extents(cr, status.c_str(), &te);
    cairo_move_to(cr, (w - te.width) / 2.0, fieldY + fieldH + 28.0 * us);
    cairo_set_source_rgba(cr, 1, 0.3, 0.3, 0.9);
    cairo_show_text(cr, status.c_str());
  }
}

static void draw_unlock_message(cairo_t* cr, int w, int h) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 24.0 * us);
  cairo_set_source_rgba(cr, 0.3, 0.9, 0.3, 1.0);
  cairo_text_extents_t te;
  cairo_text_extents(cr, "Unlocking...", &te);
  cairo_move_to(cr, (w - te.width) / 2.0, h * 0.75);
  cairo_show_text(cr, "Unlocking...");
}

static void draw_lock_hints(cairo_t* cr, int w, int h, bool is_test, uint64_t deadline_ms) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double y = h * 0.88;

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 11.0 * us);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.35);

  cairo_text_extents_t te;

  if (is_test) {
    // Countdown
    uint64_t remaining = 0;
    if (deadline_ms > 0) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      uint64_t now = static_cast<uint64_t>(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
      if (now < deadline_ms) remaining = (deadline_ms - now) / 1000ULL;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "TEST MODE — auto-unlocking in ~%llu s (or press Esc)", (unsigned long long)remaining);
    cairo_text_extents(cr, buf, &te);
    cairo_move_to(cr, (w - te.width) / 2.0, y);
    cairo_set_source_rgba(cr, 0.4, 0.9, 0.6, 0.9);
    cairo_show_text(cr, buf);
  }

  // Always show escape help
  const char* esc = "Ctrl+Alt+Delete = force emergency unlock   •   From TTY/SSH: eh-ipc unlock";
  cairo_text_extents(cr, esc, &te);
  cairo_move_to(cr, (w - te.width) / 2.0, y + 18.0 * us);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.28);
  cairo_show_text(cr, esc);
}

void LockScreen::paint_surface(SurfaceData& sd) {
   
  if (!sd.configured || sd.cfgW <= 0 || sd.cfgH <= 0) return;

  const int bufW = sd.cfgW;
  const int bufH = sd.cfgH;

  if (!sd.buf.ensure(shm_, "lock-screen", bufW, bufH)) return;
  cairo_t* cr = sd.buf.cairo();
  cairo_save(cr);

  // Background (slightly different tint in test mode for visibility)
  if (test_mode_) {
    cairo_set_source_rgba(cr, 0.08, 0.12, 0.08, 1.0);
  } else {
    cairo_set_source_rgba(cr, 0.10, 0.10, 0.12, 1.0);
  }
  cairo_paint(cr);

  draw_clock(cr, bufW, bufH);

  if (auth_pending_.load(std::memory_order_acquire)) {
    draw_unlock_message(cr, bufW, bufH);
  } else {
    draw_password_field(cr, bufW, bufH, password_, status_);
  }

  draw_lock_hints(cr, bufW, bufH, test_mode_, test_unlock_deadline_ms_);

  cairo_restore(cr);

  wl_surface_attach(sd.surface, sd.buf.wl(), 0, 0);
  wl_surface_damage_buffer(sd.surface, 0, 0, bufW, bufH);
  wl_surface_commit(sd.surface);
}

void LockScreen::paint_all() {
   
  for (auto& sd : surfaces_) paint_surface(*sd);
  wl_display_flush(display_);
}

// Input forwarding.

void LockScreen::pointer_motion(double sx, double sy) {
   
  (void)sx; (void)sy;
}

void LockScreen::pointer_button(uint32_t button, uint32_t state) {
   
  (void)button; (void)state;
}

bool LockScreen::keyboard_key(uint32_t keycode, uint32_t state) {
   
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return true;
  if (!xkb_) return true;
  if (auth_pending_.load(std::memory_order_acquire)) return true;

  const xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_, keycode + 8);
  const uint32_t mods = xkb_state_serialize_mods(xkb_, XKB_STATE_MODS_DEPRESSED);

  // Ctrl+Alt+Delete emergency unlock (bypasses PAM)
  if (sym == XKB_KEY_Delete && (mods & 4) && (mods & 8)) {
    unlock();
    return true;
  }

  if (sym == XKB_KEY_Escape) {
    if (test_mode_) {
      test_mode_ = false;
      test_unlock_deadline_ms_ = 0;
      status_ = "Test aborted";
      unlock();
      wl_display_flush(display_);
      return true;
    }
    password_.clear();
    status_.clear();
    paint_all();
    return true;
  }

  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    if (test_mode_) {
      test_mode_ = false;
      test_unlock_deadline_ms_ = 0;
      status_ = "Test unlock";
      unlock();
      wl_display_flush(display_);
      return true;
    }
    if (!password_.empty()) {
      tried_auth_ = true;
      status_.clear();
      paint_all();
      try_authenticate();
    }
    return true;
  }

  if (sym == XKB_KEY_BackSpace) {
    if (!password_.empty()) password_.pop_back();
    paint_all();
    return true;
  }

  // Check for Ctrl+U (clear line)
  if (sym == XKB_KEY_u && (mods & 4)) {
    password_.clear();
    paint_all();
    return true;
  }

  // Normal text input
  char buf[8] = {};
  int len = xkb_state_key_get_utf8(xkb_, keycode + 8, buf, sizeof(buf));
  if (len > 0 && buf[0] >= 32 && buf[0] < 127 && password_.size() < 128) {
    password_.push_back(buf[0]);
    paint_all();
  }

  return true;
}

// PAM auth in background thread.

void LockScreen::try_authenticate() {
   
  std::string pw = password_;
  auth_pending_.store(true, std::memory_order_release);
  password_.clear();

  if (auth_thread_.joinable()) auth_thread_.join();
  auth_thread_ = std::thread([this, pw]() {
    bool ok = pam_authenticate_user(pw);
    auth_ok_.store(ok, std::memory_order_release);
    auth_pending_.store(false, std::memory_order_release);
  });
}

bool LockScreen::test_lock(uint32_t auto_unlock_ms) {
   
  if (!lock()) return false;
  test_mode_ = true;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  test_unlock_deadline_ms_ = (static_cast<uint64_t>(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL) + auto_unlock_ms;
  status_ = "TEST MODE - auto-unlock soon";
  paint_all();
  return true;
}

// Called from main loop idle flush.

void LockScreen::on_idle_flush() {
   
  if (!locked_) return;

  if (!auth_pending_.load(std::memory_order_acquire) && tried_auth_) {
    if (auth_ok_.load(std::memory_order_acquire)) {
      unlock();
      wl_display_flush(display_);
      return;
    } else {
      status_ = "Authentication failed";
      tried_auth_ = false;
      paint_all();
    }
  }

  // Safe test mode auto-unlock
  if (test_mode_ && test_unlock_deadline_ms_ != 0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t nowMs = static_cast<uint64_t>(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
    if (nowMs >= test_unlock_deadline_ms_) {
      test_mode_ = false;
      test_unlock_deadline_ms_ = 0;
      status_ = "Test unlock";
      unlock();
      wl_display_flush(display_);
      return;
    }
  }

  // Repaint every ~second to update clock display (and countdown in test mode)
  static uint64_t lastPaintMs = 0;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const uint64_t nowMs = static_cast<uint64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
  if (nowMs - lastPaintMs >= 1000) {
    lastPaintMs = nowMs;
    paint_all();
  }
}

} // namespace eh::shell::lockscreen
