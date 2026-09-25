#include <cairo/cairo.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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
#include "ux/settings/settings_tab_accounts/accounts_users.hpp"

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

// Card positions for the profile card (offsets below it are runtime-derived).
static constexpr int kProfileCardTop = kContentTop;
static constexpr int kProfileBody    = kProfileCardTop + kBodyOff;
static constexpr int kProfileCardH   = kBodyOff + kAvatarS + kCardPad;

// Profile header layout.
static constexpr int kAvatarBtnW = 140;
static constexpr int kAvatarBtnH = 28;
// Minimum info-column width that still allows avatar + text + button to sit
// side by side. Below this the header stacks vertically (see accounts_layout).
static constexpr int kMinInfoW = 150;
static constexpr int kStackGap = 12;

// Status strip: every card reserves room for one 12px status line below its
// last control, so feedback text can never paint over buttons or avatars.
static constexpr int kStatusDy = 21;  // status baseline below last element bottom
static constexpr int kStatusPad = 35; // card bottom below last element bottom

// Users card.
static constexpr int kUserRowH = 56;
static constexpr int kUserAvatarS = 36;

// State (user list itself lives in accounts_users.cpp).
static std::string g_current_hostname;

static void load_hostname() {
  char hostbuf[256];
  if (gethostname(hostbuf, sizeof(hostbuf)) == 0)
    g_current_hostname = hostbuf;
  else
    g_current_hostname = "localhost";
}

// The entry the tab operates on: explicit selection, else the current user,
// else the first enumerated entry. Never null unless enumeration is empty.
static const AccountsUserEntry* selected_entry(const App& app) {
  if (!app.accountsSelectedUser.empty()) {
    if (const auto* e = accounts_find(app.accountsSelectedUser)) return e;
  }
  const std::string cur = accounts_current_username();
  if (const auto* e = accounts_find(cur)) return e;
  const auto& all = accounts_users();
  if (!all.empty()) return &all[0];
  return nullptr;
}

static void draw_avatar_surf(App& app, cairo_t* cr, int cx, int cy, int size,
                             cairo_surface_t* surf, const std::string& name) {
  if (size <= 0) return;
  float aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB;
  settings_resolve_colors(app, aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB);

  if (surf) {
    cairo_save(cr);
    cairo_round_rect(cr, cx, cy, size, size, kAvatarRad);
    cairo_clip(cr);
    const int iw = cairo_image_surface_get_width(surf);
    const int ih = cairo_image_surface_get_height(surf);
    if (iw > 0 && ih > 0) {
      double scale = static_cast<double>(size) / std::max(iw, ih);
      double sw = iw * scale, sh = ih * scale;
      double ox = cx + (size - sw) * 0.5;
      double oy = cy + (size - sh) * 0.5;
      cairo_translate(cr, ox, oy);
      cairo_scale(cr, scale, scale);
      cairo_set_source_surface(cr, surf, 0, 0);
      cairo_paint(cr);
    }
    cairo_restore(cr);
  } else {
    auto* pat = cairo_pattern_create_linear(cx, cy, cx + size, cy + size);
    cairo_pattern_add_color_stop_rgba(pat, 0, aR, aG, aB, 0.85);
    cairo_pattern_add_color_stop_rgba(pat, 1, aR * 0.4f, aG * 0.4f, aB * 0.5f, 0.85);
    cairo_round_rect(cr, cx, cy, size, size, kAvatarRad);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    const char first = name.empty() ? '?' : name[0];
    char init[2] = {static_cast<char>(std::toupper(static_cast<unsigned char>(first))), '\0'};
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 32.0 * static_cast<double>(size) / static_cast<double>(kAvatarS));
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    cairo_text_extents_t te;
    cairo_text_extents(cr, init, &te);
    cairo_move_to(cr, cx + size * 0.5 - te.width * 0.5 - te.x_bearing,
                     cy + size * 0.5 - te.height * 0.5 - te.y_bearing);
    cairo_show_text(cr, init);
  }
}

// M3 text field with horizontal scrolling: long input stays editable instead
// of being silently cut, and the caret always tracks the true buffer end.
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

  const double textY = fy + fh * 0.5 + 5.0;
  if (text.empty() && !active) {
    settings_show_text(cr, fx + 10.0, textY, placeholder, 13.f, 400,
                       Theme::TextR, Theme::TextG, Theme::TextB, 0.30f);
    return;
  }
  std::string display;
  if (password) display.assign(text.size(), '*');
  else          display = text;

  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(13.0 * PANGO_SCALE));
  pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, display.c_str(), -1);
  int pw = 0, ph = 0;
  pango_layout_get_pixel_size(layout, &pw, &ph);

  const double avail = static_cast<double>(fw) - 20.0;
  // Inactive fields show their start; the active field scrolls so the caret
  // (always at the end) stays visible.
  double off = 0.0;
  if (pw > avail) off = active ? (pw - avail) : 0.0;

  cairo_save(cr);
  cairo_rectangle(cr, fx + 1.0, fy + 1.0, fw - 2.0, fh - 2.0);
  cairo_clip(cr);
  cairo_translate(cr, fx + 10.0 - off, textY - static_cast<double>(ph) + 2.0);
  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.88f);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  pango_font_description_free(desc);
  g_object_unref(layout);

  if (active) {
    const double caretX = std::min(fx + 10.0 - off + pw + 2.0, fx + fw - 6.0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.60);
    cairo_move_to(cr, caretX, textY);
    cairo_show_text(cr, "|");
  }
}

