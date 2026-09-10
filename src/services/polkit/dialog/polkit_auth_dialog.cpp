#if defined(EH_HAVE_POLKIT_AGENT)

#include "services/polkit/dialog/polkit_auth_dialog.hpp"
#include "services/polkit/service/polkit_auth_service.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace eh::polkit {

struct PolkitAuthDialog::Impl {
  wl_display* display = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  zwlr_layer_shell_v1* layer_shell = nullptr;
  wp_viewporter* viewporter = nullptr;
  wp_fractional_scale_manager_v1* fractional_scale_mgr = nullptr;

  wl_surface* surf = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  eh::wayland::ShmBuffer buf{};
  eh::wayland::SurfaceExtensions surf_ext{};
  int cfg_w = 0;
  int cfg_h = 0;
  bool configured = false;
  bool visible = false;

  static constexpr double kCardW = 460.0;
  static constexpr double kCardH = 240.0;

  double ptr_x = 0;
  double ptr_y = 0;


  std::string password_field;

  double hit_close[4]{};
  double hit_cancel[4]{};
  double hit_auth[4]{};

  int hover_button = 0; // 0=none, 1=cancel, 2=auth
  int press_button = 0; // 0=none, 1=cancel, 2=auth

  Impl(wl_display* dpy, wl_compositor* comp, wl_shm* shm_,
       zwlr_layer_shell_v1* ls, wp_viewporter* vp,
       wp_fractional_scale_manager_v1* fsm)
    : display(dpy), compositor(comp), shm(shm_),
      layer_shell(ls), viewporter(vp), fractional_scale_mgr(fsm) {}

  static void rr(cairo_t* cr, double rx, double ry, double rw, double rh, double rad) {
     
    constexpr double kPiH = 1.57079632679489661923;
    cairo_new_path(cr);
    const double r = std::min({rad, rw * 0.5, rh * 0.5});
    const double x0 = rx, y0 = ry, x1 = rx + rw, y1 = ry + rh;
    cairo_arc(cr, x1 - r, y0 + r, r, -kPiH, 0);
    cairo_arc(cr, x1 - r, y1 - r, r, 0, kPiH);
    cairo_arc(cr, x0 + r, y1 - r, r, kPiH, 2 * kPiH);
    cairo_arc(cr, x0 + r, y0 + r, r, 2 * kPiH, 3 * kPiH);
    cairo_close_path(cr);
  }

  static bool point_in_rect(double x, double y, const double r[4]) {
    return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
  }

  void attach_surface_extensions() {
     
    if (!surf) return;
    if (viewporter && fractional_scale_mgr && !surf_ext.viewport) {
      surf_ext.viewport = wp_viewporter_get_viewport(viewporter, surf);
      wl_surface_set_buffer_scale(surf, 1);
    }
    if (!fractional_scale_mgr || surf_ext.fractionalScale) return;
    surf_ext.fractionalScale =
        wp_fractional_scale_manager_v1_get_fractional_scale(fractional_scale_mgr, surf);
  }

