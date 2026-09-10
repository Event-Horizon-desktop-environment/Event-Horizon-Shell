#include "desktop_shell/notifications/host/notification_toast_host.hpp"

#include "desktop_shell/notifications/core/notifications.hpp"
#include "desktop_shell/notifications/types/notification_types.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"
#include "wl/surface/layer_surface.hpp"
#include "m3/core/primitives/box.hpp"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <wayland-client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_set>

namespace eh::shell::notifications {

namespace {

constexpr int kMaxCards = 4;
constexpr std::chrono::milliseconds kFrameCallbackStall{250};
inline double kCardW() { return 360.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kCardH() { return 128.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kCardGap() { return 8.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kPad() { return 18.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kIconTextGap() { return 22.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kIconSz() { return 96.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kIconRad() { return 14.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
constexpr double kProgressH = 3.0;
inline double kCardRad() { return 16.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kCloseBtnSz() { return 20.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kHeaderGap() { return 6.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }
inline double kSummaryBodyGap() { return 4.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock); }

static void toast_layer_configure(void* data, zwlr_layer_surface_v1*, uint32_t serial,
                                  uint32_t width, uint32_t height) {
   
  auto* self = static_cast<NotificationToastHost*>(data);
  self->layer_configure(serial, width, height);
}

static void toast_layer_closed(void* data, zwlr_layer_surface_v1*) {
   
  auto* self = static_cast<NotificationToastHost*>(data);
  if (self) self->destroy_layer();
}

static constexpr zwlr_layer_surface_v1_listener kLayerListener = {
  .configure = toast_layer_configure,
  .closed = toast_layer_closed,
};

static void toast_buffer_release(void* data) {
   
  auto* self = static_cast<NotificationToastHost*>(data);
  self->shm_buffer_released();
}

static void toast_pointer_enter(void* data, wl_pointer*, uint32_t, wl_surface* surface,
                                 wl_fixed_t sx, wl_fixed_t sy) {
   
  auto* self = static_cast<NotificationToastHost*>(data);
  if (self->surface() == surface) {
    self->pointer_motion(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
  }
}

static void toast_pointer_leave(void*, wl_pointer*, uint32_t, wl_surface*) {
   
}

static void toast_pointer_motion(void* data, wl_pointer*, uint32_t,
                                  wl_fixed_t sx, wl_fixed_t sy) {
   
  auto* self = static_cast<NotificationToastHost*>(data);
  self->pointer_motion(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
}

static void toast_pointer_button(void* data, wl_pointer*, uint32_t, uint32_t,
                                  uint32_t button, uint32_t state) {
   
  if (button != 0x110 || state != WL_POINTER_BUTTON_STATE_PRESSED) return;
  auto* self = static_cast<NotificationToastHost*>(data);
  self->pointer_button_press();
}

static void toast_pointer_axis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {
   
}
static void toast_pointer_frame(void*, wl_pointer*) {
   
}
static void toast_pointer_axis_source(void*, wl_pointer*, uint32_t) {
   
}
static void toast_pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {
   
}
static void toast_pointer_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) {
   
}
static void toast_pointer_axis_value120(void*, wl_pointer*, uint32_t, int32_t) {
   
}
static void toast_pointer_axis_relative_direction(void*, wl_pointer*, uint32_t, uint32_t) {

}
#ifdef EH_HAVE_POINTER_WARP
static void toast_pointer_warp(void* data, wl_pointer* p, wl_fixed_t sx, wl_fixed_t sy) {
  toast_pointer_motion(data, p, 0, sx, sy);
}
#endif

static void toast_frame_done(void* data, wl_callback* cb, uint32_t /*compositor_time_ms*/) {
   
  auto* self = static_cast<NotificationToastHost*>(data);
  wl_callback_destroy(cb);
  self->frame_callback_done();
}

static constexpr wl_callback_listener kFrameListener = {
  .done = toast_frame_done,
};

static constexpr wl_pointer_listener kPointerListener = {
  .enter = toast_pointer_enter,
  .leave = toast_pointer_leave,
  .motion = toast_pointer_motion,
  .button = toast_pointer_button,
  .axis = toast_pointer_axis,
  .frame = toast_pointer_frame,
  .axis_source = toast_pointer_axis_source,
  .axis_stop = toast_pointer_axis_stop,
  .axis_discrete = toast_pointer_axis_discrete,
  .axis_value120 = toast_pointer_axis_value120,
  .axis_relative_direction = toast_pointer_axis_relative_direction,
#ifdef EH_HAVE_POINTER_WARP
  .warp = toast_pointer_warp,
#endif
};

}

NotificationToastHost::NotificationToastHost(NotificationManager& manager)
    : mgr_(manager), lastFrameDone_(Clock::now()) {
    
}

NotificationToastHost::~NotificationToastHost() { shutdown(); }

void NotificationToastHost::initialize(wl_compositor* compositor, wl_shm* shm,
                                        zwlr_layer_shell_v1* layerShell,
                                        wl_seat* seat, wl_output* output) {
   
  MANGOWM_INFO("{}", __func__);
  compositor_ = compositor;
  shm_ = shm;
  layerShell_ = layerShell;
  seat_ = seat;
  output_ = output;

  if (seat_) {
    pointer_ = wl_seat_get_pointer(seat_);
    if (pointer_) wl_pointer_add_listener(pointer_, &kPointerListener, this);
  }
}

void NotificationToastHost::shutdown() {
   
  destroy_layer();
  if (pointer_) {
    wl_pointer_release(pointer_);
    pointer_ = nullptr;
  }
}

bool NotificationToastHost::apply_config(const eh::config::ShellNotificationsToastSettings& config) {
   
  const bool posChanged = cfg_.position != config.position;
  const bool marginChanged = cfg_.marginPx != config.marginPx;
  cfg_ = config;
  if (!cfg_.layerShellEnabled) {
    shutdown();
    return true;
  }
  if (surface_ && layer_ && (posChanged || marginChanged)) {
    zwlr_layer_surface_v1_set_anchor(layer_, anchor_for_position(cfg_.position));
    zwlr_layer_surface_v1_set_margin(layer_, cfg_.marginPx, cfg_.marginPx, cfg_.marginPx, cfg_.marginPx);
    configured_ = false;
    configuredWidth_ = 0;
    configuredHeight_ = 0;
  }
  if (!mgr_.all().empty()) {
    on_notifications_changed();
  }
  return true;
}

void NotificationToastHost::create_layer() {
   
  if (!compositor_ || !layerShell_ || !output_) {
    eh::shell_log::notif_verbose("create_layer: missing compositor/layerShell/output");
    return;
  }
  if (surface_) {
    eh::shell_log::notif_verbose("create_layer: already exists");
    return;
  }

  eh::wayland::LayerSurfaceConfig lc{};
  ++layer_gen_;
  lc.nameSpace = kNotificationToastNamespace;
  lc.layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  lc.anchor = anchor_for_position(cfg_.position);
  lc.width = static_cast<std::uint32_t>(std::max(1, layoutWidth_));
  lc.height = static_cast<std::uint32_t>(std::max(1, layoutHeight_));
  lc.exclusiveZone = 0;
  lc.marginTop = cfg_.marginPx;
  lc.marginRight = cfg_.marginPx;
  lc.marginBottom = cfg_.marginPx;
  lc.marginLeft = cfg_.marginPx;
  lc.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  if (!eh::wayland::create_layer_surface(compositor_, layerShell_, output_, lc,
                                          &kLayerListener, this,
                                          &surface_, &layer_)) {
    eh::shell_log::notifications("create_layer: failed to create layer surface (size=", layoutWidth_, "x", layoutHeight_, ")");
    surface_ = nullptr;
    layer_ = nullptr;
    return;
  }

  eh::shell_log::notif_verbose("create_layer: created surface (size=", layoutWidth_, "x", layoutHeight_, ")");
  wl_surface_commit(surface_);
  if (auto* dpy = wl_proxy_get_display(reinterpret_cast<wl_proxy*>(layer_)))
    wl_display_roundtrip(dpy);
  shmBuf_.set_release_hook(toast_buffer_release, this);
}

void NotificationToastHost::destroy_layer() {
   
  eh::shell_log::notif_verbose("destroy_layer");
  if (frameCallback_) {
    wl_callback_destroy(frameCallback_);
    frameCallback_ = nullptr;
  }
  cards_.clear();
  layoutWidth_ = 0;
  layoutHeight_ = 0;
  configured_ = false;
  configuredWidth_ = 0;
  configuredHeight_ = 0;
  configured_gen_ = 0;
  shmBuf_.set_release_hook(nullptr, nullptr);
  if (layer_) {
    zwlr_layer_surface_v1_destroy(layer_);
    layer_ = nullptr;
  }
  shmBuf_.destroy();
  if (surface_) {
    wl_surface_destroy(surface_);
    surface_ = nullptr;
  }
}

void NotificationToastHost::layer_configure(uint32_t serial, uint32_t width, uint32_t height) {
   
  if (layer_) {
    zwlr_layer_surface_v1_ack_configure(layer_, serial);
  }
  if (width > 0) configuredWidth_ = static_cast<int>(width);
  if (height > 0) configuredHeight_ = static_cast<int>(height);
  configured_ = configuredWidth_ > 0 && configuredHeight_ > 0;
  if (configured_) {
    configured_gen_ = layer_gen_;
    on_notifications_changed();
  }
}

void NotificationToastHost::shm_buffer_released() {
   
  if (wantRepaint_) {
    wantRepaint_ = false;
    paint_impl();
  }
}

void NotificationToastHost::frame_callback_done() {
    
  frameCallback_ = nullptr;
  lastFrameDone_ = Clock::now();
  paint_impl();
}

bool NotificationToastHost::tick_animations() {
    
  bool animating = false;
  for (const auto& nn : mgr_.all()) {
    if (nn.expiryTime) {
      animating = true;
      break;
    }
  }
  if (!animating) return false;

  // While the compositor is delivering frame callbacks it drives repaints at
  // refresh rate; only fall back to a timer-driven repaint once the callback
  // has been stuck (surface not being presented / not focused) for a while.
  if (frameCallback_) {
    const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - lastFrameDone_);
    if (age < kFrameCallbackStall) return true;
  }
  paint_impl();
  return true;
}

std::uint32_t NotificationToastHost::anchor_for_position(const std::string& position) {
   
  if (position == "top_right") return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  if (position == "top_left") return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
  if (position == "bottom_right") return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  if (position == "bottom_left") return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
  if (position == "top_center") return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
  if (position == "bottom_center") return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
  return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
}

void NotificationToastHost::on_notifications_changed() {
   
  if (!cfg_.layerShellEnabled) {
    eh::shell_log::notif_verbose("on_notifications_changed: layerShell disabled");
    return;
  }

  const auto& all = mgr_.all();
  const bool dnd = mgr_.doNotDisturb();

  cards_.clear();
  int img_count = 0;
  std::unordered_set<std::string> seen_apps;
  std::unordered_set<std::string> seen_content;
  for (auto it = all.rbegin(); it != all.rend(); ++it) {
    if (dnd) continue;
    if (static_cast<int>(cards_.size()) >= kMaxCards) break;
    if (it->imageData) img_count++;
    if (!seen_apps.insert(it->appName).second) continue;
    std::string content = it->summary;
    content.push_back('\x1e');
    content += it->body;
    if (!seen_content.insert(std::move(content)).second) continue;
    cards_.push_back({it->id, 0, 0, kCardW(), kCardH()});
  }

  eh::shell_log::notif_verbose("on_notifications_changed: total=", all.size(), " shown=", cards_.size(), " with_image=", img_count, " dnd=", dnd);

  if (cards_.empty()) {
    if (surface_) {
      eh::shell_log::notif_verbose("on_notifications_changed: hiding surface");
      if (frameCallback_) {
        wl_callback_destroy(frameCallback_);
        frameCallback_ = nullptr;
      }
      wantRepaint_ = false;
      wl_surface_attach(surface_, nullptr, 0, 0);
      wl_surface_commit(surface_);
      configured_ = false;
      configured_gen_ = 0;
    }
    return;
  }

  compute_layout();

  if (!surface_ || !layer_) {
    create_layer();
    if (!surface_ || !layer_) return;
    return;
  }

  if (!configured_ || layoutWidth_ != configuredWidth_ || layoutHeight_ != configuredHeight_) {
    eh::shell_log::notif_verbose("on_notifications_changed: resizing to ", layoutWidth_, "x", layoutHeight_,
                                 " (was ", configuredWidth_, "x", configuredHeight_, ")");
    if (layer_) {
      zwlr_layer_surface_v1_set_size(layer_,
                                      static_cast<std::uint32_t>(std::max(1, layoutWidth_)),
                                      static_cast<std::uint32_t>(std::max(1, layoutHeight_)));
    }
    wl_surface_commit(surface_);
    if (auto* dpy = wl_proxy_get_display(reinterpret_cast<wl_proxy*>(layer_)))
      wl_display_roundtrip(dpy);
    return;
  }

  paint_impl();
}

void NotificationToastHost::compute_layout() {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double minW = 360.0 * us;
  const double cardH = 140.0 * us;

  double cardW = minW;
  {
    void* tmp_cr_ctx = nullptr;
    auto get_tmp_cr = [&]() -> cairo_t* {
      if (!tmp_cr_ctx) {
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
        tmp_cr_ctx = cairo_create(s);
        cairo_surface_destroy(s);
      }
      return static_cast<cairo_t*>(tmp_cr_ctx);
    };
    auto free_tmp_cr = [&]() {
      if (tmp_cr_ctx) {
        cairo_destroy(static_cast<cairo_t*>(tmp_cr_ctx));
        tmp_cr_ctx = nullptr;
      }
    };

    for (auto& card : cards_) {
      const auto* n = [&]() -> const Notification* {
        for (const auto& nn : mgr_.all()) if (nn.id == card.id) return &nn;
        return nullptr;
      }();
      if (!n || n->summary.empty()) continue;

      cairo_t* cr = get_tmp_cr();
      auto* layout = pango_cairo_create_layout(cr);
      auto* desc = pango_font_description_from_string("Sans");
      pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
      pango_font_description_set_size(desc, static_cast<int>(14.0 * PANGO_SCALE * us + 0.5));
      pango_layout_set_font_description(layout, desc);
      pango_layout_set_text(layout, n->summary.c_str(), -1);

      PangoRectangle log;
      pango_layout_get_extents(layout, nullptr, &log);
      const double titleW = log.width / static_cast<double>(PANGO_SCALE);

      pango_font_description_free(desc);
      g_object_unref(layout);

      const double needed = kPad() + kIconSz() + kIconTextGap() + titleW +
                            kHeaderGap() + kCloseBtnSz() + kPad();
      cardW = std::max(cardW, needed);
    }
    free_tmp_cr();
  }

  const double totalH = static_cast<double>(cards_.size()) * cardH +
                        static_cast<double>(std::max(0, static_cast<int>(cards_.size()) - 1)) * kCardGap() +
                        kPad() * 2.0;
  layoutWidth_ = static_cast<int>(cardW + kPad() * 2.0);
  layoutHeight_ = static_cast<int>(std::ceil(totalH));

  const bool stackBottom = cfg_.position.rfind("bottom", 0) == 0;
  double y = stackBottom ? (static_cast<double>(layoutHeight_) - kPad() - cardH) : kPad();
  for (auto& card : cards_) {
    card.x = kPad();
    card.y = y;
    card.w = cardW;
    card.h = cardH;
    if (stackBottom) y -= (cardH + kCardGap());
    else y += (cardH + kCardGap());
  }
}

static cairo_surface_t* notification_image_to_cairo_surface(const NotificationImageData& img) {
   
  if (img.width <= 0 || img.height <= 0 || img.data.empty()) {
    eh::shell_log::notif_verbose("notification_image_to_cairo_surface: empty image");
    return nullptr;
  }
  if (img.channels != 4 && img.channels != 3) {
    eh::shell_log::notif_verbose("notification_image_to_cairo_surface: unsupported channels=", img.channels);
    return nullptr;
  }
  if (img.bitsPerSample != 8) {
    eh::shell_log::notif_verbose("notification_image_to_cairo_surface: unsupported bps=", img.bitsPerSample);
    return nullptr;
  }

  cairo_format_t fmt = img.hasAlpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
  cairo_surface_t* surf = cairo_image_surface_create(fmt, img.width, img.height);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    eh::shell_log::notifications("notification_image_to_cairo_surface: cairo surface creation failed (", img.width, "x", img.height, ")");
    if (surf) cairo_surface_destroy(surf);
    return nullptr;
  }

  eh::shell_log::notif_verbose("notification_image_to_cairo_surface: converted ", img.width, "x", img.height, " channels=", img.channels, " hasAlpha=", img.hasAlpha);

  unsigned char* dst = cairo_image_surface_get_data(surf);
  const int dst_stride = cairo_image_surface_get_stride(surf);
  const int src_pb = img.channels;
  const int src_stride = img.rowStride > 0 ? img.rowStride : img.width * src_pb;

  // NotificationImageData is [R, G, B, A] (RGBA) or [R, G, B] (RGB)
  // Cairo CAIRO_FORMAT_ARGB32 on LE is [B, G, R, A]
  // Convert: swap byte[0] and byte[2]
  for (int y = 0; y < img.height; y++) {
    const uint8_t* src = img.data.data() + static_cast<size_t>(y) * static_cast<size_t>(src_stride);
    uint8_t* d = dst + static_cast<size_t>(y) * static_cast<size_t>(dst_stride);
    for (int x = 0; x < img.width; x++) {
      const size_t off = static_cast<size_t>(x) * 4;
      d[off + 0] = src[off + 2];
      d[off + 1] = src[off + 1];
      d[off + 2] = src[off + 0];
      d[off + 3] = (src_pb >= 4) ? src[off + 3] : 0xFF;
    }
  }

  cairo_surface_mark_dirty(surf);
  return surf;
}

static void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
   
  const double degrees = M_PI / 180.0;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -90 * degrees, 0 * degrees);
  cairo_arc(cr, x + w - r, y + h - r, r, 0 * degrees, 90 * degrees);
  cairo_arc(cr, x + r, y + h - r, r, 90 * degrees, 180 * degrees);
  cairo_arc(cr, x + r, y + r, r, 180 * degrees, 270 * degrees);
  cairo_close_path(cr);
}

void NotificationToastHost::paint_impl() {
   
  if (!surface_ || !layer_ || !configured_ || configured_gen_ != layer_gen_) {
    eh::shell_log::notif_verbose("paint_impl: not ready (surface=", !!surface_, " layer=", !!layer_, " configured=", configured_, " gen=", configured_gen_, "/", layer_gen_, ")");
    return;
  }
  if (configuredWidth_ <= 0 || configuredHeight_ <= 0) return;
  if (shmBuf_.busy()) { wantRepaint_ = true; eh::shell_log::notif_verbose("paint_impl: buffer busy, deferring"); return; }

  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const int w = configuredWidth_;
  const int h = configuredHeight_;
  eh::shell_log::notif_verbose("paint_impl: painting ", cards_.size(), " cards on ", w, "x", h);
  if (!shmBuf_.ensure(shm_, eh::shell::kNotificationToastNamespace, w, h)) {
    eh::shell_log::notifications("paint_impl: shm buffer allocation failed (", w, "x", h, ")");
    return;
  }
  shmBuf_.set_release_hook(toast_buffer_release, this);

  cairo_t* cr = shmBuf_.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  const auto& cfgSnap = eh::config::shell_config_snapshot();
  const auto chrome = eh::config::derived_chrome_colors(cfgSnap.appearance);
  const double notifOv = static_cast<double>(
      eh::config::overlay_surface_alpha_scale(cfgSnap, eh::config::OverlaySurfaceAlphaKind::Notifications));

  for (const auto& card : cards_) {
    const auto* n = [&]() -> const Notification* {
      for (const auto& nn : mgr_.all()) if (nn.id == card.id) return &nn;
      return nullptr;
    }();
    if (!n) continue;

    const double cx = card.x;
    const double cy = card.y;
    const double cw = card.w;
    const double ch = card.h;
    const bool critical = (n->urgency == Urgency::Critical);

    // Card background
    m3::Box cardBox;
    if (critical) {
      cardBox.setColor(static_cast<float>(chrome.notifCriticalBgR * 0.5), static_cast<float>(chrome.notifCriticalBgG * 0.5),
                       static_cast<float>(chrome.notifCriticalBgB * 0.5), static_cast<float>(0.78 * notifOv));
    } else {
      cardBox.setColor(static_cast<float>(chrome.dockFillR * 0.35), static_cast<float>(chrome.dockFillG * 0.35),
                       static_cast<float>(chrome.dockFillB * 0.35), static_cast<float>(0.78 * notifOv));
    }
    cardBox.setRadius(static_cast<float>(kCardRad()));
    cardBox.setGeometry(static_cast<float>(cx), static_cast<float>(cy),
                        static_cast<float>(cw), static_cast<float>(ch));
    cardBox.setGlassy(true);
    cardBox.paint(cr);

    // Card border
    draw_rounded_rect(cr, cx + 0.5, cy + 0.5, cw - 1.0, ch - 1.0, kCardRad());
    if (critical) {
      cairo_set_source_rgba(cr, chrome.notifCriticalOutlineR, chrome.notifCriticalOutlineG, chrome.notifCriticalOutlineB, 0.85);
      cairo_set_line_width(cr, 1.5);
    } else {
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
      cairo_set_line_width(cr, 1.0);
    }
    cairo_stroke(cr);

    // Icon area
    const double iconX = cx + kPad();
    const double iconY = cy + kPad() + 2.0 * us;
    bool painted_icon = false;
    if (n->imageData && n->imageData->width > 0 && n->imageData->height > 0) {
      if (cairo_surface_t* img_surf = notification_image_to_cairo_surface(*n->imageData)) {
        const int iw = cairo_image_surface_get_width(img_surf);
        const int ih = cairo_image_surface_get_height(img_surf);
        const double sc = std::max(kIconSz() / static_cast<double>(iw), kIconSz() / static_cast<double>(ih));

        cairo_save(cr);
        draw_rounded_rect(cr, iconX, iconY, kIconSz(), kIconSz(), kIconRad());
        cairo_clip(cr);
        cairo_translate(cr, iconX + (kIconSz() - iw * sc) * 0.5, iconY + (kIconSz() - ih * sc) * 0.5);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, img_surf, 0, 0);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
        cairo_paint(cr);
        cairo_restore(cr);
        cairo_surface_destroy(img_surf);
        painted_icon = true;
      }
    }
    if (!painted_icon) {
      draw_rounded_rect(cr, iconX, iconY, kIconSz(), kIconSz(), kIconRad());
      cairo_set_source_rgba(cr, chrome.outlineR, chrome.outlineG, chrome.outlineB, 0.35);
      cairo_fill(cr);
    }

    // Header row: close button
    const double textX = iconX + kIconSz() + kIconTextGap();
    const double closeX = cx + cw - kPad() - kCloseBtnSz();
    const double closeY = cy + kPad() + 2.0 * us;
    const double headerRightEdge = closeX - kHeaderGap();
    const double pbarY = cy + ch - kProgressH - 8.0 * us;

    // Close glyph (Material Symbols "close")
    {
      auto* layout = pango_cairo_create_layout(cr);
      auto* desc = pango_font_description_from_string("Material Symbols Rounded");
      pango_font_description_set_size(desc, 14 * PANGO_SCALE * us);
      pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
      pango_layout_set_font_description(layout, desc);
      {
        PangoAttribute* fea = pango_attr_font_features_new("liga");
        if (fea) {
          fea->start_index = 0;
          fea->end_index = G_MAXUINT;
          PangoAttrList* attrs = pango_attr_list_new();
          pango_attr_list_insert(attrs, fea);
          pango_layout_set_attributes(layout, attrs);
          pango_attr_list_unref(attrs);
        }
      }
      pango_layout_set_text(layout, "close", -1);
      int gw = 0, gh = 0;
      pango_layout_get_pixel_size(layout, &gw, &gh);
      cairo_set_source_rgba(cr, chrome.textR, chrome.textG, chrome.textB, 0.55);
      cairo_move_to(cr, closeX + (kCloseBtnSz() - gw) * 0.5, closeY + (kCloseBtnSz() - gh) * 0.5);
      pango_cairo_show_layout(cr, layout);
      pango_font_description_free(desc);
      g_object_unref(layout);
    }

    // Pre-measure body to compute available vertical space for title
    double bodyDescent = 0;
    double bodyH = 0;
    {
      auto* tmp = pango_cairo_create_layout(cr);
      auto* dsc = pango_font_description_from_string("Sans");
      pango_font_description_set_size(dsc, 13 * PANGO_SCALE * us);
      pango_font_description_set_weight(dsc, PANGO_WEIGHT_BOLD);
      pango_layout_set_font_description(tmp, dsc);
      pango_layout_set_text(tmp, n->body.c_str(), static_cast<int>(n->body.size()));
      pango_layout_set_width(tmp, static_cast<int>((headerRightEdge - textX) * PANGO_SCALE));
      pango_layout_set_wrap(tmp, PANGO_WRAP_WORD_CHAR);
      pango_layout_set_height(tmp, -2);
      pango_layout_set_ellipsize(tmp, PANGO_ELLIPSIZE_END);
      pango_layout_set_line_spacing(tmp, 0.8);
      {
        PangoAttrList* attrs = pango_attr_list_new();
        PangoAttribute* hyphens = pango_attr_insert_hyphens_new(FALSE);
        pango_attr_list_insert(attrs, hyphens);
        pango_layout_set_attributes(tmp, attrs);
        pango_attr_list_unref(attrs);
      }
      PangoRectangle logical;
      pango_layout_get_extents(tmp, nullptr, &logical);
      bodyDescent = (logical.y + logical.height) / static_cast<double>(PANGO_SCALE);
      bodyH = logical.height / static_cast<double>(PANGO_SCALE);
      pango_font_description_free(dsc);
      g_object_unref(tmp);
    }

    const double bodyTop = (pbarY - 18.0 * us) - bodyH;
    const double availableTitleH = bodyTop - 6.0 * us - iconY;

    // Title (Sans Bold, auto-scaled to fill space until 6px above body).
    int summaryH = 0;
    {
      auto* layout = pango_cairo_create_layout(cr);
      auto* desc = pango_font_description_from_string("Sans");
      pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
      pango_layout_set_font_description(layout, desc);
      pango_layout_set_text(layout, n->summary.c_str(), static_cast<int>(n->summary.size()));
      pango_layout_set_width(layout, static_cast<int>((headerRightEdge - textX) * PANGO_SCALE));
      pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
      pango_layout_set_height(layout, -2);
      pango_layout_set_line_spacing(layout, 0.8);

      {
        PangoAttrList* attrs = pango_attr_list_new();
        PangoAttribute* hyphens = pango_attr_insert_hyphens_new(FALSE);
        pango_attr_list_insert(attrs, hyphens);
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
      }

      const int totalLen = static_cast<int>(n->summary.size());
      double pt = 18.0 * us;
      for (int i = 0; i < 30; ++i) {
        pango_font_description_set_size(desc, static_cast<int>(pt * PANGO_SCALE + 0.5));
        pango_layout_set_font_description(layout, desc);
        PangoRectangle logical;
        pango_layout_get_extents(layout, nullptr, &logical);
        const double h = logical.height / static_cast<double>(PANGO_SCALE);

        int visibleBytes = 0;
        const int nL = pango_layout_get_line_count(layout);
        for (int j = 0; j < nL; ++j) {
          auto* line = pango_layout_get_line(layout, j);
          visibleBytes = std::max(visibleBytes, static_cast<int>(line->start_index + line->length));
        }

        if ((h <= availableTitleH && visibleBytes >= totalLen) || pt <= 6.0) break;
        pt -= 0.5;
      }
      pt *= 0.98;
      pango_font_description_set_size(desc, static_cast<int>(pt * PANGO_SCALE + 0.5));
      pango_layout_set_font_description(layout, desc);

      PangoRectangle logical;
      pango_layout_get_extents(layout, nullptr, &logical);
      const double titleAscent = (-logical.y) / static_cast<double>(PANGO_SCALE);

      int sw = 0;
      pango_layout_get_pixel_size(layout, &sw, &summaryH);

      cairo_set_source_rgba(cr, chrome.textR, chrome.textG, chrome.textB, 0.92);
      cairo_move_to(cr, textX, iconY + titleAscent);
      pango_cairo_show_layout(cr, layout);

      pango_font_description_free(desc);
      g_object_unref(layout);
    }

    // Subtitle (Sans 11pt, max 2 lines, slightly looser leading).
    {
      auto* layout = pango_cairo_create_layout(cr);
      auto* desc = pango_font_description_from_string("Sans");
      pango_font_description_set_size(desc, 13 * PANGO_SCALE * us);
      pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
      pango_layout_set_font_description(layout, desc);
      pango_layout_set_text(layout, n->body.c_str(), static_cast<int>(n->body.size()));
      pango_layout_set_width(layout, static_cast<int>((headerRightEdge - textX) * PANGO_SCALE));
      pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
      pango_layout_set_height(layout, -2);
      pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
      pango_layout_set_line_spacing(layout, 0.8);
      {
        PangoAttrList* attrs = pango_attr_list_new();
        PangoAttribute* hyphens = pango_attr_insert_hyphens_new(FALSE);
        pango_attr_list_insert(attrs, hyphens);
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
      }

      cairo_set_source_rgba(cr, chrome.textR, chrome.textG, chrome.textB, 0.92);
      cairo_move_to(cr, textX, (pbarY - 12.0 * us) - bodyDescent);
      pango_cairo_show_layout(cr, layout);

      pango_font_description_free(desc);
      g_object_unref(layout);
    }

    // Progress bar background (inset from card edges by kPad)
    const double pbarX = cx + kPad();
    const double pbarW = cw - kPad() * 2.0;
    cairo_set_source_rgba(cr, chrome.drawerDimR, chrome.drawerDimG, chrome.drawerDimB, 0.50);
    cairo_rectangle(cr, pbarX, pbarY, pbarW, kProgressH);
    cairo_fill(cr);

    // Progress bar fill
    if (n->expiryTime) {
      const auto total = std::chrono::duration_cast<std::chrono::milliseconds>(
          *n->expiryTime - n->receivedTime).count();
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          *n->expiryTime - Clock::now()).count();
      const double fraction = total > 0 ? std::clamp(static_cast<double>(remaining) / static_cast<double>(total), 0.0, 1.0) : 0.0;
      cairo_set_source_rgba(cr, chrome.accentR, chrome.accentG, chrome.accentB, 0.55);
      cairo_rectangle(cr, pbarX, pbarY, pbarW * fraction, kProgressH);
      cairo_fill(cr);
    }
  }

  cairo_restore(cr);
  cairo_surface_flush(shmBuf_.cairo_surface());
  wl_surface_attach(surface_, shmBuf_.wl(), 0, 0);
  wl_surface_damage_buffer(surface_, 0, 0, w, h);
  shmBuf_.mark_busy();

  // Request frame callback for smooth real-time progress bar animation.
  for (const auto& nn : mgr_.all()) {
    if (nn.expiryTime && !frameCallback_) {
      frameCallback_ = wl_surface_frame(surface_);
      wl_callback_add_listener(frameCallback_, &kFrameListener, this);
      break;
    }
  }

  wl_surface_commit(surface_);
}

int NotificationToastHost::hit_test(double sx, double sy) const {
   
  for (size_t i = 0; i < cards_.size(); ++i) {
    const auto& c = cards_[i];
    if (sx >= c.x && sx < c.x + c.w && sy >= c.y && sy < c.y + c.h)
      return static_cast<int>(i);
  }
  return -1;
}

void NotificationToastHost::pointer_motion(double sx, double sy) {
   
  pointerX_ = sx;
  pointerY_ = sy;
}

void NotificationToastHost::pointer_button_press() {
   
  pointer_button_press(pointerX_, pointerY_);
}

void NotificationToastHost::pointer_button_press(double sx, double sy) {
   
  const int hit = hit_test(sx, sy);
  if (hit >= 0 && static_cast<size_t>(hit) < cards_.size()) {
    const std::uint32_t id = cards_[static_cast<size_t>(hit)].id;
    eh::shell_log::notifications("dismiss notification id=", id, " at (", sx, ", ", sy, ") hit=", hit);
    (void)mgr_.close(id, CloseReason::Dismissed);
  } else {
    eh::shell_log::notif_verbose("pointer_button_press: no card hit at (", sx, ", ", sy, ")");
  }
}

}