// Semi-transparent accent button with border. Destructive variant is red.
static void draw_accent_btn(App& app, cairo_t* cr, int bx, int by, int bw, int bh,
                            const char* label, bool hovered, bool destructive = false) {
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  (void)t_r; (void)t_g; (void)t_b;
  (void)s_r; (void)s_g; (void)s_b;
  if (destructive) {
    a_r = 0.85f; a_g = 0.30f; a_b = 0.28f;
  }

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

// Runtime geometry for the Accounts tab. The profile header reserves a fixed
// avatar column on the left; the name / @username / button block lives in the
// info column to its right and is ellipsized to fit, so it can never slide
// left over the avatar. When the card is too narrow for a side-by-side header
// the info block stacks below the avatar and the profile card grows.
struct AccountsLayout {
  int cardXi = 0, cardWi = 0;
  int col1X = 0, col2X = 0, colW = 0, rightEdge = 0, innerW = 0;
  // Profile card.
  int profileTop = kProfileCardTop;
  int profileBody = kProfileBody;
  int profileH = kProfileCardH;
  int avatarX = 0, avatarY = 0, avatarS = kAvatarS;
  bool stacked = false;
  int infoX = 0, infoW = 0;
  int nameY = 0, userY = 0;       // y-args for right-aligned text (side-by-side)
  int stNameY = 0, stUserY = 0;   // y-args for left-aligned text (stacked)
  int btnX = 0, btnY = 0, btnW = kAvatarBtnW, btnH = kAvatarBtnH;
  int profileStatusY = 0;
  // Users card.
  int usersTop = 0, usersBody = 0, usersH = 0;
  int userRowsY = 0, usersDivY = 0;
  int adminRowY = 0, lockRowY = 0; // toggle rows (label + right toggle), 40 tall
  int nameFieldY = 0;              // full-width full-name field, 36 tall
  int nameBtnY = 0;                // "Set Name", kBtnW wide
  int delBtnY = 0;                 // "Delete User", kBtnW wide
  int usersStatusY = 0;
  // Login card.
  int loginTop = 0, loginBody = 0, loginH = 0;
  int autoRowY = 0, loginStatusY = 0;
  // Password card.
  int pwTop = 0, pwBody = 0, pwGridEnd = 0, pwDivY = 0, pwBtnY = 0, pwCardH = 0;
  int meterY = 0; // password strength bar
  int pwStatusY = 0;
  // Create-user card.
  int cuTop = 0, cuBody = 0, cuAdminY = 0, cuDivY = 0, cuBtnY = 0, cuCardH = 0, cuRow1Y = 0;
  int cuStatusY = 0;
  // Hostname card.
  int hnTop = 0, hnBody = 0, hnGridEnd = 0, hnDivY = 0, hnBtnY = 0, hnCardH = 0;
  int hnStatusY = 0;
};

static AccountsLayout accounts_layout(int contentX, int contentW, size_t nUsers) {
  AccountsLayout L;
  L.cardWi = std::max(100, (contentW * 68) / 100);
  L.cardXi = contentX + (contentW - L.cardWi) / 2;

  L.innerW = L.cardWi - 2 * kCardPad;
  L.colW = (L.innerW - kSpacingL) / 2;
  L.col1X = L.cardXi + kCardPad;
  L.col2X = L.col1X + L.colW + kSpacingL;
  L.rightEdge = L.cardXi + L.cardWi - kCardPad;

  L.profileTop = kProfileCardTop;
  L.profileBody = kProfileBody;
  L.avatarX = L.col1X;
  L.avatarY = L.profileBody;

  L.stacked = (L.innerW < kAvatarS + kSpacingL + kMinInfoW);
  if (!L.stacked) {
    L.avatarS = kAvatarS;
    L.infoX = L.col1X + kAvatarS + kSpacingL;
    L.infoW = L.rightEdge - L.infoX;
    L.nameY = L.profileBody + 24;
    L.userY = L.profileBody + 46;
    L.btnW = std::min(kAvatarBtnW, L.infoW);
    L.btnH = kAvatarBtnH;
    L.btnX = L.rightEdge - L.btnW;
    L.btnY = L.profileBody + 52;
    L.profileStatusY = L.profileBody + kAvatarS + kStatusDy;
    L.profileH = kBodyOff + kAvatarS + kStatusPad;
  } else {
    L.avatarS = std::min(kAvatarS, std::max(L.innerW, 1));
    L.infoX = L.col1X;
    L.infoW = L.innerW;
    L.stNameY = L.profileBody + L.avatarS + kStackGap + 21;
    L.stUserY = L.stNameY + 22;
    L.btnW = std::min(kAvatarBtnW, std::max(L.infoW, 1));
    L.btnH = kAvatarBtnH;
    L.btnX = L.col1X;
    L.btnY = L.stUserY + 12;
    L.profileStatusY = L.btnY + L.btnH + kStatusDy;
    L.profileH = L.btnY + L.btnH + kStatusPad - L.profileTop;
  }

  // Users card: list rows, then admin/lock toggles, full-name field + Set
  // Name, then Delete.
  L.usersTop = L.profileTop + L.profileH + kCardGapBig;
  L.usersBody = L.usersTop + kBodyOff;
  L.userRowsY = L.usersBody;
  const int listH = static_cast<int>(std::max<size_t>(nUsers, 1)) * kUserRowH;
  L.usersDivY = L.userRowsY + listH + kSpacingL;
  L.adminRowY = L.usersDivY + 8;   // 40 tall
  L.lockRowY = L.adminRowY + 40;   // 40 tall
  L.nameFieldY = L.lockRowY + 40 + 8; // 36 tall, full inner width
  L.nameBtnY = L.nameFieldY + kFieldH + 10;
  L.delBtnY = L.nameBtnY + kBtnH + 10;
  L.usersStatusY = L.delBtnY + kBtnH + kStatusDy;
  L.usersH = L.delBtnY + kBtnH + kStatusPad - L.usersTop;

  // Login card: automatic-login toggle row + status line.
  L.loginTop = L.usersTop + L.usersH + kCardGapBig;
  L.loginBody = L.loginTop + kBodyOff;
  L.autoRowY = L.loginBody; // 40 tall
  L.loginStatusY = L.autoRowY + 40 + kStatusDy;
  L.loginH = L.autoRowY + 40 + kStatusPad - L.loginTop;

  L.pwTop = L.loginTop + L.loginH + kCardGapBig;
  L.pwBody = L.pwTop + kBodyOff;
  L.pwGridEnd = L.pwBody + kRowH;
  L.meterY = L.pwBody + kFieldTop + kFieldH + 4;
  L.pwDivY = L.pwGridEnd + kSpacingL;
  L.pwBtnY = L.pwDivY + 4;
  L.pwStatusY = L.pwBtnY + kBtnH + kStatusDy;
  L.pwCardH = L.pwBtnY + kBtnH + kStatusPad - L.pwTop;

  L.cuTop = L.pwTop + L.pwCardH + kCardGapBig;
  L.cuBody = L.cuTop + kBodyOff;
  L.cuRow1Y = L.cuBody + kRowH;
  L.cuAdminY = L.cuBody + 2 * kRowH + 8; // 36 tall toggle row
  L.cuDivY = L.cuAdminY + 36 + kSpacingM;
  L.cuBtnY = L.cuDivY + 4;
  L.cuStatusY = L.cuBtnY + kBtnH + kStatusDy;
  L.cuCardH = L.cuBtnY + kBtnH + kStatusPad - L.cuTop;

  L.hnTop = L.cuTop + L.cuCardH + kCardGapBig;
  L.hnBody = L.hnTop + kBodyOff;
  L.hnGridEnd = L.hnBody + kRowH;
  L.hnDivY = L.hnGridEnd + kSpacingL;
  L.hnBtnY = L.hnDivY + 4;
  L.hnStatusY = L.hnBtnY + kBtnH + kStatusDy;
  L.hnCardH = L.hnBtnY + kBtnH + kStatusPad - L.hnTop;

  return L;
}

int accounts_content_bottom_px(int contentX, int contentW) {
  const size_t n = std::max<size_t>(accounts_users().size(), 1);
  const AccountsLayout L = accounts_layout(contentX, contentW, n);
  return L.hnTop + L.hnCardH;
}

// Single-line text capped to maxW with a trailing ellipsis. Returns the drawn
// pixel width (<= maxW) so callers can right-align without overhang.
static int draw_fit_text(cairo_t* cr, double x, double y, const char* text,
                         float fontSize, int fontWeight,
                         float r, float g, float b, float a, int maxW, bool rightAlign) {
  if (!text || !text[0] || maxW <= 0) return 0;
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(fontWeight));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  pango_layout_set_width(layout, maxW * PANGO_SCALE);
  pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
  int pw, ph;
  pango_layout_get_pixel_size(layout, &pw, &ph);
  if (pw > maxW) pw = maxW;
  cairo_save(cr);
  cairo_translate(cr, rightAlign ? x - pw : x, y - static_cast<double>(ph) + 2.0);
  cairo_set_source_rgba(cr, r, g, b, a);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  pango_font_description_free(desc);
  g_object_unref(layout);
  return pw;
}

