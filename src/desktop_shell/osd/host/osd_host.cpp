#include "desktop_shell/osd/host/osd_host.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/core/protocols.hpp"
#include "wl/buffer/shm_buffer.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace eh::shell::osd {

bool osd_env_disabled() noexcept {
   
  const char* e = std::getenv("EH_OSD");
  return e && (e[0] == '0' || std::strcmp(e, "off") == 0);
}

namespace {

constexpr float kShowMs = 220.f;
constexpr float kHideDelayMs = 880.f;
constexpr float kHideMs = 200.f;

bool position_is_bottom(std::string_view p) { return p.size() >= 6 && p.compare(0, 6, "bottom") == 0; }

float slide_dir(std::string_view p) { return position_is_bottom(p) ? -1.f : 1.f; }

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
   
  const double x0 = x, y0 = y, x1 = x + w, y1 = y + h;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0);
  cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI_2);
  cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

}

struct OsdOutputInst;

struct OsdHost::Impl {
  DockApp* dock = nullptr;
  std::vector<std::unique_ptr<OsdOutputInst>> inst;
  OsdContent content{};
  std::string position = "top_right";
  float ui_scale = 1.f;
  bool initialized = false;

  void destroy_surfaces();
  void ensure_instances();
  void paint(OsdOutputInst& in);
  void tick(OsdOutputInst& in, uint32_t compositor_ms);
  void schedule_frame(OsdOutputInst& in);
  void begin_show_and_hide_cycle(OsdOutputInst& in);
  void arm_hide_delayed(OsdOutputInst& in);
  void maybe_destroy_all_idle();
};

struct OsdOutputInst {
  OsdHost::Impl* impl = nullptr;
  wl_output* wl_out = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  eh::wayland::ShmBuffer shm{};
  int cfg_w = 0;
  int cfg_h = 0;
  bool configured = false;
  eh::shell::AnimationManager anim{};
  wl_callback* frame_cb = nullptr;
  float opacity = 0.f;
  float slide_y = 0.f;
  bool visible = false;
  bool show_pending = false;
  eh::shell::AnimationManager::Id show_id = 0;
  eh::shell::AnimationManager::Id hide_delay_id = 0;
  eh::shell::AnimationManager::Id hide_id = 0;
  int card_w = 0;
  int card_h = 0;
  bool want_repaint = false;
};

static void osd_shm_release(void* user) {
   
  auto* in = static_cast<OsdOutputInst*>(user);
  if (!in || !in->impl || !in->impl->dock) return;
  if (!in->want_repaint) return;
  in->want_repaint = false;
  in->impl->paint(*in);
  if (in->impl->dock->display) wl_display_flush(in->impl->dock->display);
}

static void osd_layer_configure(void* data, zwlr_layer_surface_v1* layer_surface, uint32_t serial, uint32_t width,
                                uint32_t height) {
   
  auto* in = static_cast<OsdOutputInst*>(data);
  zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
  if (width > 0) in->cfg_w = static_cast<int>(width);
  if (height > 0) in->cfg_h = static_cast<int>(height);
  in->configured = in->cfg_w > 0 && in->cfg_h > 0;
  if (in->impl) {
    in->impl->paint(*in);
    in->impl->schedule_frame(*in);
  }
}

static void osd_layer_closed(void* data, zwlr_layer_surface_v1*) {
   
  auto* in = static_cast<OsdOutputInst*>(data);
  if (!in) return;
  in->configured = false;
  in->visible = false;
  if (in->layer) {
    zwlr_layer_surface_v1_destroy(in->layer);
    in->layer = nullptr;
  }
  if (in->surface) {
    wl_surface_destroy(in->surface);
    in->surface = nullptr;
  }
}

static const zwlr_layer_surface_v1_listener k_layer_listener = {
    .configure = osd_layer_configure,
    .closed = osd_layer_closed,
};

static void osd_frame_done(void* data, wl_callback* cb, uint32_t compositor_ms) {
   
  auto* in = static_cast<OsdOutputInst*>(data);
  wl_callback_destroy(cb);
  in->frame_cb = nullptr;
  if (in->impl) in->impl->tick(*in, compositor_ms);
}

