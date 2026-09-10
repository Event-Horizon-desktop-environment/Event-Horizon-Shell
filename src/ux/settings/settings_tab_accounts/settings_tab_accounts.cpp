#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pwd.h>
#include <unistd.h>
#include <string>
#include <vector>

#include "m3/controls/containers/button.hpp"
#include "m3/core/primitives/box.hpp"
#include "m3/core/label.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_accounts/settings_tab_accounts.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

static constexpr int kFieldH   = 36;
static constexpr int kBtnW     = 200;

static constexpr int kBtnH     = 34;
static constexpr int kAvatarS  = 80;
static constexpr int kAvatarRad = 16;

// Card spacing.
static constexpr int kCardGapBig = kSpacingL + kSpacingS; // 24
static constexpr int kRowH      = 64;
static constexpr int kFieldTop  = 22; // field Y within a grid row

// Card header at +28, body content starts at +50 (matching dock appearance tab)
static constexpr int kBodyOff = 50;

// Card positions.
static constexpr int kProfileCardTop = kContentTop;
static constexpr int kProfileBody    = kProfileCardTop + kBodyOff;
static constexpr int kProfileCardH   = kBodyOff + kAvatarS + kCardPad;

static constexpr int kPwCardTop    = kProfileCardTop + kProfileCardH + kCardGapBig;
static constexpr int kPwBody       = kPwCardTop + kBodyOff;
static constexpr int kPwGridEnd    = kPwBody + kRowH;
static constexpr int kPwDivY       = kPwGridEnd + kSpacingL;
static constexpr int kPwBtnY       = kPwDivY + 4;
static constexpr int kPwCardH      = kPwBtnY + kBtnH + kCardPad - kPwCardTop;

static constexpr int kCuCardTop    = kPwCardTop + kPwCardH + kCardGapBig;
static constexpr int kCuBody       = kCuCardTop + kBodyOff;
static constexpr int kCuGridEnd    = kCuBody + 2 * kRowH;
static constexpr int kCuDivY       = kCuGridEnd + kSpacingL;
static constexpr int kCuBtnY       = kCuDivY + 4;
static constexpr int kCuCardH      = kCuBtnY + kBtnH + kCardPad - kCuCardTop;

static constexpr int kHnCardTop    = kCuCardTop + kCuCardH + kCardGapBig;
static constexpr int kHnBody       = kHnCardTop + kBodyOff;
static constexpr int kHnGridEnd    = kHnBody + kRowH;
static constexpr int kHnDivY       = kHnGridEnd + kSpacingL;
static constexpr int kHnBtnY       = kHnDivY + 4;
static constexpr int kHnCardH      = kHnBtnY + kBtnH + kCardPad - kHnCardTop;

// State.
static std::string g_current_user_name;
static std::string g_current_user_username;
static std::string g_current_hostname;
static cairo_surface_t* g_avatar_surf = nullptr;
static std::string g_avatar_path;

static void load_user_info() {
  struct passwd* pw = getpwuid(getuid());
  if (pw) {
    {
      std::string raw = pw->pw_gecos ? pw->pw_gecos : "";
      auto pos = raw.find(',');
      g_current_user_name = (pos != std::string::npos) ? raw.substr(0, pos) : raw;
    }
    if (g_current_user_name.empty())
      g_current_user_name = pw->pw_name;
    g_current_user_username = pw->pw_name;
  }
  char hostbuf[256];
  if (gethostname(hostbuf, sizeof(hostbuf)) == 0)
    g_current_hostname = hostbuf;
  else
    g_current_hostname = "localhost";
}

static void load_avatar(const std::string& path) {
  if (g_avatar_surf) {
    cairo_surface_destroy(g_avatar_surf);
    g_avatar_surf = nullptr;
  }
  g_avatar_path = path;
  if (path.empty()) return;
  cairo_surface_t* img = cairo_image_surface_create_from_png(path.c_str());
  if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
    const int iw = cairo_image_surface_get_width(img);
    const int ih = cairo_image_surface_get_height(img);
    if (iw > 0 && ih > 0) {
      g_avatar_surf = img;
      return;
    }
  }
  cairo_surface_destroy(img);
}