// Per-card status line: accent for success, warm red for errors.
static void draw_card_status(App& app, cairo_t* cr, double x, double y,
                             const std::string& msg, bool isErr, int maxW) {
  if (msg.empty()) return;
  float aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB;
  settings_resolve_colors(app, aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB);
  (void)tR; (void)tG; (void)tB; (void)sR; (void)sG; (void)sB; (void)oR; (void)oG; (void)oB;
  if (isErr) draw_fit_text(cr, x, y, msg.c_str(), 12.f, 400, 1.0f, 0.45f, 0.40f, 0.9f, maxW, false);
  else       draw_fit_text(cr, x, y, msg.c_str(), 12.f, 400, aR, aG, aB, 0.85f, maxW, false);
}

// Password strength 0..6 from length + character classes.
static int password_score(const std::string& pw) {
  if (pw.empty()) return 0;
  int score = 0;
  if (pw.size() >= 8) ++score;
  if (pw.size() >= 12) ++score;
  bool lo = false, up = false, dig = false, sym = false;
  for (unsigned char c : pw) {
    if (c >= 'a' && c <= 'z') lo = true;
    else if (c >= 'A' && c <= 'Z') up = true;
    else if (c >= '0' && c <= '9') dig = true;
    else sym = true;
  }
  if (lo && up) ++score;
  if (dig) ++score;
  if (sym) ++score;
  if (pw.size() >= 16) ++score;
  return std::min(score, 6);
}

static void draw_strength_meter(cairo_t* cr, int x, int y, int w, int score) {
  if (w <= 0) return;
  cairo_save(cr);
  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.12);
  cairo_round_rect(cr, x, y, w, 6.0, 3.0);
  cairo_fill(cr);
  const double frac = std::clamp(score / 6.0, 0.0, 1.0);
  double r = 0.85, g = 0.30, b = 0.28; // weak
  if (score >= 4) { r = 0.95; g = 0.75; b = 0.25; }
  if (score >= 5) { r = 0.35; g = 0.80; b = 0.45; }
  if (frac > 0.0) {
    cairo_set_source_rgba(cr, r, g, b, 0.9);
    cairo_round_rect(cr, x, y, std::max(6.0, w * frac), 6.0, 3.0);
    cairo_fill(cr);
  }
  cairo_restore(cr);
}

