#if defined(EH_HAVE_POLKIT_AGENT)

#include "services/polkit/dialog/polkit_auth_dialog.hpp"
#include "services/polkit/service/polkit_auth_service.hpp"
#include "services/polkit/polkit_log.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

#include <unistd.h>

namespace eh::polkit {

namespace {

// One icon cache for the auth dialog, built on first use — a file-scope
// IconCache would allocate during static init (the child-process heap pitfall
// the jemalloc pin in every standalone main exists for).
eh::icons::IconCache& dialog_icon_cache() {
  static eh::icons::IconCache cache;
  return cache;
}

// The listener hands over a human-readable identity ("user" / "Full Name
// (user)"). Present it the way the reference session agents do: "user@host",
// leaving anything that already reads as an annotation alone.
std::string identity_subtitle(const std::string& identity) {
  if (identity.empty()) return {};
  if (identity.find_first_of(" @(") != std::string::npos) return identity;
  char host[64]{};
  if (::gethostname(host, sizeof(host) - 1) != 0 || host[0] == '\0') return identity;
  return identity + "@" + host;
}

// Compact modal card: small fixed width, height sized to content (no fixed
// slots, no dead space). Type is Inter via Pango like the rest of the DE;
// surfaces come from ChromePaintColors, which resolve the matugen /
// horizon-colors wallpaper palette — the wallpaper decides the colors.
static PangoLayout* make_row_layout(cairo_t* cr, const std::string& text, float size, int weight,
                                    double maxW, bool ellipsize, int maxLines) {
  PangoLayout* lay = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(size * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(weight));
  pango_layout_set_font_description(lay, desc);
  pango_font_description_free(desc);
  pango_layout_set_text(lay, text.c_str(), -1);
  if (maxW > 0) {
    pango_layout_set_width(lay, static_cast<int>(maxW * PANGO_SCALE));
    pango_layout_set_single_paragraph_mode(lay, TRUE);
  }
  if (ellipsize) pango_layout_set_ellipsize(lay, PANGO_ELLIPSIZE_END);
  if (maxLines > 0) pango_layout_set_height(lay, -maxLines);
  return lay;
}

static double row_height(cairo_t* cr, const std::string& text, float size, int weight, double maxW,
                         bool ellipsize, int maxLines) {
  if (text.empty()) return 0.0;
  PangoLayout* lay = make_row_layout(cr, text, size, weight, maxW, ellipsize, maxLines);
  int pw = 0, ph = 0;
  pango_layout_get_pixel_size(lay, &pw, &ph);
  g_object_unref(lay);
  (void)pw;
  return static_cast<double>(ph);
}

static void draw_row(cairo_t* cr, const std::string& text, float size, int weight, double maxW,
                     bool ellipsize, int maxLines, double r, double g, double b, double a, double x,
                     double yTop) {
  if (text.empty()) return;
  PangoLayout* lay = make_row_layout(cr, text, size, weight, maxW, ellipsize, maxLines);
  cairo_save(cr);
  cairo_translate(cr, x, yTop);
  cairo_set_source_rgba(cr, r, g, b, a);
  pango_cairo_show_layout(cr, lay);
  cairo_restore(cr);
  g_object_unref(lay);
}

// Pixel width of a single-line row (caret placement in the field).
static double row_pixel_width(cairo_t* cr, const std::string& text, float size, int weight,
                              double maxW) {
  if (text.empty()) return 0.0;
  PangoLayout* lay = make_row_layout(cr, text, size, weight, maxW, true, 0);
  int pw = 0, ph = 0;
  pango_layout_get_pixel_size(lay, &pw, &ph);
  g_object_unref(lay);
  (void)ph;
  return static_cast<double>(pw);
}

}  // namespace

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
  int lastSizeW = 0;
  int lastSizeH = 0;
  bool configured = false;
  bool visible = false;

  // Compact card: fixed small width, height sized to content every paint so
  // rows with no content cost no space.
  static constexpr double kCardW = 480.0;

  double ptr_x = 0;
  double ptr_y = 0;

  // Icon box (logical px) for polkit's themed icon, and the raster size asked
  // of the cache (covers 2x/3x output scales).
  static constexpr double kIconBox = 22.0;
  static constexpr int kIconRasterPx = 96;
  bool icon_theme_applied = false;