static void draw_avatar(App& app, cairo_t* cr, int cx, int cy) {
  float aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB;
  settings_resolve_colors(app, aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB);

  if (g_avatar_surf) {
    cairo_save(cr);
    cairo_round_rect(cr, cx, cy, kAvatarS, kAvatarS, kAvatarRad);
    cairo_clip(cr);
    const int iw = cairo_image_surface_get_width(g_avatar_surf);
    const int ih = cairo_image_surface_get_height(g_avatar_surf);
    double scale = static_cast<double>(kAvatarS) / std::max(iw, ih);
    double sw = iw * scale, sh = ih * scale;
    double ox = cx + (kAvatarS - sw) * 0.5;
    double oy = cy + (kAvatarS - sh) * 0.5;
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, g_avatar_surf, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
  } else {
    auto* pat = cairo_pattern_create_linear(cx, cy, cx + kAvatarS, cy + kAvatarS);
    cairo_pattern_add_color_stop_rgba(pat, 0, aR, aG, aB, 0.85);
    cairo_pattern_add_color_stop_rgba(pat, 1, aR * 0.4f, aG * 0.4f, aB * 0.5f, 0.85);
    cairo_round_rect(cr, cx, cy, kAvatarS, kAvatarS, kAvatarRad);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    char init[2] = {static_cast<char>(toupper(g_current_user_name.empty() ? '?' : g_current_user_name[0])), '\0'};
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 32);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    cairo_text_extents_t te;
    cairo_text_extents(cr, init, &te);
    cairo_move_to(cr, cx + kAvatarS * 0.5 - te.width * 0.5 - te.x_bearing,
                     cy + kAvatarS * 0.5 - te.height * 0.5 - te.y_bearing);
    cairo_show_text(cr, init);
  }
}

// M3 text field.
static void draw_text_field(App& app, cairo_t* cr, int fx, int fy, int fw, int fh,
                            const std::string& text, bool active, bool password,
                            const char* placeholder) {
  {
    m3::Box box;
    float r, g, b;
    if (app.drawChromeMatugen) {
      r = app.drawChrome.panelFillR;
      g = app.drawChrome.panelFillG;
      b = app.drawChrome.panelFillB;
    } else {
      r = static_cast<float>(Theme::BgR);
      g = static_cast<float>(Theme::BgG);
      b = static_cast<float>(Theme::BgB);
    }
    box.setColor(r, g, b, 0.55f);
    box.setRadius(8.f);
    box.setGeometry(fx, fy, fw, fh);
    box.paint(cr);
  }
  if (active) {
    paint_src_accent(app, cr, 0.7);
    cairo_set_line_width(cr, 1.5);
  } else {
    cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.12);
    cairo_set_line_width(cr, 1.0);
  }
  cairo_round_rect(cr, fx, fy, fw, fh, 8.0);
  cairo_stroke(cr);

  if (text.empty() && !active) {
    settings_show_text(cr, fx + 10.0, fy + fh * 0.5 + 5.0, placeholder, 13.f, 400,
                       Theme::TextR, Theme::TextG, Theme::TextB, 0.30f);
  } else {
    std::string display;
    if (password) display.assign(text.size(), '*');
    else          display = text;
    if (display.size() > 50) display.resize(47), display += "...";
    settings_show_text(cr, fx + 10.0, fy + fh * 0.5 + 5.0, display.c_str(), 13.f, 400,
                       Theme::TextR, Theme::TextG, Theme::TextB, 0.88f);
    if (active) {
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 13.0);
      cairo_text_extents_t te;
      cairo_text_extents(cr, display.c_str(), &te);
      cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.60);
      cairo_move_to(cr, fx + 10.0 + te.x_advance, fy + fh * 0.5 + 5.0);
      cairo_show_text(cr, "|");
    }
  }
}