// Read-only box with ellipsis so long values (hostnames) stay inside.
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

  draw_fit_text(cr, fx + 10.0, fy + fh * 0.5 + 5.0, text, 13.f, 500,
                Theme::TextR, Theme::TextG, Theme::TextB, 0.88f, fw - 20, false);
}

// Keep the focused field on screen.
static void accounts_ensure_visible(App& app, int topY, int botY) {
  const int viewTop = kContentTop + 4;
  const int viewBot = app.height - kSpacingL - 4;
  if (viewBot <= viewTop) return;
  int raw = app.settingsAccountsScrollPx;
  if (topY - raw < viewTop) raw = topY - viewTop;
  else if (botY - raw > viewBot) raw = botY - viewBot;
  if (raw == app.settingsAccountsScrollPx) return;
  app.settingsAccountsScrollPx = raw;
  settings_clamp_accounts_scroll_px(app);
  settings_scroll_sync_after_clamp(app);
}

// ---- form actions (shared by pointer + keyboard handlers) ----

void accounts_action_change_password(App& app) {
  const AccountsUserEntry* sel = selected_entry(app);
  const std::string target = sel ? sel->username : accounts_current_username();
  if (target.empty()) {
    app.accountsStatusMsg = "No user account selected.";
    app.accountsStatusErr = true;
    draw(app);
    return;
  }
  if (app.accountsPwNew.empty() || app.accountsPwConfirm.empty()) {
    app.accountsStatusMsg = "Please fill in both fields.";
    app.accountsStatusErr = true;
  } else if (app.accountsPwNew != app.accountsPwConfirm) {
    app.accountsStatusMsg = "Passwords do not match.";
    app.accountsStatusErr = true;
  } else {
    AccountsActionResult r = accounts_set_password(target, app.accountsPwNew);
    app.accountsStatusMsg = r.msg;
    app.accountsStatusErr = !r.ok;
    if (r.ok) {
      app.accountsPwNew.clear();
      app.accountsPwConfirm.clear();
    }
  }
  draw(app);
}

void accounts_action_create_user(App& app) {
  if (app.accountsCuUsername.empty()) {
    app.accountsCuMsg = "Username is required.";
    app.accountsCuErr = true;
  } else if (app.accountsCuPassword.empty()) {
    app.accountsCuMsg = "Password is required.";
    app.accountsCuErr = true;
  } else if (app.accountsCuPassword != app.accountsCuConfirm) {
    app.accountsCuMsg = "Passwords do not match.";
    app.accountsCuErr = true;
  } else {
    AccountsActionResult r = accounts_create_user(app.accountsCuUsername, app.accountsCuFullName,
                                                  app.accountsCuPassword, app.accountsCuAdmin);
    app.accountsCuMsg = r.msg;
    app.accountsCuErr = !r.ok;
    if (r.ok) {
      app.accountsSelectedUser = app.accountsCuUsername;
      if (const auto* e = accounts_find(app.accountsCuUsername))
        app.accountsUserFullName = e->fullName;
      app.accountsCuUsername.clear();
      app.accountsCuFullName.clear();
      app.accountsCuPassword.clear();
      app.accountsCuConfirm.clear();
      app.accountsCuAdmin = false;
    }
  }
  draw(app);
}

void accounts_action_set_hostname(App& app) {
  if (app.accountsHostnameEdit.empty()) {
    app.accountsHnMsg = "Hostname cannot be empty.";
    app.accountsHnErr = true;
  } else {
    AccountsActionResult r = accounts_set_hostname(app.accountsHostnameEdit);
    app.accountsHnMsg = r.msg;
    app.accountsHnErr = !r.ok;
    if (r.ok) {
      load_hostname();
      app.accountsHostnameEdit.clear();
    }
  }
  draw(app);
}

void accounts_action_set_fullname(App& app) {
  const AccountsUserEntry* sel = selected_entry(app);
  const std::string target = sel ? sel->username : std::string();
  if (target.empty()) {
    app.accountsUsersMsg = "No user account selected.";
    app.accountsUsersErr = true;
    draw(app);
    return;
  }
  AccountsActionResult r = accounts_set_fullname(target, app.accountsUserFullName);
  app.accountsUsersMsg = r.msg;
  app.accountsUsersErr = !r.ok;
  draw(app);
}