static const wl_callback_listener k_frame_listener = {
    .done = osd_frame_done,
};

void OsdHost::Impl::destroy_surfaces() {
   
  for (auto& up : inst) {
    if (!up) continue;
    up->anim.cancel_all();
    up->show_id = up->hide_delay_id = up->hide_id = 0;
    if (up->frame_cb) {
      wl_callback_destroy(up->frame_cb);
      up->frame_cb = nullptr;
    }
    up->shm.set_release_hook(nullptr, nullptr);
    // Destroy the layer surface before the wl_surface: wspace may
    // automatically destroy the layer surface when the associated wl_surface
    // is destroyed, causing a protocol error on the follow-up destroy call.
    if (up->layer) {
      zwlr_layer_surface_v1_destroy(up->layer);
      up->layer = nullptr;
    }
    if (up->surface) {
      wl_surface_destroy(up->surface);
      up->surface = nullptr;
    }
    up->shm.destroy();
    up->configured = false;
    up->visible = false;
  }
  inst.clear();
}

void OsdHost::Impl::ensure_instances() {
   
  if (!dock || !dock->compositor || !dock->layerShell || !dock->shm) return;

  size_t count = 0;
  for (const auto& slot : dock->outputSlots)
    if (slot && slot->output) ++count;

  if (!inst.empty() && inst.size() != count) destroy_surfaces();
  if (!inst.empty()) return;

  const float s = ui_scale;
  const int card_w_px = static_cast<int>(std::ceil(248.0 * static_cast<double>(s)));
  const int card_h_px = static_cast<int>(std::ceil(54.0 * static_cast<double>(s)));
  const int margin = static_cast<int>(std::ceil(10.0 * static_cast<double>(s)));
  const int surf_w = card_w_px + margin * 2;
  const int surf_h = card_h_px + margin * 2;

  const std::string& pos = position;
  uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  int mt = margin, mr = margin, mb = 0, ml = 0;
  if (pos == "top_left") {
    anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    mr = 0;
    ml = margin;
  } else if (pos == "top_center") {
    anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    mr = 0;
  } else if (pos == "bottom_left") {
    anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    mt = 0;
    mr = 0;
    mb = margin;
    ml = margin;
  } else if (pos == "bottom_center") {
    anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    mt = 0;
    mr = 0;
    mb = margin;
  } else if (pos == "bottom_right") {
    anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    mt = 0;
    mb = margin;
  }

  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace = eh::shell::kOsdNamespace;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.anchor = anchor;
  cfg.width = static_cast<std::uint32_t>(surf_w);
  cfg.height = static_cast<std::uint32_t>(surf_h);
  cfg.exclusiveZone = 0;
  cfg.marginTop = mt;
  cfg.marginRight = mr;
  cfg.marginBottom = mb;
  cfg.marginLeft = ml;
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  for (const auto& slot : dock->outputSlots) {
    if (!slot || !slot->output) continue;
    auto up = std::make_unique<OsdOutputInst>();
    OsdOutputInst* raw = up.get();
    raw->impl = this;
    raw->wl_out = slot->output;
    raw->card_w = card_w_px;
    raw->card_h = card_h_px;
    wl_surface* surf = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    if (!eh::wayland::create_layer_surface(dock->compositor, dock->layerShell, slot->output, cfg, &k_layer_listener, raw,
                                           &surf, &layer))
      continue;
    raw->surface = surf;
    raw->layer = layer;
    wl_region* region = wl_compositor_create_region(dock->compositor);
    if (region) {
      wl_surface_set_input_region(surf, region);
      wl_region_destroy(region);
    }
    inst.push_back(std::move(up));
    wl_surface_commit(surf);
  }
  for (auto& up : inst) {
    if (up && up->layer) {
      auto* dpy = wl_proxy_get_display(reinterpret_cast<wl_proxy*>(up->layer));
      if (dpy) wl_display_roundtrip(dpy);
      break;
    }
  }
}