// Semi-transparent accent button with border.
static void draw_accent_btn(App& app, cairo_t* cr, int bx, int by, int bw, int bh,
                            const char* label, bool hovered) {
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  (void)t_r; (void)t_g; (void)t_b;
  (void)s_r; (void)s_g; (void)s_b;

  const float r = std::min(bw, bh) * 0.5f;
  {
    m3::Box box;
    box.setColor(a_r, a_g, a_b, hovered ? 0.35f : 0.25f);
    box.setRadius(r);
    box.setGeometry(static_cast<float>(bx), static_cast<float>(by),
                    static_cast<float>(bw), static_cast<float>(bh));
    box.paint(cr);
  }
  cairo_save(cr);
  cairo_set_source_rgba(cr, a_r, a_g, a_b, hovered ? 0.8f : 0.6f);
  cairo_set_line_width(cr, 1.0f);
  cairo_round_rect(cr, static_cast<double>(bx) + 0.5,
                   static_cast<double>(by) + 0.5,
                   static_cast<double>(bw) - 1.0,
                   static_cast<double>(bh) - 1.0, r);
  cairo_stroke(cr);
  cairo_restore(cr);

  m3::Label lbl;
  lbl.setText(label);
  lbl.setFontSize(12.0f);
  lbl.setFontWeight(600);
  float lw = 0, lh = 0;
  lbl.measureExtents(lw, lh);
  lbl.setColor(1, 1, 1, hovered ? 0.95f : 0.85f);
  lbl.paintAt(cr, bx + (bw - lw) * 0.5f, by + (bh - lh) * 0.5f);
}

// M3 outlined button.
static void draw_outlined_btn(App& app, cairo_t* cr, int bx, int by, int bw, int bh,
                              const char* label, bool hovered) {
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  m3::Button btn;
  btn.setMinSize(0, 0);
  btn.setLabel(label);
  btn.setGeometry(static_cast<float>(bx), static_cast<float>(by),
                  static_cast<float>(bw), static_cast<float>(bh));
  btn.setStyle(m3::Button::Style::Outlined);
  btn.setSize(m3::Button::Size::XS);
  btn.setAccentColor(a_r, a_g, a_b);
  btn.setTextColor(t_r, t_g, t_b);
  btn.setOutlineColor(o_r, o_g, o_b);
  btn.setHovered(hovered);
  btn.paint(cr);
}

// Thin divider.
static void draw_divider(cairo_t* cr, double x, double y, double w) {
  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.06);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, x, y);
  cairo_line_to(cr, x + w, y);
  cairo_stroke(cr);
}

static bool hit(int px, double ly, int x, int y, int w, int h) {
  return px >= x && ly >= y && px < x + w && ly < y + h;
}

// Read-only box.
static void draw_display_box(App& app, cairo_t* cr, int fx, int fy, int fw, int fh,
                             const char* text) {
  m3::Box box;
  float r, g, b;
  if (app.drawChromeMatugen) {
    r = app.drawChrome.panelFillR;
    g = app.drawChrome.panelFillG;
    b = app.drawChrome.panelFillB;
  } else {
    r = static_cast<float>(Theme::BgR);
    g = static_cast<float>(Theme::BgG);
    b = static_cast<float>(Theme::BgB);
  }
  box.setColor(r, g, b, 0.55f);
  box.setRadius(8.f);
  box.setGeometry(fx, fy, fw, fh);
  box.paint(cr);

  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.12);
  cairo_set_line_width(cr, 1.0);
  cairo_round_rect(cr, fx, fy, fw, fh, 8.0);
  cairo_stroke(cr);

  settings_show_text(cr, fx + 10.0, fy + fh * 0.5 + 5.0, text, 13.f, 500,
                     Theme::TextR, Theme::TextG, Theme::TextB, 0.88f);
}