void paint_accounts_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  load_hostname();
  accounts_poll_refresh();
  const auto& users = accounts_users();
  const size_t nUsers = std::max<size_t>(users.size(), 1);

  const AccountsLayout L = accounts_layout(contentX, contentW, nUsers);
  const int cardWi = L.cardWi;
  const int cardXi = L.cardXi;
  const double cardX = static_cast<double>(cardXi);
  const double cardW = static_cast<double>(cardWi);

  // Two-column grid geometry
  const int colW = L.colW;
  const int col1X = L.col1X;
  const int col2X = L.col2X;

  const AccountsUserEntry* sel = selected_entry(app);
  const std::string selName = sel ? sel->fullName : std::string();
  const std::string selUser = sel ? sel->username : std::string();
  const std::string selAt = sel ? ("@" + sel->username) : std::string();
  cairo_surface_t* selAvatar = sel ? accounts_avatar_for(
      sel->username, sel->home, sel->isCurrent ? app.settings.avatarPath : std::string())
                                   : nullptr;

  const double py = app.pointerY + settings_scroll_px(app);

  // Profile card (shows the selected account).
  settings_card(app, cr, cardX, L.profileTop, cardW, L.profileH, glassOv);
  settings_show_text(cr, cardX + kCardPad, L.profileTop + 28, "ACCOUNT", 13.f, 500,
                     static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                     static_cast<float>(Theme::TextB), 0.90f);

  {
    draw_avatar_surf(app, cr, L.avatarX, L.avatarY, L.avatarS, selAvatar, selName);

    if (!L.stacked) {
      draw_fit_text(cr, L.rightEdge, L.nameY, selName.c_str(), 18.f, 700,
                    Theme::TextR, Theme::TextG, Theme::TextB, 0.93f, L.infoW, true);
      draw_fit_text(cr, L.rightEdge, L.userY, selAt.c_str(), 13.f, 400,
                    Theme::TextR, Theme::TextG, Theme::TextB, 0.50f, L.infoW, true);
    } else {
      draw_fit_text(cr, L.col1X, L.stNameY, selName.c_str(), 18.f, 700,
                    Theme::TextR, Theme::TextG, Theme::TextB, 0.93f, L.infoW, false);
      draw_fit_text(cr, L.col1X, L.stUserY, selAt.c_str(), 13.f, 400,
                    Theme::TextR, Theme::TextG, Theme::TextB, 0.50f, L.infoW, false);
    }

    bool avHover = hit(app.pointerX, py, L.btnX, L.btnY, L.btnW, L.btnH);
    draw_outlined_btn(app, cr, L.btnX, L.btnY, L.btnW, L.btnH, "Choose Image", avHover);
    draw_card_status(app, cr, cardX + kCardPad, L.profileStatusY,
                     app.accountsProfileMsg, app.accountsProfileErr, L.innerW);
  }

  // Users card.
  settings_card(app, cr, cardX, L.usersTop, cardW, L.usersH, glassOv);
  {
    std::string title = "USERS (" + std::to_string(users.size()) + ")";
    settings_show_text(cr, cardX + kCardPad, L.usersTop + 28, title.c_str(), 13.f, 500,
                       static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                       static_cast<float>(Theme::TextB), 0.90f);
  }
  {
    if (users.empty()) {
      settings_show_text(cr, cardX + kCardPad, L.userRowsY + 30, "No user accounts found.", 13.f, 400,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.55f);
    }
    const int textX = col1X + kUserAvatarS + kSpacingM;
    const int textW = L.rightEdge - textX;
    for (size_t i = 0; i < users.size(); ++i) {
      const auto& u = users[i];
      const int rowY = L.userRowsY + static_cast<int>(i) * kUserRowH;
      const bool isSel = sel && u.username == sel->username;
      if (isSel) {
        float aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB;
        settings_resolve_colors(app, aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB);
        m3::Box box;
        box.setColor(aR, aG, aB, 0.14f);
        box.setRadius(12.f);
        box.setGeometry(static_cast<float>(col1X - 6), static_cast<float>(rowY + 2),
                        static_cast<float>(L.innerW + 12), static_cast<float>(kUserRowH - 4));
        box.paint(cr);
      }
      cairo_surface_t* av = accounts_avatar_for(u.username, u.home, std::string());
      draw_avatar_surf(app, cr, col1X, rowY + (kUserRowH - kUserAvatarS) / 2, kUserAvatarS, av,
                       u.fullName);
      // Badges first (right-aligned), then names fit the remaining width so
      // long names and badges can never overlap each other.
      std::string badges;
      if (u.isCurrent) badges += "You";
      if (u.isAdmin) badges += (badges.empty() ? "" : "  ") + std::string("Admin");
      if (u.lockKnown && u.locked) badges += (badges.empty() ? "" : "  ") + std::string("Locked");
      int nameMaxW = textW;
      if (!badges.empty()) {
        const int bw = draw_fit_text(cr, L.rightEdge, rowY + 26, badges.c_str(), 11.f, 600,
                                     Theme::TextR, Theme::TextG, Theme::TextB, 0.55f,
                                     textW / 2, true);
        nameMaxW = std::max(0, textW - bw - 12);
      }
      draw_fit_text(cr, textX, rowY + 26, u.fullName.c_str(), 15.f, 600,
                    Theme::TextR, Theme::TextG, Theme::TextB, 0.93f, nameMaxW, false);
      draw_fit_text(cr, textX, rowY + 46, ("@" + u.username).c_str(), 12.f, 400,
                    Theme::TextR, Theme::TextG, Theme::TextB, 0.50f, nameMaxW, false);
    }

    draw_divider(cr, cardX, L.usersDivY, cardW);

    // Administrator toggle row.
    {
      const bool on = sel && sel->isAdmin;
      const bool rowHover = hit(app.pointerX, py, col1X, L.adminRowY - 8, L.innerW, 44);
      settings_show_text(cr, cardX + kCardPad, L.adminRowY + 14, "Administrator", 14.f, 400,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.88f);
      settings_show_text(cr, cardX + kCardPad, L.adminRowY + 30,
                         sel ? ("Give '@" + sel->username + "' sudo rights").c_str() : "", 11.f, 400,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.45f);
      settings_toggle(app, cr, cardXi, L.adminRowY, cardWi,
                      static_cast<double>(L.adminRowY - 7), 28.0, on, rowHover ? 1.0 : 0.0);
      (void)rowHover;
    }
    // Account locked toggle row.
    {
      const bool on = sel && sel->lockKnown && sel->locked;
      const bool rowHover = hit(app.pointerX, py, col1X, L.lockRowY - 8, L.innerW, 44);
      settings_show_text(cr, cardX + kCardPad, L.lockRowY + 14, "Account Locked", 14.f, 400,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.88f);
      const char* sub = (sel && !sel->lockKnown) ? "Select to check the lock state"
                                                 : "Prevent this account from signing in";
      settings_show_text(cr, cardX + kCardPad, L.lockRowY + 30, sub, 11.f, 400,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.45f);
      settings_toggle(app, cr, cardXi, L.lockRowY, cardWi,
                      static_cast<double>(L.lockRowY - 7), 28.0, on, rowHover ? 1.0 : 0.0);
    }

    draw_text_field(app, cr, col1X, L.nameFieldY, L.innerW, kFieldH,
                    app.accountsUserFullName,
                    app.accountsActiveField == AccountsField::UserFullName,
                    false, "Full name");

    {
      bool hover = hit(app.pointerX, py, col1X, L.nameBtnY, kBtnW, kBtnH);
      draw_accent_btn(app, cr, col1X, L.nameBtnY, kBtnW, kBtnH, "Set Name", hover);
    }
    {
      const bool armed = !app.accountsDeleteArm.empty() && sel &&
                         app.accountsDeleteArm == sel->username;
      bool hover = hit(app.pointerX, py, col1X, L.delBtnY, kBtnW, kBtnH);
      draw_accent_btn(app, cr, col1X, L.delBtnY, kBtnW, kBtnH,
                      armed ? "Confirm Delete" : "Delete User", hover, true);
    }
    draw_card_status(app, cr, cardX + kCardPad, L.usersStatusY,
                     app.accountsUsersMsg, app.accountsUsersErr, L.innerW);
  }

  // Login card.
  settings_card(app, cr, cardX, L.loginTop, cardW, L.loginH, glassOv);
  settings_show_text(cr, cardX + kCardPad, L.loginTop + 28, "AUTOMATIC LOGIN", 13.f, 500,
                     static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                     static_cast<float>(Theme::TextB), 0.90f);
  {
    const AutologinInfo info = accounts_autologin_info();
    const bool on = info.supported && info.enabled && sel && info.user == sel->username;
    settings_show_text(cr, cardX + kCardPad, L.autoRowY + 14, "Log In Automatically", 14.f, 400,
                       static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                       static_cast<float>(Theme::TextB), info.supported ? 0.88f : 0.40f);
    std::string sub;
    if (!info.supported) sub = "No supported login manager (GDM/SDDM/LightDM) detected";
    else if (info.enabled) sub = "Currently '" + info.user + "' (" + info.dm + ")";
    else sub = "Uses " + info.dm + "; applies to the selected account";
    settings_show_text(cr, cardX + kCardPad, L.autoRowY + 30, sub.c_str(), 11.f, 400,
                       static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                       static_cast<float>(Theme::TextB), 0.45f);
    if (info.supported) {
      const bool rowHover = hit(app.pointerX, py, col1X, L.autoRowY - 8, L.innerW, 44);
      settings_toggle(app, cr, cardXi, L.autoRowY, cardWi,
                      static_cast<double>(L.autoRowY - 7), 28.0, on, rowHover ? 1.0 : 0.0);
    }
    draw_card_status(app, cr, cardX + kCardPad, L.loginStatusY,
                     app.accountsLoginMsg, app.accountsLoginErr, L.innerW);
  }

  // Password card (targets the selected account).
  settings_card(app, cr, cardX, L.pwTop, cardW, L.pwCardH, glassOv);
  settings_show_text(cr, cardX + kCardPad, L.pwTop + 28, "SET PASSWORD", 13.f, 500,
                     static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                     static_cast<float>(Theme::TextB), 0.90f);
  draw_fit_text(cr, L.rightEdge, L.pwTop + 28, sel ? ("For @" + sel->username).c_str() : "", 12.f,
                400, Theme::TextR, Theme::TextG, Theme::TextB, 0.50f, L.innerW / 2, true);

  {
    draw_text_field(app, cr, col1X, L.pwBody + kFieldTop, colW, kFieldH,
                    app.accountsPwNew, app.accountsActiveField == AccountsField::PwNew, true,
                    "New password");

    draw_text_field(app, cr, col2X, L.pwBody + kFieldTop, colW, kFieldH,
                    app.accountsPwConfirm, app.accountsActiveField == AccountsField::PwConfirm, true,
                    "Confirm password");

    if (!app.accountsPwNew.empty()) {
      draw_strength_meter(cr, col1X, L.meterY, L.innerW, password_score(app.accountsPwNew));
    }
    draw_divider(cr, cardX, L.pwDivY, cardW);

    bool pwHover = hit(app.pointerX, py, col1X, L.pwBtnY, kBtnW, kBtnH);
    draw_accent_btn(app, cr, col1X, L.pwBtnY, kBtnW, kBtnH, "Set Password", pwHover);

    draw_card_status(app, cr, col1X, L.pwStatusY,
                     app.accountsStatusMsg, app.accountsStatusErr, L.innerW);
  }

  // Create User card.
  settings_card(app, cr, cardX, L.cuTop, cardW, L.cuCardH, glassOv);
  settings_show_text(cr, cardX + kCardPad, L.cuTop + 28, "CREATE NEW USER", 13.f, 500,
                     static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                     static_cast<float>(Theme::TextB), 0.90f);

  {
    draw_text_field(app, cr, col1X, L.cuBody + kFieldTop, colW, kFieldH,
                    app.accountsCuUsername, app.accountsActiveField == AccountsField::CuUsername,
                    false, "username");

    draw_text_field(app, cr, col2X, L.cuBody + kFieldTop, colW, kFieldH,
                    app.accountsCuFullName, app.accountsActiveField == AccountsField::CuFullName,
                    false, "Full Name");

    draw_text_field(app, cr, col1X, L.cuRow1Y + kFieldTop, colW, kFieldH,
                    app.accountsCuPassword, app.accountsActiveField == AccountsField::CuPassword,
                    true, "Password");

    draw_text_field(app, cr, col2X, L.cuRow1Y + kFieldTop, colW, kFieldH,
                    app.accountsCuConfirm, app.accountsActiveField == AccountsField::CuConfirm,
                    true, "Confirm password");

    {
      const bool rowHover = hit(app.pointerX, py, col1X, L.cuAdminY - 4, L.innerW, 44);
      settings_show_text(cr, cardX + kCardPad, L.cuAdminY + 14, "Administrator", 14.f, 400,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.88f);
      settings_show_text(cr, cardX + kCardPad, L.cuAdminY + 30, "Give the new account sudo rights",
                         11.f, 400,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.45f);
      settings_toggle(app, cr, cardXi, L.cuAdminY, cardWi,
                      static_cast<double>(L.cuAdminY - 7), 28.0, app.accountsCuAdmin,
                      rowHover ? 1.0 : 0.0);
      (void)rowHover;
    }

    draw_divider(cr, cardX, L.cuDivY, cardW);

    bool cuHover = hit(app.pointerX, py, col1X, L.cuBtnY, kBtnW, kBtnH);
    draw_accent_btn(app, cr, col1X, L.cuBtnY, kBtnW, kBtnH, "Create User", cuHover);

    draw_card_status(app, cr, col1X, L.cuStatusY,
                     app.accountsCuMsg, app.accountsCuErr, L.innerW);
  }

  // Hostname card.
  settings_card(app, cr, cardX, L.hnTop, cardW, L.hnCardH, glassOv);
  settings_show_text(cr, cardX + kCardPad, L.hnTop + 28, "HOSTNAME", 13.f, 500,
                     static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                     static_cast<float>(Theme::TextB), 0.90f);

  {
    draw_display_box(app, cr, col1X, L.hnBody + kFieldTop, colW, kFieldH,
                     g_current_hostname.c_str());

    draw_text_field(app, cr, col2X, L.hnBody + kFieldTop, colW, kFieldH,
                    app.accountsHostnameEdit, app.accountsActiveField == AccountsField::Hostname,
                    false, "New hostname");

    draw_divider(cr, cardX, L.hnDivY, cardW);

    bool hnHover = hit(app.pointerX, py, col1X, L.hnBtnY, kBtnW, kBtnH);
    draw_accent_btn(app, cr, col1X, L.hnBtnY, kBtnW, kBtnH, "Set Hostname", hnHover);

    draw_card_status(app, cr, col1X, L.hnStatusY,
                     app.accountsHnMsg, app.accountsHnErr, L.innerW);
  }
}