  std::string password_field;
  // Cookie of the request the field was last cleared for. show() fires on
  // EVERY service notification (identity/prompt/info lines), so wiping
  // unconditionally eats a password already typed -> an empty submit ->
  // PAM "conversation failed", which burns one of polkit's attempts.
  std::string shown_cookie;

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
    const double pad = 16.0;
    const double contentW = cardW - 2.0 * pad;

    // No scrim: the compositor blurs behind the whole layer surface, so a
    // fullscreen surface smears the entire desktop. The surface is tightened
    // to the card every paint instead.
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Wallpaper-driven palette (matugen / horizon-colors through
    // derived_chrome_colors).
    const double tR = mc.textR, tG = mc.textG, tB = mc.textB;
    const double aR = mc.accentR, aG = mc.accentG, aB = mc.accentB;
    const double eR = mc.notifCriticalOutlineR, eG = mc.notifCriticalOutlineG,
                 eB = mc.notifCriticalOutlineB;

    // Content: only rows that have content cost space (dynamic card height).
    const std::string who = identity_subtitle(snap.identity);
    std::string meta = who;
    if (!snap.action_id.empty()) {
      if (!meta.empty()) meta += "  ·  ";
      meta += snap.action_id;
    }
    const std::string& message = snap.message;
    const std::string& details = snap.details;
    std::string note;
    bool noteIsError = false;
    if (!snap.error.empty()) {
      note = snap.error;
      noteIsError = true;
    } else if (!snap.info.empty()) {
      note = snap.info;
    } else if (snap.state == AuthState::Failed) {
      note = "Authentication failed — try again";
      noteIsError = true;
    }
    const std::string promptLabel = snap.prompt.empty() ? "Password:" : snap.prompt;

    const double titleMaxW = contentW - 32.0 - 28.0;
    const double titleH = row_height(cr, "Authentication Required", 15.0F, 600, titleMaxW, true, 0);
    const double headerH = 26.0;
    const double metaH = row_height(cr, meta, 11.5F, 500, contentW, true, 0);
    const double msgH = row_height(cr, message, 12.5F, 400, contentW, true, 3);
    const double detH = row_height(cr, details, 11.0F, 400, contentW, true, 2);
    const double noteH = row_height(cr, note, 11.5F, noteIsError ? 700 : 400, contentW, true, 0);
    const double fieldH = 34.0;
    const double btnH = 34.0;

    double cardH = pad + headerH + 12.0;
    if (metaH > 0) cardH += metaH + 6.0;
    if (msgH > 0) cardH += msgH + 6.0;
    if (detH > 0) cardH += detH + 6.0;
    if (noteH > 0) cardH += noteH + 6.0;
    cardH += 10.0 + fieldH + 12.0 + btnH + pad;

    // Tighten the surface to the card: the compositor blurs behind the whole
    // layer surface, so a fullscreen surface (even fully transparent) smears
    // the desktop. No scrim — the card floats over the untouched desktop.
    // Requested once per size; the configure round-trip converges immediately.
    const double surfPad = 8.0;
    const int wantW = static_cast<int>(std::ceil(cardW + surfPad * 2.0));
    const int wantH = static_cast<int>(std::ceil(cardH + surfPad * 2.0));
    if (layer && (wantW != lastSizeW || wantH != lastSizeH)) {
      lastSizeW = wantW;
      lastSizeH = wantH;
      zwlr_layer_surface_v1_set_size(layer, static_cast<std::uint32_t>(wantW),
                                     static_cast<std::uint32_t>(wantH));
    }

    const double cx = (W - cardW) * 0.5;
    const double cy = (H - cardH) * 0.5;
    // Card: wallpaper-surface fill + the shared m3 glass treatment (sheen,
    // rim light, drop shadow all come from the Box).
    {
      m3::Box box;
      box.setColor(static_cast<float>(mc.panelFillR), static_cast<float>(mc.panelFillG),
                   static_cast<float>(mc.panelFillB), 0.96F);
      box.setRadius(14.0F);
      box.setGeometry(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(cardW),
                      static_cast<float>(cardH));
      box.setGlassy(true);
      box.paint(cr);
    }

    // Header: the action's themed icon (lock fallback) + title + close.
    {
      if (!icon_theme_applied) {
        dialog_icon_cache().set_icon_theme(eh::config::shell_config_snapshot().dock.iconTheme);
        icon_theme_applied = true;
      }
      const double iconCx = cx + pad + 11.0;
      const double iconCy = cy + pad + 13.0;
      bool drewIcon = false;
      if (!snap.icon_name.empty()) {
        if (const eh::icons::IconEntry* ic =
                dialog_icon_cache().tray_icon_sync(snap.icon_name, kIconRasterPx)) {
          if (ic->surface && ic->width > 0 && ic->height > 0) {
            const double sc = std::min(kIconBox / static_cast<double>(ic->width),
                                       kIconBox / static_cast<double>(ic->height));
            const double dw = ic->width * sc;
            const double dh = ic->height * sc;
            cairo_save(cr);
            cairo_translate(cr, iconCx - dw * 0.5, iconCy - dh * 0.5);
            cairo_scale(cr, dw / ic->width, dh / ic->height);
            cairo_set_source_surface(cr, ic->surface, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
            drewIcon = true;
          }
        }
      }
      if (!drewIcon) {
        eh::shell::draw_material_glyph(cr, iconCx, iconCy, 20.0, "lock", aR, aG, aB, 0.95);
      }
    }

    draw_row(cr, "Authentication Required", 15.0F, 600, titleMaxW, true, 0, tR, tG, tB, 0.97,
             cx + pad + 32.0, cy + pad + (headerH - titleH) * 0.5);

    const double closeSz = 24.0;
    hit_close[0] = cx + cardW - pad - closeSz;
    hit_close[1] = cy + pad + 1.0;
    hit_close[2] = closeSz;
    hit_close[3] = closeSz;
    const int cHov = hover_button == 3 ? 1 : 0;
    const int cPrs = press_button == 3 ? 1 : 0;
    rr(cr, hit_close[0], hit_close[1], hit_close[2], hit_close[3], 7.0);
    cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.45);
    cairo_fill(cr);
    if (cPrs) {
      cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
      rr(cr, hit_close[0], hit_close[1], hit_close[2], hit_close[3], 7.0);
      cairo_fill(cr);
    } else if (cHov) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.15);
      rr(cr, hit_close[0], hit_close[1], hit_close[2], hit_close[3], 7.0);
      cairo_fill(cr);
    }
    eh::shell::draw_material_glyph(cr, hit_close[0] + closeSz * 0.5, hit_close[1] + closeSz * 0.5,
                                   16.0, "close", tR, tG, tB, 0.88);

    // Content rows, top to bottom. Empty rows were measured as zero height and
    // cost nothing.
    double rowY = cy + pad + headerH + 12.0;
    if (metaH > 0) {
      draw_row(cr, meta, 11.5F, 500, contentW, true, 0, aR, aG, aB, 0.92, cx + pad, rowY);
      rowY += metaH + 6.0;
    }
    if (msgH > 0) {
      draw_row(cr, message, 12.5F, 400, contentW, true, 3, tR, tG, tB, 0.80, cx + pad, rowY);
      rowY += msgH + 6.0;
    }
    if (detH > 0) {
      draw_row(cr, details, 11.0F, 400, contentW, true, 2, tR, tG, tB, 0.45, cx + pad, rowY);
      rowY += detH + 6.0;
    }
    if (noteH > 0) {
      if (noteIsError)
        draw_row(cr, note, 11.5F, 700, contentW, true, 0, eR, eG, eB, 1.0, cx + pad, rowY);
      else
        draw_row(cr, note, 11.5F, 400, contentW, true, 0, tR, tG, tB, 0.60, cx + pad, rowY);
      rowY += noteH + 6.0;
    }
    rowY += 10.0;

    // Password field. GNOME Shell shows the prompt as placeholder hint text;
    // bullets mask hidden input, the caret sits at the end.
    const double fieldX = cx + pad;
    const double fieldY = rowY;
    const double fieldW = contentW;
    rr(cr, fieldX, fieldY, fieldW, fieldH, 10.0);
    cairo_set_source_rgba(cr, mc.dockFillR * 0.4, mc.dockFillG * 0.4, mc.dockFillB * 0.4, 0.75);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, aR, aG, aB, 0.8);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);

    std::string shown;
    if (!snap.echo_on) {
      std::size_t n = 0;
      for (unsigned char c : password_field)
        if ((c & 0xC0) != 0x80) ++n;
      if (n > 120) n = 120;
      shown.reserve(n * 3);
      for (std::size_t i = 0; i < n; ++i) shown += "•";
    } else {
      shown = password_field;
      if (shown.size() > 120) shown = shown.substr(0, 117) + "...";
    }
    const double textMaxW = fieldW - 20.0;
    const std::string& fieldText = shown.empty() ? promptLabel : shown;
    const double fieldTextH =
        row_height(cr, fieldText, 13.0F, 400, textMaxW, true, 0);
    const double fieldTextY = fieldY + (fieldH - fieldTextH) * 0.5;
    if (shown.empty())
      draw_row(cr, promptLabel, 13.0F, 400, textMaxW, true, 0, tR, tG, tB, 0.40, fieldX + 10.0,
               fieldTextY);
    else
      draw_row(cr, shown, 13.0F, 400, textMaxW, true, 0, tR, tG, tB, 0.95, fieldX + 10.0,
               fieldTextY);
    {
      const double textW =
          shown.empty() ? 0.0 : std::min(row_pixel_width(cr, shown, 13.0F, 400, textMaxW), textMaxW);
      cairo_set_source_rgba(cr, aR, aG, aB, 0.9);
      cairo_rectangle(cr, fieldX + 10.0 + textW + 1.5, fieldY + (fieldH - 15.0) * 0.5, 1.5, 15.0);
      cairo_fill(cr);
    }

    // Buttons: DE-standard m3 pair, right-aligned. Confirm is armed only when
    // a live prompt can take the answer (matches the empty-submit guards).
    const double btnY = fieldY + fieldH + 12.0;
    const double btnGap = 8.0;
    const double btnRight = cx + cardW - pad;
    const bool canSubmit = snap.state == AuthState::Active && !snap.prompt.empty() &&
                           (snap.echo_on || !password_field.empty());

    m3::Button btnCancel;
    btnCancel.setMinSize(0, 0);
    btnCancel.setLabel("Cancel");
    btnCancel.setStyle(m3::Button::Style::Outlined);
    btnCancel.setSize(m3::Button::Size::S);
    btnCancel.setAccentColor(static_cast<float>(aR), static_cast<float>(aG),
                             static_cast<float>(aB));
    btnCancel.setSurfaceColor(static_cast<float>(mc.panelFillR), static_cast<float>(mc.panelFillG),
                              static_cast<float>(mc.panelFillB));
    btnCancel.setGeometry(0, 0, 0, static_cast<float>(btnH));
    const double cancelW = btnCancel.width();

    m3::Button btnAuth;
    btnAuth.setMinSize(0, 0);
    btnAuth.setLabel("Confirm");
    btnAuth.setStyle(m3::Button::Style::Filled);
    btnAuth.setSize(m3::Button::Size::S);
    btnAuth.setAccentColor(static_cast<float>(aR), static_cast<float>(aG),
                           static_cast<float>(aB));
    btnAuth.setGeometry(0, 0, 0, static_cast<float>(btnH));
    const double authW = btnAuth.width();

    hit_auth[0] = btnRight - authW;
    hit_auth[1] = btnY;
    hit_auth[2] = authW;
    hit_auth[3] = btnH;
    hit_cancel[0] = hit_auth[0] - btnGap - cancelW;
    hit_cancel[1] = btnY;
    hit_cancel[2] = cancelW;
    hit_cancel[3] = btnH;

    btnCancel.setGeometry(static_cast<float>(hit_cancel[0]), static_cast<float>(hit_cancel[1]),
                          static_cast<float>(hit_cancel[2]), static_cast<float>(hit_cancel[3]));
    btnCancel.setHovered(hover_button == 1);
    btnCancel.setPressed(press_button == 1);
    btnAuth.setGeometry(static_cast<float>(hit_auth[0]), static_cast<float>(hit_auth[1]),
                        static_cast<float>(hit_auth[2]), static_cast<float>(hit_auth[3]));
    btnAuth.setEnabled(canSubmit);
    btnAuth.setHovered(canSubmit && hover_button == 2);
    btnAuth.setPressed(canSubmit && press_button == 2);
    btnCancel.paint(cr);
    btnAuth.paint(cr);

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