void paint_accounts_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  load_user_info();

  if (g_avatar_path != app.settings.avatarPath) {
    if (app.settings.avatarPath.empty()) {
      const char* home = getenv("HOME");
      if (home) {
        std::string face = std::string(home) + "/.face";
        load_avatar(face);
      }
    } else {
      load_avatar(app.settings.avatarPath);
    }
  }

  // Fixed-width centered column (like monitors tab)
  const int cardWi = std::max(100, (contentW * 68) / 100);
  const int cardXi = contentX + (contentW - cardWi) / 2;
  const double cardX = static_cast<double>(cardXi);
  const double cardW = static_cast<double>(cardWi);

  // Two-column grid geometry
  const int innerW = cardWi - 2 * kCardPad;
  const int colW = (innerW - kSpacingL) / 2;
  const int col1X = cardXi + kCardPad;
  const int col2X = col1X + colW + kSpacingL;

  // Profile card.
  settings_card(app, cr, cardX, kProfileCardTop, cardW, kProfileCardH, glassOv);
  settings_show_text(cr, cardX + kCardPad, kProfileCardTop + 28, "ACCOUNT", 13.f, 500,
                     static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                     static_cast<float>(Theme::TextB), 0.90f);

  {
    draw_avatar(app, cr, col1X, kProfileBody);

    const int rightEdge = cardXi + cardWi - kCardPad;

    auto right_text = [&](cairo_t* cr, double rx, double y, const char* text, float fontSize, int fontWeight, float r, float g, float b, float a) {
      if (!text || !text[0]) return;
      auto* layout = pango_cairo_create_layout(cr);
      auto* desc = pango_font_description_new();
      pango_font_description_set_family(desc, "Inter");
      pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
      pango_font_description_set_weight(desc, static_cast<PangoWeight>(fontWeight));
      pango_layout_set_font_description(layout, desc);
      pango_layout_set_text(layout, text, -1);
      int pw, ph;
      pango_layout_get_pixel_size(layout, &pw, &ph);
      cairo_save(cr);
      cairo_translate(cr, rx - pw, y - static_cast<double>(ph) + 2.0);
      cairo_set_source_rgba(cr, r, g, b, a);
      pango_cairo_show_layout(cr, layout);
      cairo_restore(cr);
      pango_font_description_free(desc);
      g_object_unref(layout);
    };

    right_text(cr, rightEdge, kProfileBody + 24, g_current_user_name.c_str(), 18.f, 700,
               Theme::TextR, Theme::TextG, Theme::TextB, 0.93f);
    right_text(cr, rightEdge, kProfileBody + 46, ("@" + g_current_user_username).c_str(), 13.f, 400,
               Theme::TextR, Theme::TextG, Theme::TextB, 0.50f);

    const int cbw = 140;
    const int cbh = 28;
    const int cbx = rightEdge - cbw;
    const int cby = kProfileBody + 52;
    bool avHover = hit(app.pointerX, app.pointerY + settings_scroll_px(app), cbx, cby, cbw, cbh);
    draw_outlined_btn(app, cr, cbx, cby, cbw, cbh, "Choose Image", avHover);
  }

  // Password card.
  settings_card(app, cr, cardX, kPwCardTop, cardW, kPwCardH, glassOv);
  settings_show_text(cr, cardX + kCardPad, kPwCardTop + 28, "CHANGE PASSWORD", 13.f, 500,
                     static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                     static_cast<float>(Theme::TextB), 0.90f);

  {
    draw_text_field(app, cr, col1X, kPwBody + kFieldTop, colW, kFieldH,
                    app.accountsPwNew, app.accountsActiveField == AccountsField::PwNew, true,
                    "New password");

    draw_text_field(app, cr, col2X, kPwBody + kFieldTop, colW, kFieldH,
                    app.accountsPwConfirm, app.accountsActiveField == AccountsField::PwConfirm, true,
                    "Confirm password");

    draw_divider(cr, cardX, kPwDivY, cardW);

    bool pwHover = hit(app.pointerX, app.pointerY + settings_scroll_px(app),
                       col1X, kPwBtnY, kBtnW, kBtnH);
    draw_accent_btn(app, cr, col1X, kPwBtnY, kBtnW, kBtnH, "Change Password", pwHover);

    if (!app.accountsStatusMsg.empty()) {
      float aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB;
      settings_resolve_colors(app, aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB);
      (void)tR; (void)tG; (void)tB; (void)sR; (void)sG; (void)sB; (void)oR; (void)oG; (void)oB;
      settings_show_text(cr, col1X, kPwBtnY + kBtnH + 8, app.accountsStatusMsg.c_str(), 12.f, 400,
                         aR, aG, aB, 0.8f);
    }
  }

  // Create User card.
  settings_card(app, cr, cardX, kCuCardTop, cardW, kCuCardH, glassOv);
  settings_show_text(cr, cardX + kCardPad, kCuCardTop + 28, "CREATE NEW USER", 13.f, 500,
                     static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                     static_cast<float>(Theme::TextB), 0.90f);

  {
    draw_text_field(app, cr, col1X, kCuBody + kFieldTop, colW, kFieldH,
                    app.accountsCuUsername, app.accountsActiveField == AccountsField::CuUsername,
                    false, "username");

    draw_text_field(app, cr, col2X, kCuBody + kFieldTop, colW, kFieldH,
                    app.accountsCuFullName, app.accountsActiveField == AccountsField::CuFullName,
                    false, "Full Name");

    const int row1Y = kCuBody + kRowH;
    draw_text_field(app, cr, col1X, row1Y + kFieldTop, colW, kFieldH,
                    app.accountsCuPassword, app.accountsActiveField == AccountsField::CuPassword,
                    true, "Password");

    draw_text_field(app, cr, col2X, row1Y + kFieldTop, colW, kFieldH,
                    app.accountsCuConfirm, app.accountsActiveField == AccountsField::CuConfirm,
                    true, "Confirm password");

    draw_divider(cr, cardX, kCuDivY, cardW);

    bool cuHover = hit(app.pointerX, app.pointerY + settings_scroll_px(app),
                       col1X, kCuBtnY, kBtnW, kBtnH);
    draw_accent_btn(app, cr, col1X, kCuBtnY, kBtnW, kBtnH, "Create User", cuHover);
  }

  // Hostname card.
  settings_card(app, cr, cardX, kHnCardTop, cardW, kHnCardH, glassOv);
  settings_show_text(cr, cardX + kCardPad, kHnCardTop + 28, "HOSTNAME", 13.f, 500,
                     static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                     static_cast<float>(Theme::TextB), 0.90f);

  {
    draw_display_box(app, cr, col1X, kHnBody + kFieldTop, colW, kFieldH,
                     g_current_hostname.c_str());

    draw_text_field(app, cr, col2X, kHnBody + kFieldTop, colW, kFieldH,
                    app.accountsHostnameEdit, app.accountsActiveField == AccountsField::Hostname,
                    false, "New hostname");

    draw_divider(cr, cardX, kHnDivY, cardW);

    bool hnHover = hit(app.pointerX, app.pointerY + settings_scroll_px(app),
                       col1X, kHnBtnY, kBtnW, kBtnH);
    draw_accent_btn(app, cr, col1X, kHnBtnY, kBtnW, kBtnH, "Set Hostname", hnHover);
  }


}