  void paint() {
     
    if (!surf || !configured || cfg_w <= 0 || cfg_h <= 0) return;
    if (!shm) return;

    auto snap = PolkitAuthService::instance().snapshot();

    const int logW = cfg_w;
    const int logH = cfg_h;
    const double bufScale = surf_ext.preferred_scale();
    const int bufW = std::max(1, static_cast<int>(std::ceil(static_cast<double>(logW) * bufScale)));
    const int bufH = std::max(1, static_cast<int>(std::ceil(static_cast<double>(logH) * bufScale)));

    if (!buf.ensure(shm, eh::shell::kPolkitNamespace, bufW, bufH)) return;

    cairo_t* cr = buf.cairo();
    cairo_save(cr);
    if (bufScale != 1.0) cairo_scale(cr, bufScale, bufScale);

    const eh::config::ChromePaintColors mc =
        eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
    const double W = static_cast<double>(logW);
    const double H = static_cast<double>(logH);
    const double cardW = Impl::kCardW;
    const double cardH = Impl::kCardH;
    const double cx = (W - cardW) * 0.5;
    const double cy = (H - cardH) * 0.5;

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    rr(cr, cx + 2, cy + 3, cardW, cardH, 20.0);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
    cairo_fill(cr);

    eh::shell::shared::paint_glass_card(cr, cx, cy, cardW, cardH, 18.0, mc, 1.0);

    const double pad = 20.0;
    eh::shell::draw_material_glyph(cr, cx + pad + 11.0, cy + pad + 14.0, 22.0, "lock",
                                   mc.accentR, mc.accentG, mc.accentB, 0.95);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 17.0);
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.98, 0.96);
    cairo_move_to(cr, cx + pad + 40.0, cy + pad + 22.0);
    cairo_show_text(cr, "Authentication Required");

    const double closeSz = 28.0;
    hit_close[0] = cx + cardW - pad - closeSz;
    hit_close[1] = cy + pad - 4.0;
    hit_close[2] = closeSz;
    hit_close[3] = closeSz;
    const int cHov = hover_button == 3 ? 1 : 0;
    const int cPrs = press_button == 3 ? 1 : 0;
    rr(cr, hit_close[0], hit_close[1], hit_close[2], hit_close[3], 8.0);
    cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.5);
    cairo_fill(cr);
    if (cPrs) {
      cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
      rr(cr, hit_close[0], hit_close[1], hit_close[2], hit_close[3], 8.0);
      cairo_fill(cr);
    } else if (cHov) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.2);
      rr(cr, hit_close[0], hit_close[1], hit_close[2], hit_close[3], 8.0);
      cairo_fill(cr);
    }
    eh::shell::draw_material_glyph(cr, hit_close[0] + closeSz * 0.5, hit_close[1] + closeSz * 0.5,
                                   18.0, "close", 0.88, 0.92, 0.95, 1.0);

    auto draw_clipped = [&](cairo_t* cr, double x, double y, double maxW,
                            const char* text, double fontSize) {
      cairo_save(cr);
      cairo_rectangle(cr, x, y - fontSize * 0.8, maxW, fontSize * 1.4);
      cairo_clip(cr);
      cairo_set_font_size(cr, fontSize);
      cairo_text_extents_t te;
      cairo_text_extents(cr, text, &te);
      std::string show = text;
      if (te.x_advance > maxW) {
        while (!show.empty()) {
          std::string trial = show.substr(0, show.size() - 1) + "...";
          cairo_text_extents(cr, trial.c_str(), &te);
          if (te.x_advance <= maxW) { show = trial; break; }
          show.pop_back();
        }
      }
      cairo_move_to(cr, x, y);
      cairo_show_text(cr, show.c_str());
      cairo_restore(cr);
    };

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_source_rgba(cr, mc.outlineR + 0.2, mc.outlineG + 0.15, mc.outlineB + 0.15, 0.88);
    draw_clipped(cr, cx + pad, cy + pad + 48.0, cardW - 2.0 * pad, snap.message.c_str(), 13.0);

    if (!snap.error.empty()) {
      cairo_set_source_rgba(cr, 0.93, 0.38, 0.42, 0.95);
      draw_clipped(cr, cx + pad, cy + pad + 70.0, cardW - 2.0 * pad, snap.error.c_str(), 11.5);
    }

    const double fieldY = cy + pad + 92.0;
    const double fieldH = 36.0;
    const double fieldW = cardW - 2 * pad;
    rr(cr, cx + pad, fieldY, fieldW, fieldH, 10.0);
    cairo_set_source_rgba(cr, mc.dockFillR * 0.35, mc.dockFillG * 0.35, mc.dockFillB * 0.38, 0.75);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.55);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);

    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, 0.92, 0.95, 0.97, 0.96);
    std::string shown = password_field;
    if (!snap.echo_on && !shown.empty()) {
      shown.assign(shown.size(), '*');
    }
    if (shown.size() > 80) shown.resize(77), shown += "...";
    cairo_move_to(cr, cx + pad + 8.0, fieldY + 24.0);
    cairo_show_text(cr, shown.empty() ? " " : shown.c_str());

    if (snap.state == AuthState::Failed) {
      cairo_set_source_rgba(cr, 0.93, 0.38, 0.42, 0.95);
      cairo_set_font_size(cr, 11.0);
      cairo_move_to(cr, cx + pad, fieldY + fieldH + 14.0);
      cairo_show_text(cr, "Authentication failed, please try again");
    }

    const double btnY = cy + cardH - pad - 38.0;
    const double btnH = 36.0;
    const double gap = 10.0;
    const double cancelW = 100.0;
    const double authW = 130.0;
    const double btnRight = cx + cardW - pad;
    hit_auth[0] = btnRight - authW;
    hit_auth[1] = btnY;
    hit_auth[2] = authW;
    hit_auth[3] = btnH;
    hit_cancel[0] = hit_auth[0] - gap - cancelW;
    hit_cancel[1] = btnY;
    hit_cancel[2] = cancelW;
    hit_cancel[3] = btnH;

    const int isHov = hover_button;
    const int isPrs = press_button;

    // Cancel button
    {
      const bool hover = isHov == 1;
      const bool press = isPrs == 1;
      rr(cr, hit_cancel[0], hit_cancel[1], hit_cancel[2], hit_cancel[3], btnH * 0.5);
      cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.75);
      cairo_fill(cr);
      if (press) {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.25);
        rr(cr, hit_cancel[0], hit_cancel[1], hit_cancel[2], hit_cancel[3], btnH * 0.5);
        cairo_fill(cr);
      } else if (hover) {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.15);
        rr(cr, hit_cancel[0], hit_cancel[1], hit_cancel[2], hit_cancel[3], btnH * 0.5);
        cairo_fill(cr);
      }
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 13.0);
      cairo_set_source_rgba(cr, mc.outlineR + 0.25, mc.outlineG + 0.25, mc.outlineB + 0.25, 0.9);
      cairo_move_to(cr, hit_cancel[0] + cancelW * 0.5 - 25.0, hit_cancel[1] + btnH * 0.5 + 5.0);
      cairo_show_text(cr, "Cancel");
    }

    // Confirm button
    {
      const bool hasPw = !password_field.empty();
      const bool hover = isHov == 2;
      const bool press = isPrs == 2;
      rr(cr, hit_auth[0], hit_auth[1], hit_auth[2], hit_auth[3], btnH * 0.5);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, press ? 0.55 : 0.85);
      cairo_fill(cr);
      if (hover && !press) {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
        rr(cr, hit_auth[0], hit_auth[1], hit_auth[2], hit_auth[3], btnH * 0.5);
        cairo_fill(cr);
      }
      double textA = hasPw ? 0.95 : 0.45;
      if (press) textA *= 0.6;
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 13.0);
      cairo_set_source_rgba(cr, 0.08, 0.1, 0.12, textA);
      cairo_move_to(cr, hit_auth[0] + authW * 0.5 - 22.0, hit_auth[1] + btnH * 0.5 + 5.0);
      cairo_show_text(cr, "Confirm");
    }

    cairo_restore(cr);
    wl_surface_attach(surf, buf.wl(), 0, 0);
    if (surf_ext.viewport) {
      wp_viewport_set_source(surf_ext.viewport, wl_fixed_from_double(0), wl_fixed_from_double(0),
                             wl_fixed_from_int(bufW), wl_fixed_from_int(bufH));
    }
    wl_surface_set_buffer_scale(surf, 1);
    wl_surface_damage_buffer(surf, 0, 0, bufW, bufH);
    if (layer) zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    wl_surface_commit(surf);
    if (display) wl_display_flush(display);
  }
};