bool settings_accounts_consume_pointer_down(App& app, int contentX, int contentW) {
  const double ly = app.pointerY + settings_scroll_px(app);
  accounts_poll_refresh();
  const auto& users = accounts_users();
  const size_t nUsers = std::max<size_t>(users.size(), 1);
  const AccountsLayout L = accounts_layout(contentX, contentW, nUsers);
  const int colW = L.colW;
  const int col1X = L.col1X;
  const int col2X = L.col2X;

  const AccountsUserEntry* sel = selected_entry(app);
  const std::string selUser = sel ? sel->username : std::string();

  auto select_user = [&](const std::string& username) {
    app.accountsSelectedUser = username;
    app.accountsPwNew.clear();
    app.accountsPwConfirm.clear();
    app.accountsDeleteArm.clear();
    app.accountsStatusMsg.clear();
    if (const auto* e = accounts_find(username)) app.accountsUserFullName = e->fullName;
    draw(app);
  };

  // User list rows.
  for (size_t i = 0; i < users.size(); ++i) {
    const int rowY = L.userRowsY + static_cast<int>(i) * kUserRowH;
    if (hit(app.pointerX, ly, col1X, rowY, L.innerW, kUserRowH)) {
      if (users[i].username != selUser) select_user(users[i].username);
      return true;
    }
  }

  // Choose avatar (applies to the selected account).
  {
    if (hit(app.pointerX, ly, L.btnX, L.btnY, L.btnW, L.btnH)) {
      app.accountsActiveField = AccountsField::None;
      if (selUser.empty()) {
        app.accountsProfileMsg = "No user account selected.";
        app.accountsProfileErr = true;
        draw(app);
        return true;
      }
      const std::string who = selUser;
      const std::string home = sel ? sel->home : std::string();
      const bool isCur = sel && sel->isCurrent;
      std::string cmd = "zenity --file-selection --title='Choose Avatar Image' --file-filter='*.png *.jpg *.jpeg' 2>/dev/null";
      FILE* fp = popen(cmd.c_str(), "r");
      if (fp) {
        char buf[4096];
        if (fgets(buf, sizeof(buf), fp)) {
          std::string path = buf;
          while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
            path.pop_back();
          if (!path.empty()) {
            AccountsActionResult r = accounts_install_avatar(who, home, path, isCur);
            app.accountsProfileMsg = r.msg;
            app.accountsProfileErr = !r.ok;
            if (r.ok && isCur) {
              app.settings.avatarPath = path;
              save_settings(app.settings);
            }
          }
        }
        pclose(fp);
      } else {
        app.accountsProfileMsg = "Install zenity for file picker.";
        app.accountsProfileErr = true;
      }
      draw(app);
      return true;
    }
  }

  // Administrator toggle row.
  if (hit(app.pointerX, ly, col1X, L.adminRowY - 8, L.innerW, 44)) {
    if (selUser.empty()) {
      app.accountsUsersMsg = "No user account selected.";
      app.accountsUsersErr = true;
    } else {
      const bool want = sel ? !sel->isAdmin : true;
      AccountsActionResult r = accounts_set_admin(selUser, want);
      app.accountsUsersMsg = r.msg;
      app.accountsUsersErr = !r.ok;
    }
    draw(app);
    return true;
  }

  // Lock toggle row: first click checks the state, later clicks flip it.
  if (hit(app.pointerX, ly, col1X, L.lockRowY - 8, L.innerW, 44)) {
    if (selUser.empty()) {
      app.accountsUsersMsg = "No user account selected.";
      app.accountsUsersErr = true;
    } else if (!sel->lockKnown) {
      bool st = false;
      if (accounts_query_locked(selUser, st)) {
        app.accountsUsersMsg = st ? "'" + selUser + "' is currently locked."
                                  : "'" + selUser + "' is currently unlocked.";
        app.accountsUsersErr = false;
      } else {
        app.accountsUsersMsg = "Could not read lock state (authentication needed?).";
        app.accountsUsersErr = true;
      }
    } else {
      AccountsActionResult r = accounts_set_locked(selUser, !sel->locked);
      app.accountsUsersMsg = r.msg;
      app.accountsUsersErr = !r.ok;
    }
    draw(app);
    return true;
  }

  // Full-name field + Set Name.
  if (hit(app.pointerX, ly, col1X, L.nameFieldY, L.innerW, kFieldH)) {
    app.accountsActiveField = AccountsField::UserFullName;
    accounts_ensure_visible(app, L.nameFieldY, L.nameFieldY + kFieldH);
    draw(app);
    return true;
  }
  if (hit(app.pointerX, ly, col1X, L.nameBtnY, kBtnW, kBtnH)) {
    app.accountsActiveField = AccountsField::None;
    accounts_action_set_fullname(app);
    return true;
  }

  // Delete user (two-step).
  if (hit(app.pointerX, ly, col1X, L.delBtnY, kBtnW, kBtnH)) {
    app.accountsActiveField = AccountsField::None;
    if (selUser.empty()) {
      app.accountsUsersMsg = "No user account selected.";
      app.accountsUsersErr = true;
    } else if (app.accountsDeleteArm != selUser) {
      app.accountsDeleteArm = selUser;
      app.accountsUsersMsg = "Delete '" + selUser + "' and their home directory? Click Delete again.";
      app.accountsUsersErr = true;
    } else {
      app.accountsDeleteArm.clear();
      AccountsActionResult r = accounts_delete_user(selUser);
      app.accountsUsersMsg = r.msg;
      app.accountsUsersErr = !r.ok;
      if (r.ok) {
        app.accountsSelectedUser.clear();
        if (const auto* cur = selected_entry(app)) app.accountsUserFullName = cur->fullName;
      }
    }
    draw(app);
    return true;
  }

  // Automatic login toggle row.
  {
    const AutologinInfo info = accounts_autologin_info();
    if (info.supported && hit(app.pointerX, ly, col1X, L.autoRowY - 8, L.innerW, 44)) {
      app.accountsActiveField = AccountsField::None;
      if (selUser.empty()) {
        app.accountsLoginMsg = "No user account selected.";
        app.accountsLoginErr = true;
      } else {
        const bool on = info.enabled && info.user == selUser;
        AccountsActionResult r = accounts_set_autologin(selUser, !on);
        app.accountsLoginMsg = r.msg;
        app.accountsLoginErr = !r.ok;
      }
      draw(app);
      return true;
    }
  }

  // Password fields.
  {
    if (hit(app.pointerX, ly, col1X, L.pwBody + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::PwNew;
      accounts_ensure_visible(app, L.pwBody + kFieldTop, L.pwBody + kFieldTop + kFieldH);
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col2X, L.pwBody + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::PwConfirm;
      accounts_ensure_visible(app, L.pwBody + kFieldTop, L.pwBody + kFieldTop + kFieldH);
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col1X, L.pwBtnY, kBtnW, kBtnH)) {
      app.accountsActiveField = AccountsField::None;
      accounts_action_change_password(app);
      return true;
    }
  }

  // Create User fields.
  {
    // Row 0
    if (hit(app.pointerX, ly, col1X, L.cuBody + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::CuUsername;
      accounts_ensure_visible(app, L.cuBody + kFieldTop, L.cuBody + kFieldTop + kFieldH);
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col2X, L.cuBody + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::CuFullName;
      accounts_ensure_visible(app, L.cuBody + kFieldTop, L.cuBody + kFieldTop + kFieldH);
      draw(app);
      return true;
    }
    // Row 1
    if (hit(app.pointerX, ly, col1X, L.cuRow1Y + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::CuPassword;
      accounts_ensure_visible(app, L.cuRow1Y + kFieldTop, L.cuRow1Y + kFieldTop + kFieldH);
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col2X, L.cuRow1Y + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::CuConfirm;
      accounts_ensure_visible(app, L.cuRow1Y + kFieldTop, L.cuRow1Y + kFieldTop + kFieldH);
      draw(app);
      return true;
    }
    // Administrator toggle row for the new account.
    if (hit(app.pointerX, ly, col1X, L.cuAdminY - 4, L.innerW, 44)) {
      app.accountsCuAdmin = !app.accountsCuAdmin;
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col1X, L.cuBtnY, kBtnW, kBtnH)) {
      app.accountsActiveField = AccountsField::None;
      accounts_action_create_user(app);
      return true;
    }
  }

  // Hostname field.
  {
    if (hit(app.pointerX, ly, col2X, L.hnBody + kFieldTop, colW, kFieldH)) {
      app.accountsActiveField = AccountsField::Hostname;
      accounts_ensure_visible(app, L.hnBody + kFieldTop, L.hnBody + kFieldTop + kFieldH);
      draw(app);
      return true;
    }
    if (hit(app.pointerX, ly, col1X, L.hnBtnY, kBtnW, kBtnH)) {
      app.accountsActiveField = AccountsField::None;
      accounts_action_set_hostname(app);
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