bool settings_accounts_consume_pointer_down(App& app, int contentX, int contentW) {
  const double ly = app.pointerY + settings_scroll_px(app);
  const int cardWi = std::max(100, (contentW * 68) / 100);
  const int cardXi = contentX + (contentW - cardWi) / 2;

  const int innerW = cardWi - 2 * kCardPad;
  const int colW = (innerW - kSpacingL) / 2;
  const int col1X = cardXi + kCardPad;
  const int col2X = col1X + colW + kSpacingL;

  // Choose Avatar.
  {
    const int rightEdge = cardXi + cardWi - kCardPad;
    const int cbx = rightEdge - 140;
    const int cby = kProfileBody + 52;
    const int cbw = 140, cbh = 28;
    if (hit(app.pointerX, ly, cbx, cby, cbw, cbh)) {
      app.accountsActiveField = AccountsField::None;
      std::string cmd = "zenity --file-selection --title='Choose Avatar Image' --file-filter='*.png *.jpg *.jpeg' 2>/dev/null";
      FILE* fp = popen(cmd.c_str(), "r");
      if (fp) {
        char buf[4096];
        if (fgets(buf, sizeof(buf), fp)) {
          std::string path = buf;
          while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
            path.pop_back();
          if (!path.empty()) {
            app.settings.avatarPath = path;
            const char* home = getenv("HOME");
            if (home) {
              std::string cp = "cp '" + path + "' " + std::string(home) + "/.face 2>/dev/null";
              (void)std::system(cp.c_str());
            }
            load_avatar(path);
            save_settings(app.settings);
            app.accountsStatusMsg = "Avatar updated.";
          }
        }
        pclose(fp);
      } else {
        app.accountsStatusMsg = "Install zenity for file picker.";
      }
      draw(app);
      return true;
    }
  }

  // Password fields.
  {
    if (hit(app.pointerX, ly, col1X, kPwBody + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::PwNew;
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col2X, kPwBody + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::PwConfirm;
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col1X, kPwBtnY, colW, kBtnH)) {
      app.accountsActiveField = AccountsField::None;
      if (app.accountsPwNew.empty() || app.accountsPwConfirm.empty()) {
        app.accountsStatusMsg = "Please fill in both fields.";
      } else if (app.accountsPwNew != app.accountsPwConfirm) {
        app.accountsStatusMsg = "Passwords do not match.";
      } else if (app.accountsPwNew.size() < 4) {
        app.accountsStatusMsg = "Password too short (min 4 chars).";
      } else {
        std::string cmd = "echo '" + g_current_user_username + ":" + app.accountsPwNew + "' | pkexec chpasswd 2>/dev/null";
        int rc = std::system(cmd.c_str());
        if (rc == 0) {
          app.accountsStatusMsg = "Password changed successfully.";
          app.accountsPwNew.clear();
          app.accountsPwConfirm.clear();
        } else {
          app.accountsStatusMsg = "Failed to change password.";
        }
      }
      draw(app);
      return true;
    }
  }

  // Create User fields.
  {
    // Row 0
    if (hit(app.pointerX, ly, col1X, kCuBody + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::CuUsername;
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col2X, kCuBody + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::CuFullName;
      draw(app);
      return true;
    }
    // Row 1
    const int row1Y = kCuBody + kRowH;
    if (hit(app.pointerX, ly, col1X, row1Y + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::CuPassword;
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col2X, row1Y + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::CuConfirm;
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col1X, kCuBtnY, colW, kBtnH)) {
      app.accountsActiveField = AccountsField::None;
      if (app.accountsCuUsername.empty()) {
        app.accountsStatusMsg = "Username is required.";
      } else if (app.accountsCuPassword.empty()) {
        app.accountsStatusMsg = "Password is required.";
      } else if (app.accountsCuPassword != app.accountsCuConfirm) {
        app.accountsStatusMsg = "Passwords do not match.";
      } else if (app.accountsCuPassword.size() < 4) {
        app.accountsStatusMsg = "Password too short (min 4 chars).";
      } else {
        std::string cmd = "pkexec useradd -m -s /bin/bash";
        if (!app.accountsCuFullName.empty())
          cmd += " -c '" + app.accountsCuFullName + "'";
        cmd += " " + app.accountsCuUsername + " 2>/dev/null";
        int rc = std::system(cmd.c_str());
        if (rc == 0) {
          std::string pwcmd = "echo '" + app.accountsCuUsername + ":" + app.accountsCuPassword + "' | pkexec chpasswd 2>/dev/null";
          int pwrc = std::system(pwcmd.c_str());
          if (pwrc == 0) {
            app.accountsStatusMsg = "User '" + app.accountsCuUsername + "' created.";
            app.accountsCuUsername.clear();
            app.accountsCuFullName.clear();
            app.accountsCuPassword.clear();
            app.accountsCuConfirm.clear();
            load_user_info();
          } else {
            app.accountsStatusMsg = "User created but password failed.";
          }
        } else {
          app.accountsStatusMsg = "Failed to create user (may already exist).";
        }
      }
      draw(app);
      return true;
    }
  }

  // Hostname field.
  {
    if (hit(app.pointerX, ly, col2X, kHnBody + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::Hostname;
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col1X, kHnBtnY, colW, kBtnH)) {
      app.accountsActiveField = AccountsField::None;
      if (app.accountsHostnameEdit.empty()) {
        app.accountsStatusMsg = "Hostname cannot be empty.";
      } else {
        std::string cmd = "pkexec hostnamectl set-hostname '" + app.accountsHostnameEdit + "' 2>/dev/null";
        int rc = std::system(cmd.c_str());
        if (rc == 0) {
          g_current_hostname = app.accountsHostnameEdit;
          app.accountsHostnameEdit.clear();
          app.accountsStatusMsg = "Hostname changed.";
        } else {
          app.accountsStatusMsg = "Failed to set hostname.";
        }
      }
      draw(app);
      return true;
    }
  }

  if (app.accountsActiveField != AccountsField::None) {
    app.accountsActiveField = AccountsField::None;
    draw(app);
    return true;
  }

  return false;
}