// Per-instance layer surface listener trampoline
static void layer_configure_trampoline(void* data, zwlr_layer_surface_v1* layer,
                                       uint32_t serial, uint32_t w, uint32_t h) {
   
  auto* dlg = static_cast<PolkitAuthDialog*>(data);
  if (!dlg) return;
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  if (w > 0) dlg->impl()->cfg_w = static_cast<int>(w);
  if (h > 0) dlg->impl()->cfg_h = static_cast<int>(h);
  dlg->impl()->configured = true;
  if (dlg->impl()->visible) dlg->impl()->paint();
}

static void layer_closed_trampoline(void* data, zwlr_layer_surface_v1*) {
  auto* dlg = static_cast<PolkitAuthDialog*>(data);
  if (!dlg) return;
  auto* i = dlg->impl();
  i->surf = nullptr;
  i->layer = nullptr;
  i->configured = false;
  i->visible = false;
}

static const zwlr_layer_surface_v1_listener kLayerListener = {
    .configure = layer_configure_trampoline,
    .closed = layer_closed_trampoline,
};

PolkitAuthDialog::PolkitAuthDialog(wl_display* display, wl_compositor* compositor, wl_shm* shm,
                                   zwlr_layer_shell_v1* layer_shell, wp_viewporter* viewporter,
                                   wp_fractional_scale_manager_v1* fractional_scale_mgr)
  : impl_(std::make_unique<Impl>(display, compositor, shm, layer_shell, viewporter,
                                 fractional_scale_mgr)) {
}

PolkitAuthDialog::~PolkitAuthDialog() {
   
  hide();
  impl_->surf_ext.destroy();
  if (impl_->layer) {
    zwlr_layer_surface_v1_destroy(impl_->layer);
    impl_->layer = nullptr;
  }
  if (impl_->surf) {
    wl_surface_destroy(impl_->surf);
    impl_->surf = nullptr;
  }
}