void OsdHost::Impl::schedule_frame(OsdOutputInst& in) {
   
  if (!in.surface || in.frame_cb) return;
  in.frame_cb = wl_surface_frame(in.surface);
  wl_callback_add_listener(in.frame_cb, &k_frame_listener, &in);
  wl_surface_commit(in.surface);
}

void OsdHost::Impl::tick(OsdOutputInst& in, uint32_t /*compositor_ms*/) {
   
  in.anim.tick();
  paint(in);
  if (in.anim.has_active()) schedule_frame(in);
}

void OsdHost::Impl::maybe_destroy_all_idle() {
   
  for (const auto& up : inst) {
    if (!up) continue;
    if (up->visible || up->show_pending || up->anim.has_active()) return;
  }
  destroy_surfaces();
}

void OsdHost::Impl::arm_hide_delayed(OsdOutputInst& in) {
   
  const float slide_max = 12.f * ui_scale;
  const float dir = slide_dir(position);
  in.hide_delay_id = in.anim.animate(
      1.f, 1.f, kHideDelayMs, eh::shell::Easing::Linear, [](float) {},
      [this, &in, slide_max, dir]() {
        in.hide_delay_id = 0;
        in.hide_id = in.anim.animate(
            1.f, 0.f, kHideMs, eh::shell::Easing::EaseInOutQuad,
            [&](float v) {
              in.opacity = v;
              in.slide_y = dir * slide_max * (1.f - v);
            },
            [this, &in]() {
              in.hide_id = 0;
              in.visible = false;
              in.opacity = 0.f;
              maybe_destroy_all_idle();
            });
      });
}

void OsdHost::Impl::begin_show_and_hide_cycle(OsdOutputInst& in) {
   
  const float slide_max = 12.f * ui_scale;
  const float dir = slide_dir(position);

  in.anim.cancel(in.show_id);
  in.anim.cancel(in.hide_delay_id);
  in.anim.cancel(in.hide_id);
  in.show_id = in.hide_delay_id = in.hide_id = 0;

  if (in.visible && in.opacity > 0.95f) {
    arm_hide_delayed(in);
    return;
  }

  in.opacity = 0.f;
  in.slide_y = dir * slide_max;
  in.show_id = in.anim.animate(
      0.f, 1.f, kShowMs, eh::shell::Easing::EaseOutCubic,
      [&](float v) {
        in.opacity = v;
        in.slide_y = dir * slide_max * (1.f - v);
      },
      [this, &in]() {
        in.show_id = 0;
        in.visible = true;
        in.opacity = 1.f;
        in.slide_y = 0.f;
        arm_hide_delayed(in);
      });
}