static void wipe_password(std::string& s) {
  volatile char* out = s.data();
  for (size_t n = 0; n < s.size(); ++n) out[n] = 0;
  s.clear();
}

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
    polkit_log("dlg show: creating surface");
    if (!impl_->compositor || !impl_->layer_shell) return;
    eh::wayland::LayerSurfaceConfig cfg{};
    cfg.nameSpace = eh::shell::kPolkitNamespace;
    cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    // Unanchored + explicit size: the compositor centers a tight box around
    // the card. (A fullscreen surface gets compositor blur smeared across
    // the whole desktop, and a nonzero set_size on full anchors is honored
    // as a small centered box by some compositors.) paint() tightens the
    // size to the measured card every frame; initial size just needs to
    // exist until the first configure round-trip.
    cfg.anchor = 0;
    cfg.width = static_cast<std::uint32_t>(Impl::kCardW);
    cfg.height = 300;
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
    polkit_log("dlg show: re-mapping existing surface");
    impl_->configured = true;
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        impl_->layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    impl_->paint();
  }
  // A new request (new cookie) or a failed attempt clears the field. Any
  // other notification (identity/prompt/info arriving mid-dialog) must NOT:
  // the user may already be typing.
  const PolkitState snap = PolkitAuthService::instance().snapshot();
  if (snap.cookie != impl_->shown_cookie || snap.state == AuthState::Failed) {
    impl_->shown_cookie = snap.cookie;
    wipe_password(impl_->password_field);
  }
}