void PolkitAuthDialog::show(wl_output* primary_output) {
   
  impl_->visible = true;
  if (!impl_->surf) {
    if (!impl_->compositor || !impl_->layer_shell) return;
    eh::wayland::LayerSurfaceConfig cfg{};
    cfg.nameSpace = eh::shell::kPolkitNamespace;
    cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    cfg.width = static_cast<std::uint32_t>(Impl::kCardW);
    cfg.height = static_cast<std::uint32_t>(Impl::kCardH);
    cfg.exclusiveZone = 0;
    cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
    wl_surface* surf = nullptr;
    zwlr_layer_surface_v1* layer = nullptr;
    if (!eh::wayland::create_layer_surface(impl_->compositor, impl_->layer_shell, primary_output,
                                           cfg, &kLayerListener, this, &surf, &layer)) {
      std::cerr << "[polkit] create_layer_surface failed\n";
      return;
    }
    impl_->surf = surf;
    impl_->layer = layer;
    impl_->attach_surface_extensions();
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    wl_surface_commit(impl_->surf);
    if (impl_->display) (void)wl_display_roundtrip(impl_->display);
  } else {
    // Surface already exists from a previous show/hide cycle; re-map it.
    // The compositor won't send a new configure for an existing surface,
    // so restore configured=true and paint immediately.
    impl_->configured = true;
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        impl_->layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    impl_->paint();
  }
  impl_->password_field.clear();
}

void PolkitAuthDialog::hide() {
   
  impl_->visible = false;
  impl_->configured = false;
  impl_->password_field.clear();
  if (impl_->buf.wl()) impl_->buf.destroy();
  // Unmap the surface without destroying it so the compositor can
  // re-assign keyboard focus to the dialog on the next show().
  if (impl_->surf) {
    wl_surface_attach(impl_->surf, nullptr, 0, 0);
    wl_surface_commit(impl_->surf);
    if (impl_->display) wl_display_flush(impl_->display);
  }
}

void PolkitAuthDialog::repaint() {
  impl_->paint();
}

void PolkitAuthDialog::pointer_motion(double sx, double sy) {
   
  if (!impl_->visible) return;
  impl_->ptr_x = sx;
  impl_->ptr_y = sy;
  int old = impl_->hover_button;
  impl_->hover_button = 0;
  if (Impl::point_in_rect(sx, sy, impl_->hit_cancel)) impl_->hover_button = 1;
  else if (Impl::point_in_rect(sx, sy, impl_->hit_auth)) impl_->hover_button = 2;
  else if (Impl::point_in_rect(sx, sy, impl_->hit_close)) impl_->hover_button = 3;
  if (old != impl_->hover_button) impl_->paint();
}

void PolkitAuthDialog::pointer_button(uint32_t button, uint32_t state) {
   
  if (button != 0x110) return;
  if (!impl_->visible) return;

  auto snap = PolkitAuthService::instance().snapshot();
  if (!snap.has_request) return;

  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    int btn = 0;
    if (Impl::point_in_rect(impl_->ptr_x, impl_->ptr_y, impl_->hit_close) ||
        Impl::point_in_rect(impl_->ptr_x, impl_->ptr_y, impl_->hit_cancel))
      btn = 1;
    else if (Impl::point_in_rect(impl_->ptr_x, impl_->ptr_y, impl_->hit_auth))
      btn = 2;
    if (btn) {
      impl_->press_button = btn;
      impl_->paint();
    }
  } else if (state == WL_POINTER_BUTTON_STATE_RELEASED && impl_->press_button) {
    int btn = impl_->press_button;
    impl_->press_button = 0;
    if (btn == 1 &&
        (Impl::point_in_rect(impl_->ptr_x, impl_->ptr_y, impl_->hit_close) ||
         Impl::point_in_rect(impl_->ptr_x, impl_->ptr_y, impl_->hit_cancel))) {
      PolkitAuthService::instance().cancel_request();
    } else if (btn == 2 &&
               Impl::point_in_rect(impl_->ptr_x, impl_->ptr_y, impl_->hit_auth)) {
      PolkitAuthService::instance().submit_response(impl_->password_field);
    }
  }
}


bool PolkitAuthDialog::consume_keyboard_key(uint32_t state, uint32_t sym, const char* utf8, int utf8_len) {
   
  if (!impl_->visible) return false;

  auto snap = PolkitAuthService::instance().snapshot();
  if (!snap.has_request) return false;

  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return false;

  if (sym == XKB_KEY_Escape) {
    PolkitAuthService::instance().cancel_request();
    return true;
  }
  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    PolkitAuthService::instance().submit_response(impl_->password_field);
    return true;
  }
  if (sym == XKB_KEY_BackSpace) {
    if (!impl_->password_field.empty()) {
      impl_->password_field.pop_back();
      impl_->paint();
    }
    return true;
  }

  // xkb_state_key_get_utf8 may report a length that includes the NUL byte;
  // never append it to the password buffer.
  int len = utf8_len;
  if (len > 0 && utf8 && utf8[len - 1] == '\0') --len;
  if (len > 0 && utf8) {
    impl_->password_field.append(utf8, static_cast<size_t>(len));
    impl_->paint();
    return true;
  }
  return false;
}

wl_surface* PolkitAuthDialog::surface() const {
  return impl_->surf;
}

}

#endif