void OsdHost::Impl::paint(OsdOutputInst& in) {
   
  if (!dock || !in.surface || !in.configured || in.cfg_w <= 0 || in.cfg_h <= 0) return;
  if (in.shm.busy()) {
    in.want_repaint = true;
    return;
  }

  if (in.show_pending) {
    in.show_pending = false;
    begin_show_and_hide_cycle(in);
  }

  const double cw = static_cast<double>(in.card_w);
  const double ch = static_cast<double>(in.card_h);
  const double inner_pad = 10.0 * static_cast<double>(ui_scale);
  const bool bottom = position_is_bottom(position);
  const double card_x = (static_cast<double>(in.cfg_w) - cw) * 0.5;
  const double card_y_eff =
      bottom ? (static_cast<double>(in.cfg_h) - inner_pad - ch + static_cast<double>(in.slide_y))
             : (inner_pad + static_cast<double>(in.slide_y));

  if (!in.shm.ensure(dock->shm, eh::shell::kOsdNamespace, in.cfg_w, in.cfg_h)) return;
  in.shm.set_release_hook(osd_shm_release, &in);

  cairo_t* cr = in.shm.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  const double alpha = std::clamp(static_cast<double>(in.opacity), 0.0, 1.0);
  if (alpha <= 1e-4) {
    cairo_restore(cr);
    cairo_surface_flush(in.shm.cairo_surface());
    wl_surface_attach(in.surface, in.shm.wl(), 0, 0);
    wl_surface_damage_buffer(in.surface, 0, 0, in.cfg_w, in.cfg_h);
    in.shm.mark_busy();
    wl_surface_commit(in.surface);
    return;
  }

  rounded_rect(cr, card_x, card_y_eff, cw, ch, ch * 0.5);
  cairo_set_source_rgba(cr, 0.14, 0.14, 0.16, 0.92 * alpha);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.9 * alpha, 0.9 * alpha, 0.92 * alpha, 0.12 * alpha);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const double glyph_px = 22.0 * static_cast<double>(ui_scale);
  const double gx = card_x + inner_pad + glyph_px * 0.5;
  const double gy = card_y_eff + ch * 0.5;
  eh::shell::draw_material_glyph(cr, gx, gy, glyph_px, content.icon_ligature.c_str(), 0.88 * alpha, 0.88 * alpha, 0.92 * alpha,
                           alpha);

  const double prog_left = gx + glyph_px * 0.5 + 10.0 * static_cast<double>(ui_scale);
  const double prog_right = card_x + cw - 12.0 * static_cast<double>(ui_scale) - 52.0 * static_cast<double>(ui_scale);
  const double prog_w = std::max(8.0, prog_right - prog_left);
  const double prog_h = std::max(4.0, 5.0 * static_cast<double>(ui_scale));
  const double prog_y = card_y_eff + (ch - prog_h) * 0.5;
  const float p = std::clamp(content.progress, 0.f, 1.f);
  rounded_rect(cr, prog_left, prog_y, prog_w, prog_h, prog_h * 0.5);
  cairo_set_source_rgba(cr, 0.22, 0.22, 0.26, 0.85 * alpha);
  cairo_fill(cr);
  if (p > 1e-4f) {
    rounded_rect(cr, prog_left, prog_y, static_cast<double>(p) * prog_w, prog_h, prog_h * 0.5);
    cairo_set_source_rgba(cr, 0.55 * alpha, 0.72 * alpha, 0.98 * alpha, 0.95 * alpha);
    cairo_fill(cr);
  }

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 13.0 * static_cast<double>(ui_scale));
  cairo_set_source_rgba(cr, 0.93 * alpha, 0.93 * alpha, 0.96 * alpha, alpha);
  cairo_move_to(cr, prog_left + prog_w + 8.0 * static_cast<double>(ui_scale), card_y_eff + ch * 0.5 + 5.0);
  cairo_show_text(cr, content.value_text.c_str());

  cairo_restore(cr);
  cairo_surface_flush(in.shm.cairo_surface());
  wl_surface_attach(in.surface, in.shm.wl(), 0, 0);
  wl_surface_damage_buffer(in.surface, 0, 0, in.cfg_w, in.cfg_h);
  in.shm.mark_busy();
  wl_surface_commit(in.surface);

  if (in.anim.has_active()) schedule_frame(in);
}

OsdHost::OsdHost() : impl_(std::make_unique<Impl>()) {
   
}
OsdHost::~OsdHost() = default;

void OsdHost::init(DockApp& dock) {
   
  impl_->dock = &dock;
  impl_->initialized = true;
  if (const char* p = std::getenv("EH_OSD_POSITION"); p && p[0]) impl_->position = p;
  impl_->ui_scale = static_cast<float>(std::clamp(dock_ui_scale(dock.settings), 0.5, 2.0));
}

void OsdHost::shutdown() {
   
  if (impl_) impl_->destroy_surfaces();
  if (impl_) {
    impl_->dock = nullptr;
    impl_->initialized = false;
  }
}

void OsdHost::show(const OsdContent& content) {
   
  if (!impl_ || !impl_->dock || !impl_->initialized) return;
  impl_->content = content;
  impl_->ensure_instances();
  for (auto& up : impl_->inst) {
    if (!up) continue;
    up->show_pending = true;
    impl_->paint(*up);
  }
}

}