void PolkitAuthDialog::hide() {
  
  polkit_log("dlg hide");
  impl_->visible = false;
  impl_->configured = false;
  impl_->shown_cookie.clear();
  wipe_password(impl_->password_field);
  if (impl_->buf.wl()) impl_->buf.destroy();
  // Unmap the surface without destroying it so the compositor can
  // re-assign keyboard focus to the dialog on the next show().
  if (impl_->surf) {
    // Deterministically drop the EXCLUSIVE keyboard grab: if this surface
    // somehow stays mapped, the compositor must not keep routing all keys
    // here (that reads as a full-shell freeze). show() re-arms EXCLUSIVE.
    if (impl_->layer)
      zwlr_layer_surface_v1_set_keyboard_interactivity(
          impl_->layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
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
      polkit_log("dlg click: Cancel/close -> cancel_request enter");
      PolkitAuthService::instance().cancel_request();
      polkit_log("dlg click: cancel_request returned");
      impl_->paint();
    } else if (btn == 2 &&
               Impl::point_in_rect(impl_->ptr_x, impl_->ptr_y, impl_->hit_auth)) {
      // An empty password is never valid for a hidden prompt: submitting one
      // burns a polkit attempt ("conversation failed" in the journal) for a
      // mis-click or an Enter typed before focus arrived. Swallow it; echo-on
      // prompts (visible Q&A) still submit.
      if (impl_->password_field.empty() && !snap.echo_on) {
        polkit_log("dlg click: Confirm on empty field -> ignored");
        impl_->paint();
      } else {
        polkit_log("dlg click: Confirm -> submit_response");
        PolkitAuthService::instance().submit_response(impl_->password_field);
        wipe_password(impl_->password_field);
        impl_->paint();
      }
    } else {
      impl_->paint();
    }
  }
}


bool PolkitAuthDialog::consume_keyboard_key(uint32_t state, uint32_t sym, const char* utf8, int utf8_len) {
   
  if (!impl_->visible) return false;

  auto snap = PolkitAuthService::instance().snapshot();
  if (!snap.has_request) return false;

  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return false;

  if (sym == XKB_KEY_Escape) {
    polkit_log("dlg key: Escape -> cancel_request enter");
    PolkitAuthService::instance().cancel_request();
    polkit_log("dlg key: cancel_request returned");
    return true;
  }
  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    if (impl_->password_field.empty() && !snap.echo_on) {
      polkit_log("dlg key: Return on empty field -> ignored");
      return true;
    }
    PolkitAuthService::instance().submit_response(impl_->password_field);
    wipe_password(impl_->password_field);
    return true;
  }
  if (sym == XKB_KEY_BackSpace) {
    if (!impl_->password_field.empty()) {
      while (impl_->password_field.size() > 1 &&
             (impl_->password_field.back() & 0xC0) == 0x80)
        impl_->password_field.pop_back();
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
