#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_keyboard/settings_tab_keyboard.hpp"
#include "ux/settings/settings_tab_accounts/accounts_users.hpp"
#include "ux/settings/settings_tab_hyprland/settings_tab_hyprland.hpp"
#include "ux/settings/settings_tab_mango/settings_tab_mango.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "m3/core/label.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

using namespace keyboard_tab;

static const std::vector<std::pair<const char*, const char*>> kKnownLayouts = {
  {"us", "US (QWERTY)"},
  {"us(dvorak)", "US (Dvorak)"},
  {"us(colemak)", "US (Colemak)"},
  {"gb", "UK (QWERTY)"},
  {"de", "German (QWERTZ)"},
  {"fr", "French (AZERTY)"},
  {"it", "Italian"},
  {"es", "Spanish"},
  {"pt", "Portuguese"},
  {"br", "Portuguese (Brazil)"},
  {"ru", "Russian"},
  {"jp", "Japanese"},
  {"kr", "Korean"},
  {"se", "Swedish"},
  {"no", "Norwegian"},
  {"dk", "Danish"},
  {"fi", "Finnish"},
  {"pl", "Polish"},
  {"cz", "Czech"},
  {"sk", "Slovak"},
  {"hu", "Hungarian"},
  {"ro", "Romanian"},
  {"bg", "Bulgarian"},
  {"gr", "Greek"},
  {"il", "Hebrew"},
  {"tr", "Turkish"},
  {"ara", "Arabic"},
  {"ca", "Canadian"},
};

static void ensure_layouts_populated(App& app) {
  // Never clobber the user's list: only seed a default when empty, and keep
  // the active layout inside the enabled set.
  if (app.settings.keyboardLayouts.empty()) {
    app.settings.keyboardLayouts.emplace_back("us");
  }
  const auto& ls = app.settings.keyboardLayouts;
  if (std::find(ls.begin(), ls.end(), app.settings.keyboardLayout) == ls.end()) {
    app.settings.keyboardLayout = ls.front();
  }
}

namespace keyboard_tab {

int sources_card_top() {
  return kCardTop + kLayoutCardH + kCardGap;
}

int sources_card_h(const App& app) {
  const int n = static_cast<int>(app.settings.keyboardLayouts.size());
  return 52 + std::max(1, n) * kSourcesRowH + 48 + 12;
}

int behavior_card_top(const App& app) {
  return sources_card_top() + sources_card_h(app) + kCardGap;
}

int repeat_card_top(const App& app) {
  return behavior_card_top(app) + kBehaviorCardH + kCardGap;
}

int language_card_top(const App& app) {
  return repeat_card_top(app) + kRepeatCardH + kCardGap;
}

int language_card_h() {
  // 52 header + 44 title/current + rows + 12 + 34 apply + 10 + 52 x11 + 8 + 28 status.
  const int n = keyboard_locale_count();
  return 52 + 44 + std::max(1, n) * kLocaleRowH + 12 + 34 + 10 + 52 + 8 + 28;
}

int content_bottom(const App& app) {
  return language_card_top(app) + language_card_h();
}

void repeat_slider_track_geom(int contentX, int contentW, int* trX, int* trW) {
  const int cardXi = contentX + 8;
  const int cardWi = contentW - 16;
  const int rightRail = cardXi + cardWi - kSpacingXL;
  int x = cardXi + kCardPad;
  // Fixed reserve for the persistent value label ("1000 ms"), so the track
  // never jitters while dragging and the value is always visible.
  int w = rightRail - kRepeatValueW - 12 - x;
  if (w < 40) w = 40;
  if (trX) *trX = x;
  if (trW) *trW = w;
}

static int repeat_slider_value_x(int contentX, int contentW) {
  const int cardXi = contentX + 8;
  const int cardWi = contentW - 16;
  return cardXi + cardWi - kSpacingXL; // rightRail: values right-align here
}

} // namespace keyboard_tab
static constexpr int kLayoutBodyTop = kBodyTop;

extern const char* kSwitchShortcutLabels[];
extern const int kSwitchShortcutCount;
extern const char* kCapsLockLabels[];
extern const int kCapsLockCount;
extern const char* kComposeKeyLabels[];
extern const int kComposeKeyCount;

const char* kSwitchShortcutLabels[] = {
  "Alt+Shift",
  "Ctrl+Shift",
  "Super+Space",
  "Caps Lock",
  "Alt+Shift (toggle)",
  "Ctrl+Shift (toggle)",
  "None (disabled)"
};
const int kSwitchShortcutCount = 7;

const char* kCapsLockLabels[] = {
  "Caps Lock (default)",
  "Ctrl (both)",
  "Swap Escape/Caps Lock",
  "Caps Lock disabled"
};
const int kCapsLockCount = 4;

const char* kComposeKeyLabels[] = {
  "None (disabled)",
  "Right Alt (AltGr)",
  "Right Ctrl",
  "Menu key",
  "Right Win/Super"
};
const int kComposeKeyCount = 5;

static const char* switch_shortcut_label(int mode) {
  if (mode >= 0 && mode < kSwitchShortcutCount) return kSwitchShortcutLabels[mode];
  return "Alt+Shift";
}

static const char* caps_lock_label(int mode) {
  if (mode >= 0 && mode < kCapsLockCount) return kCapsLockLabels[mode];
  return "Caps Lock (default)";
}

static const char* compose_key_label(int mode) {
  if (mode >= 0 && mode < kComposeKeyCount) return kComposeKeyLabels[mode];
  return "None (disabled)";
}

const char* keyboard_layout_display_name(const std::string& layout) {
  for (const auto& [code, name] : kKnownLayouts) {
    if (layout == code) return name;
  }
  // Unknown codes (e.g. from localectl): prettify a static buffer.
  // "de(neo)" -> "De (neo)".
  static std::string pretty;
  pretty.clear();
  const size_t paren = layout.find('(');
  std::string base = (paren == std::string::npos) ? layout : layout.substr(0, paren);
  if (!base.empty()) {
    base[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(base[0])));
    pretty = base;
  } else {
    pretty = layout;
    return pretty.c_str();
  }
  if (paren != std::string::npos) {
    pretty += " (" + layout.substr(paren + 1, layout.size() - paren - 2) + ")";
  }
  return pretty.c_str();
}

namespace {
// fork+exec with a literal argv (no shell). Captures stdout+stderr when out
// is non-null. Returns the exit status, or -1 on spawn failure.
int kb_spawn(const std::vector<std::string>& argv, std::string* out) {
  int outPipe[2] = {-1, -1};
  const bool capture = (out != nullptr);
  if (capture && pipe(outPipe) != 0) return -1;
  const pid_t pid = fork();
  if (pid < 0) {
    if (capture) { close(outPipe[0]); close(outPipe[1]); }
    return -1;
  }
  if (pid == 0) {
    if (capture) {
      dup2(outPipe[1], STDOUT_FILENO);
      dup2(outPipe[1], STDERR_FILENO);
      close(outPipe[0]); close(outPipe[1]);
    } else {
      const int devnull = open("/dev/null", O_WRONLY);
      if (devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > 2) close(devnull);
      }
    }
    std::vector<char*> ea;
    ea.reserve(argv.size() + 1);
    for (const auto& a : argv) ea.push_back(const_cast<char*>(a.c_str()));
    ea.push_back(nullptr);
    if (!ea.empty() && ea[0]) execvp(ea[0], ea.data());
    _exit(127);
  }
  if (capture) close(outPipe[1]);
  std::string buf;
  if (capture) {
    std::array<char, 4096> tmp{};
    for (;;) {
      ssize_t n = read(outPipe[0], tmp.data(), tmp.size());
      if (n < 0) {
        if (errno == EINTR) continue;
        break;
      }
      if (n == 0) break;
      buf.append(tmp.data(), static_cast<size_t>(n));
      if (buf.size() > 262144) break;
    }
    close(outPipe[0]);
    *out = std::move(buf);
  }
  int status = -1;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) break;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string kb_read_file(const std::string& path) {
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return {};
  std::string out;
  std::array<char, 1024> b{};
  size_t n = 0;
  while ((n = fread(b.data(), 1, b.size(), f)) > 0) {
    out.append(b.data(), n);
    if (out.size() > 65536) break;
  }
  fclose(f);
  return out;
}

// Installed X11 layouts from the system database (cached, process lifetime).
const std::vector<std::string>& system_layout_cache() {
  static std::vector<std::string> cache;
  static bool done = false;
  if (done) return cache;
  done = true;
  std::string out;
  if (kb_spawn({"localectl", "list-x11-keymap-layouts"}, &out) == 0) {
    std::string::size_type s = 0;
    while (s < out.size()) {
      auto e = out.find('\n', s);
      std::string line = out.substr(s, e == std::string::npos ? e : e - s);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
      if (!line.empty() && line.find_first_of(" \t") == std::string::npos) cache.push_back(line);
      if (e == std::string::npos) break;
      s = e + 1;
    }
  }
  if (cache.empty()) {
    for (const auto& [code, name] : kKnownLayouts) {
      (void)name;
      cache.emplace_back(code);
    }
  }
  return cache;
}

const std::vector<std::string>& system_locale_cache() {
  static std::vector<std::string> cache;
  static bool done = false;
  if (done) return cache;
  done = true;
  std::string out;
  if (kb_spawn({"localectl", "list-locales"}, &out) == 0) {
    std::string::size_type s = 0;
    while (s < out.size()) {
      auto e = out.find('\n', s);
      std::string line = out.substr(s, e == std::string::npos ? e : e - s);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
      if (!line.empty() && line.find_first_of(" \t") == std::string::npos) cache.push_back(line);
      if (e == std::string::npos) break;
      s = e + 1;
    }
  }
  if (cache.empty()) cache.emplace_back("C.UTF-8");
  return cache;
}

std::string localectl_status_line(const char* key) {
  std::string out;
  if (kb_spawn({"localectl", "status"}, &out) != 0) return {};
  std::string::size_type s = 0;
  const std::string k = key;
  while (s < out.size()) {
    auto e = out.find('\n', s);
    std::string line = out.substr(s, e == std::string::npos ? e : e - s);
    auto c = line.find(':');
    if (c != std::string::npos) {
      std::string name = line.substr(0, c);
      std::string::size_type b = name.find_first_not_of(" \t");
      std::string::size_type t = name.find_last_not_of(" \t");
      if (b != std::string::npos && name.substr(b, t - b + 1) == k) {
        std::string v = line.substr(c + 1);
        b = v.find_first_not_of(" \t");
        t = v.find_last_not_of(" \t\r");
        if (b == std::string::npos) return {};
        return v.substr(b, t - b + 1);
      }
    }
    if (e == std::string::npos) break;
    s = e + 1;
  }
  return {};
}

bool valid_xkb_codes(const std::string& list) {
  if (list.empty() || list.size() > 256) return false;
  for (char c : list) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '(' || c == ')' || c == ',' ||
          c == '_' || c == '-'))
      return false;
  }
  return true;
}

// Switch the running compositor to layout index idx right now (best effort).
// Returns true when the switch command was accepted.
bool keyboard_activate_now(const App& app, int idx) {
  if (app.monitorsTab.kind == CompositorKind::Hyprland) {
    std::string out;
    if (kb_spawn({"hyprctl", "-j", "devices"}, &out) != 0 || out.empty()) return false;
    nlohmann::json dev;
    try {
      dev = nlohmann::json::parse(out);
    } catch (...) {
      return false;
    }
    if (!dev.is_object() || !dev.contains("keyboards") || !dev["keyboards"].is_array() ||
        dev["keyboards"].empty())
      return false;
    const auto& kbs = dev["keyboards"];
    const nlohmann::json* kb = &kbs[0];
    for (const auto& k : kbs) {
      if (k.is_object() && k.value("main", false)) {
        kb = &k;
        break;
      }
    }
    if (!kb->is_object()) return false;
    const std::string name = kb->value("name", std::string{});
    if (name.empty()) return false;
    return kb_spawn({"hyprctl", "dispatch", "switchxkblayout", name, std::to_string(idx)},
                    nullptr) == 0;
  }
  if (app.monitorsTab.kind == CompositorKind::Sway) {
    return kb_spawn({"swaymsg", "input", "type:keyboard", "xkb_switch_layout",
                     std::to_string(idx)},
                    nullptr) == 0;
  }
  return false;
}
} // namespace

std::vector<std::string> keyboard_available_layouts(const App& app) {
  std::vector<std::string> out;
  for (const auto& code : system_layout_cache()) {
    const auto& ls = app.settings.keyboardLayouts;
    if (std::find(ls.begin(), ls.end(), code) == ls.end()) out.emplace_back(code);
  }
  return out;
}

std::vector<std::string> keyboard_system_locales() { return system_locale_cache(); }

int keyboard_locale_count() { return static_cast<int>(system_locale_cache().size()); }

namespace {
int g_kb_sys_tick = 0;
int g_kb_locale_at = -1000000;
std::string g_kb_locale_cache;
int g_kb_x11_at = -1000000;
std::string g_kb_x11_cache;
constexpr int kKbSysCacheEvery = 240;
} // namespace

std::string keyboard_system_locale() {
  // /etc/locale.conf changes only via Apply below (or an external tool);
  // re-read at most every 240 calls instead of once per paint.
  ++g_kb_sys_tick;
  if (g_kb_locale_at >= 0 && g_kb_sys_tick - g_kb_locale_at < kKbSysCacheEvery) return g_kb_locale_cache;
  g_kb_locale_at = g_kb_sys_tick;
  const std::string conf = kb_read_file("/etc/locale.conf");
  std::string::size_type s = 0;
  g_kb_locale_cache.clear();
  while (s < conf.size()) {
    auto e = conf.find('\n', s);
    std::string line = conf.substr(s, e == std::string::npos ? e : e - s);
    if (line.compare(0, 5, "LANG=") == 0) {
      std::string v = line.substr(5);
      if (!v.empty() && v.front() == '"') v = v.substr(1, v.size() - 2);
      g_kb_locale_cache = v;
      break;
    }
    if (e == std::string::npos) break;
    s = e + 1;
  }
  if (g_kb_locale_cache.empty()) {
    const char* env = getenv("LANG");
    if (env) g_kb_locale_cache = env;
  }
  return g_kb_locale_cache;
}

static void keyboard_invalidate_sys_caches() {
  g_kb_locale_at = -1000000;
  g_kb_x11_at = -1000000;
}

std::string keyboard_x11_keymap_line() {
  // localectl spawns are cached on the same cadence as the locale above.
  ++g_kb_sys_tick;
  if (g_kb_x11_at >= 0 && g_kb_sys_tick - g_kb_x11_at < kKbSysCacheEvery) return g_kb_x11_cache;
  g_kb_x11_at = g_kb_sys_tick;
  std::string layout = localectl_status_line("X11 Layout");
  if (layout.empty() || layout == "n/a") {
    g_kb_x11_cache.clear();
    return g_kb_x11_cache;
  }
  std::string variant = localectl_status_line("X11 Variant");
  if (!variant.empty() && variant != "n/a") {
    // Pair per-layout variants positionally when counts match.
    int cl = 0, cv = 0;
    for (char c : layout) cl += (c == ',');
    for (char c : variant) cv += (c == ',');
    if (cl == cv) {
      g_kb_x11_cache = layout + " (" + variant + ")";
      return g_kb_x11_cache;
    }
  }
  g_kb_x11_cache = layout;
  return g_kb_x11_cache;
}

bool keyboard_apply_locale(const std::string& locale, std::string& msg) {
  const auto& all = system_locale_cache();
  if (std::find(all.begin(), all.end(), locale) == all.end()) {
    msg = "Unknown locale.";
    return false;
  }
  PrivResult r = accounts_run_priv({"pkexec", "localectl", "set-locale", "LANG=" + locale});
  if (r.status == 0) {
    keyboard_invalidate_sys_caches();
    msg = "System language set to '" + locale + "' (next login).";
    return true;
  }
  msg = "Failed to set system language.";
  return false;
}

bool keyboard_apply_x11_keymap(const App& app, std::string& msg) {
  const auto& s = app.settings;
  if (s.keyboardLayouts.empty()) {
    msg = "No keyboard layouts enabled.";
    return false;
  }
  std::vector<std::string> bases, variants;
  for (const auto& code : s.keyboardLayouts) {
    const size_t paren = code.find('(');
    if (paren == std::string::npos || code.back() != ')') {
      bases.push_back(code);
      variants.emplace_back();
    } else {
      bases.push_back(code.substr(0, paren));
      variants.push_back(code.substr(paren + 1, code.size() - paren - 2));
    }
  }
  auto join = [](const std::vector<std::string>& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
      if (i) o.push_back(',');
      o += v[i];
    }
    return o;
  };
  const std::string layoutList = join(bases);
  const std::string variantList = join(variants);
  if (!valid_xkb_codes(layoutList)) {
    msg = "Layout codes contain unsupported characters.";
    return false;
  }
  std::vector<std::string> argv = {"pkexec", "localectl", "set-x11-keymap", layoutList};
  if (!variantList.empty() && variantList.find_first_not_of(',') != std::string::npos) {
    if (!valid_xkb_codes(variantList)) {
      msg = "Variant codes contain unsupported characters.";
      return false;
    }
    std::string model = localectl_status_line("X11 Model");
    if (model.empty() || model == "n/a") model = "pc105";
    argv.push_back(model);
    argv.push_back(variantList);
  }
  PrivResult r = accounts_run_priv(argv);
  if (r.status == 0) {
    keyboard_invalidate_sys_caches();
    msg = "Login-screen keyboard set to '" + layoutList + "'.";
    return true;
  }
  msg = "Failed to set login-screen keyboard.";
  return false;
}

static void paint_slider_pill(App& app, cairo_t* cr, int titleY, int trX, int trY, int trW) {
   
  const double textCap = static_cast<double>(titleY) - 11.0;
  const double slTop = static_cast<double>(trY) - 10.0;
  const double slBot = static_cast<double>(trY) + 26.0;
  constexpr double kPad = 2.0;
  const double pX = static_cast<double>(trX) - 4.0;
  const double pY = std::min(textCap, slTop) - kPad;
  const double pW = static_cast<double>(trW) + 56.0;
  const double pH = std::max(static_cast<double>(titleY), slBot) + kPad - pY;
  const double pR = std::max(4.0, std::min(pH, pW) * 0.18);
  cairo_save(cr);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_GOOD);
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
    box.setColor(r, g, b, 0.22f);
    box.setRadius(static_cast<float>(pR));
    box.setGeometry(static_cast<float>(pX), static_cast<float>(pY),
                    static_cast<float>(pW), static_cast<float>(pH));
    box.paint(cr);
  }
  cairo_round_rect(cr, pX, pY, pW, pH, pR);
  paint_src_glass_hi(app, cr, 0.06);
  cairo_set_line_width(cr, 0.5);
  cairo_stroke(cr);
  cairo_restore(cr);
}

// Push persisted keyboard settings into the active compositor (best effort).
// Hyprland/Mango own their input config, so mirror the generic settings there
// and commit; other compositors keep the persisted values for session start.
void keyboard_push_to_compositor(App& app) {
  const auto& s = app.settings;
  if (s.keyboardLayouts.empty()) return;

  // Split "us(dvorak)" codes into base layouts + aligned variants.
  std::vector<std::string> bases;
  std::vector<std::string> variants;
  for (const auto& code : s.keyboardLayouts) {
    const size_t paren = code.find('(');
    if (paren == std::string::npos || code.back() != ')') {
      bases.push_back(code);
      variants.emplace_back();
    } else {
      bases.push_back(code.substr(0, paren));
      variants.push_back(code.substr(paren + 1, code.size() - paren - 2));
    }
  }
  auto join = [](const std::vector<std::string>& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
      if (i) o.push_back(',');
      o += v[i];
    }
    return o;
  };
  const std::string layoutList = join(bases);
  const std::string variantList = join(variants);

  static const char* kCapsOpts[] = {"", "ctrl:nocaps", "caps:swapescape", "caps:none"};
  static const char* kComposeOpts[] = {"", "compose:ralt", "compose:rctrl", "compose:menu", "compose:rwin"};
  static const char* kSwitchOpts[] = {"grp:alt_shift_toggle", "grp:ctrl_shift_toggle", "grp:win_space_toggle",
                                      "grp:caps_toggle", "grp:alt_shift_toggle", "grp:ctrl_shift_toggle", ""};
  std::string opts;
  auto addOpt = [&](const char* o) {
    if (o && o[0]) {
      if (!opts.empty()) opts.push_back(',');
      opts += o;
    }
  };
  if (s.keyboardCapsLockBehavior >= 0 && s.keyboardCapsLockBehavior < 4) addOpt(kCapsOpts[s.keyboardCapsLockBehavior]);
  if (s.keyboardComposeKey >= 0 && s.keyboardComposeKey < 5) addOpt(kComposeOpts[s.keyboardComposeKey]);
  if (s.keyboardSwitchShortcut >= 0 && s.keyboardSwitchShortcut < 7) addOpt(kSwitchOpts[s.keyboardSwitchShortcut]);

  if (app.monitorsTab.kind == CompositorKind::Hyprland) {
    app.hyprlandConfig.input.kb_layout = layoutList;
    app.hyprlandConfig.input.kb_variant = variantList;
    app.hyprlandConfig.input.kb_options = opts;
    app.hyprlandConfig.input.repeat_rate = s.keyboardRepeatRate;
    app.hyprlandConfig.input.repeat_delay = s.keyboardRepeatDelay;
    app.hyprlandConfig.input.numlock_by_default = s.keyboardNumlock;
    app.hyprlandConfig.misc.middle_click_paste = s.keyboardMiddleClickPaste;
    hyprland_commit_cfg(app);
  } else if (app.monitorsTab.kind == CompositorKind::Mango) {
    app.mangoConfig.xkb_rules_layout = layoutList;
    app.mangoConfig.xkb_rules_variant = variantList;
    app.mangoConfig.xkb_rules_options = opts;
    app.mangoConfig.repeat_rate = s.keyboardRepeatRate;
    app.mangoConfig.repeat_delay = s.keyboardRepeatDelay;
    app.mangoConfig.numlockon = s.keyboardNumlock;
    mango_commit_cfg(app);
  } else if (app.monitorsTab.kind == CompositorKind::Sway) {
    // No shell: Sway commands go through fork+exec with a literal argv.
    kb_spawn({"swaymsg", "input", "type:keyboard", "xkb_layout", layoutList, "xkb_variant",
              variantList, "xkb_options", opts, "xkb_numlock",
              s.keyboardNumlock ? "enable" : "disable"},
             nullptr);
  }
}

void paint_keyboard_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
   
  ensure_layouts_populated(app);
  const double cardX = static_cast<double>(contentX + 8);
  const double cardW = static_cast<double>(contentW - 16);
  const int rightRail = static_cast<int>(cardX + cardW - kSpacingXL);

  // Layout card.
  settings_card(app, cr, cardX, static_cast<double>(kCardTop),
                cardW, static_cast<double>(kLayoutCardH), glassOv);
  settings_cat_label(cr, cardX + kCardPad, kCardTop + 22, "KEYBOARD LAYOUTS");

  int rowY = kLayoutBodyTop;

  auto paint_combo = [&](int ry, const char* val) {
    int cbx = rightRail - kComboW;
    settings_paint_combo_closed(app, cr, cbx, ry + 40,
                                kComboW, kComboH, glassOv, val, false, settings_scroll_px_int(app));
  };

  // Row 0: Keyboard layout
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Keyboard layout", nullptr);
  paint_combo(rowY, keyboard_layout_display_name(app.settings.keyboardLayout));

  rowY += kSliderRowH;

  // Row 1: Switch layout shortcut
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Switch layout shortcut", nullptr);
  paint_combo(rowY, switch_shortcut_label(app.settings.keyboardSwitchShortcut));

  rowY += kSliderRowH + 12;

  // Row 2: NumLock on startup
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "NumLock on startup", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), rowY, static_cast<int>(cardW),
                   static_cast<double>(rowY - 7), 28.0,
                   app.settings.keyboardNumlock, 0.0);

  // Input sources card: enabled layouts with per-row remove + add.
  const int sourcesTop = keyboard_tab::sources_card_top();
  const int sourcesH = keyboard_tab::sources_card_h(app);
  settings_card(app, cr, cardX, static_cast<double>(sourcesTop),
                cardW, static_cast<double>(sourcesH), glassOv);
  settings_cat_label(cr, cardX + kCardPad, sourcesTop + 22, "INPUT SOURCES");

  {
    int srcY = sourcesTop + 52;
    const auto& layouts = app.settings.keyboardLayouts;
    for (size_t li = 0; li < layouts.size(); ++li) {
      const bool isActive = layouts[li] == app.settings.keyboardLayout;
      const bool rowHov =
          app.pointerX >= cardX + kCardPad && app.pointerX < cardX + cardW - kCardPad &&
          app.pointerY + settings_scroll_px(app) >= srcY &&
          app.pointerY + settings_scroll_px(app) < srcY + keyboard_tab::kSourcesRowH;
      if (rowHov) {
        m3::Box hb;
        hb.setColor(Theme::TextR, Theme::TextG, Theme::TextB, 0.05f);
        hb.setRadius(6.0f);
        hb.setGeometry(static_cast<float>(cardX + kCardPad), static_cast<float>(srcY + 2),
                       static_cast<float>(cardW - 2 * kCardPad), static_cast<float>(keyboard_tab::kSourcesRowH - 4));
        hb.paint(cr);
      }
      settings_show_text(cr, cardX + kCardPad + 8, srcY + keyboard_tab::kSourcesRowH * 0.5 + 5.0,
                         keyboard_layout_display_name(layouts[li]), 13, isActive ? 700 : 400, Theme::TextR,
                         Theme::TextG, Theme::TextB, isActive ? 0.95 : 0.80);
      if (isActive) {
        settings_show_text(cr, cardX + cardW - kCardPad - 150, srcY + keyboard_tab::kSourcesRowH * 0.5 + 4.0,
                           "Active", 11, 600, Theme::AccR, Theme::AccG, Theme::AccB, 0.90);
      }
      // Reorder chevrons + remove, right-aligned in fixed zones:
      // up [E-98,E-70], down [E-64,E-36], close [E-30,E] (E = right edge).
      const double edge = cardX + cardW - kCardPad;
      const double btnCy = srcY + keyboard_tab::kSourcesRowH * 0.5;
      auto chevron = [&](double cx, bool up, bool enabled) {
        const double s = 7.0;
        cairo_save(cr);
        cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, enabled ? 0.75 : 0.20);
        cairo_set_line_width(cr, 2.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        if (up) {
          cairo_move_to(cr, cx - s, btnCy + s * 0.6);
          cairo_line_to(cr, cx, btnCy - s * 0.6);
          cairo_line_to(cr, cx + s, btnCy + s * 0.6);
        } else {
          cairo_move_to(cr, cx - s, btnCy - s * 0.6);
          cairo_line_to(cr, cx, btnCy + s * 0.6);
          cairo_line_to(cr, cx + s, btnCy - s * 0.6);
        }
        cairo_stroke(cr);
        cairo_restore(cr);
      };
      chevron(edge - 84.0, true, li > 0);
      chevron(edge - 50.0, false, li + 1 < layouts.size());
      if (layouts.size() > 1) {
        material_symbols_draw_glyph(cr, edge - 15.0, btnCy,
                                    16.0, "close", Theme::TextR, Theme::TextG, Theme::TextB, rowHov ? 0.75 : 0.40);
      }
      srcY += keyboard_tab::kSourcesRowH;
    }
    // Add-layout button row.
    const int addY = srcY + 6;
    const int addW = 170;
    const int addH = 34;
    const int addX = static_cast<int>(cardX) + kCardPad;
    const bool addHov = app.pointerX >= addX && app.pointerX < addX + addW &&
                        app.pointerY + settings_scroll_px(app) >= addY &&
                        app.pointerY + settings_scroll_px(app) < addY + addH;
    {
      m3::Box ab;
      ab.setColor(Theme::AccR, Theme::AccG, Theme::AccB, addHov ? 0.28f : 0.16f);
      ab.setRadius(addH * 0.5f);
      ab.setGeometry(static_cast<float>(addX), static_cast<float>(addY), static_cast<float>(addW),
                     static_cast<float>(addH));
      ab.paint(cr);
    }
    m3::Label addLbl;
    addLbl.setText("+ Add layout");
    addLbl.setFontSize(13.0f);
    addLbl.setFontWeight(600);
    addLbl.setColor(Theme::TextR, Theme::TextG, Theme::TextB, 0.90f);
    float addTw, addTh;
    addLbl.measureExtents(addTw, addTh);
    addLbl.paintAt(cr, static_cast<float>(addX) + (addW - addTw) * 0.5f,
                   static_cast<float>(addY) + (addH - addTh) * 0.5f);
  }

  // Behavior card.
  const int behaviorTop = keyboard_tab::behavior_card_top(app);
  settings_card(app, cr, cardX, static_cast<double>(behaviorTop),
                cardW, static_cast<double>(kBehaviorCardH), glassOv);
  settings_cat_label(cr, cardX + kCardPad, behaviorTop + 22, "KEY BEHAVIOR");

  rowY = behaviorTop + 52;

  // Row 0: Caps Lock behavior
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Caps Lock behavior", nullptr);
  paint_combo(rowY, caps_lock_label(app.settings.keyboardCapsLockBehavior));

  rowY += kSliderRowH;

  // Row 1: Compose key
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Compose key", nullptr);
  paint_combo(rowY, compose_key_label(app.settings.keyboardComposeKey));

  rowY += kSliderRowH + 12;

  // Row 2: Middle-click paste
  settings_label(cr, cardX + kCardPad, static_cast<double>(rowY + 14),
                 "Middle-click paste", nullptr);
  settings_toggle(app, cr, static_cast<int>(cardX), rowY, static_cast<int>(cardW),
                   static_cast<double>(rowY - 7), 28.0,
                   app.settings.keyboardMiddleClickPaste, 0.0);

  // Repeat rate card.
  const int repeatTop = keyboard_tab::repeat_card_top(app);
  settings_card(app, cr, cardX, static_cast<double>(repeatTop),
                cardW, static_cast<double>(kRepeatCardH), glassOv);
  settings_cat_label(cr, cardX + kCardPad, repeatTop + 22, "KEY REPEAT");

  rowY = repeatTop + 52;

  auto paint_slider_row = [&](const char* title, const char* valFmt, int value,
                              int vmin, int vmax, int which) {
    const int titleY = rowY + 14;
    settings_label(cr, cardX + kCardPad, static_cast<double>(titleY), title, nullptr);
    int trX = 0, trW = 0;
    keyboard_tab::repeat_slider_track_geom(contentX, contentW, &trX, &trW);
    const int trY = titleY + 26;
    paint_slider_pill(app, cr, titleY, trX, trY, trW);
    char buf[32];
    std::snprintf(buf, sizeof(buf), valFmt, value);
    settings_slider(app, cr, trX, trY, trW, value, vmin, vmax, 0.0, buf, false, (app.keyboardSliderDrag == which) ? app.settingsSliderDragNormT : -1.0);
    // Persistent value, right-aligned in the reserved zone (the m3 popup
    // only appears while dragging).
    {
      m3::Label vl;
      vl.setText(buf);
      vl.setFontSize(12.0f);
      vl.setFontWeight(600);
      vl.setColor(static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                  static_cast<float>(Theme::TextB), 0.70f);
      float lw = 0, lh = 0;
      vl.measureExtents(lw, lh);
      const int rightRail = keyboard_tab::repeat_slider_value_x(contentX, contentW);
      vl.paintAt(cr, static_cast<float>(rightRail) - lw, static_cast<float>(trY) + 8.0f - lh * 0.5f);
    }
  };

  // Row 0: Repeat rate
  paint_slider_row("Repeat rate", "%d cps", app.settings.keyboardRepeatRate, 15, 50, 0);

  rowY += kSliderRowH + 8;

  // Row 1: Repeat delay
  paint_slider_row("Repeat delay", "%d ms", app.settings.keyboardRepeatDelay, 150, 1000, 1);

  // Language card: system locale + login-screen keymap.
  const int langTop = keyboard_tab::language_card_top(app);
  const int langH = keyboard_tab::language_card_h();
  settings_card(app, cr, cardX, static_cast<double>(langTop),
                cardW, static_cast<double>(langH), glassOv);
  settings_cat_label(cr, cardX + kCardPad, langTop + 22, "LANGUAGE");
  {
    const double ly = app.pointerY + settings_scroll_px(app);
    const std::string curLocale = keyboard_system_locale();
    const std::string pending = app.keyboardPendingLocale;
    int y = langTop + 52;
    settings_show_text(cr, cardX + kCardPad, y + 14, "System language", 14.f, 400,
                       static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                       static_cast<float>(Theme::TextB), 0.88f);
    std::string curLine = curLocale.empty() ? "Current: unknown" : ("Current: " + curLocale);
    settings_show_text(cr, cardX + kCardPad, y + 30, curLine.c_str(), 11.f, 400,
                       static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                       static_cast<float>(Theme::TextB), 0.50f);
    y += 44;
    const auto locales = keyboard_system_locales();
    if (locales.empty()) {
      settings_show_text(cr, cardX + kCardPad, y + 20, "No locales found.", 13.f, 400,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.55f);
      y += keyboard_tab::kLocaleRowH;
    }
    for (const auto& loc : locales) {
      const bool isCur = loc == curLocale;
      const bool isPend = !pending.empty() && loc == pending;
      const bool rowHov = app.pointerX >= cardX + kCardPad &&
                          app.pointerX < cardX + cardW - kCardPad && ly >= y &&
                          ly < y + keyboard_tab::kLocaleRowH;
      if (isPend || rowHov) {
        m3::Box hb;
        hb.setColor(Theme::TextR, Theme::TextG, Theme::TextB, isPend ? 0.10f : 0.05f);
        hb.setRadius(6.0f);
        hb.setGeometry(static_cast<float>(cardX + kCardPad), static_cast<float>(y + 2),
                       static_cast<float>(cardW - 2 * kCardPad),
                       static_cast<float>(keyboard_tab::kLocaleRowH - 4));
        hb.paint(cr);
      }
      settings_show_text(cr, cardX + kCardPad + 8, y + keyboard_tab::kLocaleRowH * 0.5 + 5.0,
                         loc.c_str(), 13, (isCur || isPend) ? 700 : 400, Theme::TextR,
                         Theme::TextG, Theme::TextB, (isCur || isPend) ? 0.95 : 0.80);
      if (isCur) {
        settings_show_text(cr, cardX + cardW - kCardPad - 60, y + keyboard_tab::kLocaleRowH * 0.5 + 4.0,
                           "Active", 11, 600, Theme::AccR, Theme::AccG, Theme::AccB, 0.90);
      }
      y += keyboard_tab::kLocaleRowH;
    }
    y += 12;
    // Apply-locale button.
    {
      const int bw = 200, bh = 34;
      const int bx = static_cast<int>(cardX) + kCardPad;
      const bool hov = app.pointerX >= bx && app.pointerX < bx + bw && ly >= y && ly < y + bh;
      float aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB;
      settings_resolve_colors(app, aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB);
      m3::Box box;
      box.setColor(aR, aG, aB, hov ? 0.35f : 0.25f);
      box.setRadius(bh * 0.5f);
      box.setGeometry(static_cast<float>(bx), static_cast<float>(y),
                      static_cast<float>(bw), static_cast<float>(bh));
      box.paint(cr);
      m3::Label lbl;
      lbl.setText(pending.empty() ? "Apply Language" : ("Apply '" + pending + "'").c_str());
      lbl.setFontSize(12.0f);
      lbl.setFontWeight(600);
      float lw = 0, lh = 0;
      lbl.measureExtents(lw, lh);
      lbl.setColor(1, 1, 1, hov ? 0.95f : 0.85f);
      lbl.paintAt(cr, bx + (bw - lw) * 0.5f, y + (bh - lh) * 0.5f);
    }
    y += 34 + 10;
    // Login-screen keymap line + button.
    {
      const std::string x11 = keyboard_x11_keymap_line();
      const std::string line = x11.empty() ? "Login screen keyboard: unknown"
                                           : ("Login screen keyboard: " + x11);
      settings_show_text(cr, cardX + kCardPad, y + 12, line.c_str(), 12.f, 400,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.60f);
      const int bw = 250, bh = 34;
      const int bx = static_cast<int>(cardX) + kCardPad;
      const int by = y + 20;
      const bool hov = app.pointerX >= bx && app.pointerX < bx + bw && ly >= by && ly < by + bh;
      float aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB;
      settings_resolve_colors(app, aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB);
      m3::Box box;
      box.setColor(Theme::TextR, Theme::TextG, Theme::TextB, hov ? 0.14f : 0.08f);
      box.setRadius(bh * 0.5f);
      box.setGeometry(static_cast<float>(bx), static_cast<float>(by),
                      static_cast<float>(bw), static_cast<float>(bh));
      box.paint(cr);
      m3::Label lbl;
      lbl.setText("Use These Layouts at Login");
      lbl.setFontSize(12.0f);
      lbl.setFontWeight(600);
      float lw = 0, lh = 0;
      lbl.measureExtents(lw, lh);
      lbl.setColor(Theme::TextR, Theme::TextG, Theme::TextB, hov ? 0.95f : 0.85f);
      lbl.paintAt(cr, bx + (bw - lw) * 0.5f, by + (bh - lh) * 0.5f);
      y = by + bh;
    }
    y += 8;
    if (!app.keyboardStatusMsg.empty()) {
      float aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB;
      settings_resolve_colors(app, aR, aG, aB, tR, tG, tB, sR, sG, sB, oR, oG, oB);
      (void)tR; (void)tG; (void)tB; (void)sR; (void)sG; (void)sB; (void)oR; (void)oG; (void)oB;
      if (app.keyboardStatusErr)
        settings_show_text(cr, cardX + kCardPad, y + 12, app.keyboardStatusMsg.c_str(), 12.f, 400,
                           1.0f, 0.45f, 0.40f, 0.9f);
      else
        settings_show_text(cr, cardX + kCardPad, y + 12, app.keyboardStatusMsg.c_str(), 12.f, 400,
                           aR, aG, aB, 0.85f);
    }
  }
}

bool settings_keyboard_consume_pointer_down(App& app, int contentX, int contentW) {
   
  const int cardXi = contentX + 8;
  const int cardWi = contentW - 16;
  const int rightRail = cardXi + cardWi - kSpacingXL;
  const double py = app.pointerY + settings_scroll_px(app);

  if (app.settingsKeyboardDdKind >= 0) {
    // Kinds 0-3 popups hang off the right-rail combos; kind 4 hangs off the Add button.
    int cbx = rightRail - kComboW;
    int cbw = kComboW;
    int cbh = kComboH;
    if (app.settingsKeyboardDdKind == 4) {
      cbx = cardXi + kCardPad;
    }
    const int behaviorTop = keyboard_tab::behavior_card_top(app);
    const std::vector<std::string> addChoices =
        (app.settingsKeyboardDdKind == 4) ? keyboard_available_layouts(app) : std::vector<std::string>();
    const int rowH = kSettingsDdRowH;
    const int nItems = [&]() -> int {
      switch (app.settingsKeyboardDdKind) {
        case 0: return static_cast<int>(app.settings.keyboardLayouts.size());
        case 1: return kSwitchShortcutCount;
        case 2: return kCapsLockCount;
        case 3: return kComposeKeyCount;
        case 4: return static_cast<int>(addChoices.size());
        default: return 0;
      }
    }();
    auto add_button_y = [&]() {
      const int n = static_cast<int>(app.settings.keyboardLayouts.size());
      return keyboard_tab::sources_card_top() + 52 + n * keyboard_tab::kSourcesRowH + 6;
    };
    if (nItems > 0) {
      int ddY = 0;
      switch (app.settingsKeyboardDdKind) {
        case 0: ddY = kLayoutBodyTop + 0 * kSliderRowH + 40 + cbh + 2; break;
        case 1: ddY = kLayoutBodyTop + 1 * kSliderRowH + 40 + cbh + 2; break;
        case 2: ddY = behaviorTop + 52 + 0 * kSliderRowH + 40 + cbh + 2; break;
        case 3: ddY = behaviorTop + 52 + 1 * kSliderRowH + 40 + cbh + 2; break;
        case 4: ddY = add_button_y() + 34 + 2; break;
      }
      const int ddH = nItems * rowH;
      if (point_in_rect(app.pointerX, py, cbx, ddY, cbw, ddH)) {
        const int relY = static_cast<int>(py) - ddY;
        const int idx = relY / rowH;
        if (idx >= 0 && idx < nItems) {
          int switchNowIdx = -1;
          switch (app.settingsKeyboardDdKind) {
            case 0:
              if (static_cast<size_t>(idx) < app.settings.keyboardLayouts.size()) {
                app.settings.keyboardLayout = app.settings.keyboardLayouts[idx];
                app.settingsKeyboardDdKind = -1;
                app.settingsKeyboardDdHoverRow = -1;
                save_settings(app.settings);
                keyboard_push_to_compositor(app);
                keyboard_activate_now(app, idx);
                draw(app);
                return true;
              }
              break;
            case 1: app.settings.keyboardSwitchShortcut = idx; break;
            case 2: app.settings.keyboardCapsLockBehavior = idx; break;
            case 3: app.settings.keyboardComposeKey = idx; break;
            case 4:
              app.settings.keyboardLayouts.push_back(addChoices[static_cast<size_t>(idx)]);
              app.settings.keyboardLayout = app.settings.keyboardLayouts.back();
              switchNowIdx = static_cast<int>(app.settings.keyboardLayouts.size()) - 1;
              break;
          }
          app.settingsKeyboardDdKind = -1;
          app.settingsKeyboardDdHoverRow = -1;
          debug_log("keybinds", "keyboard settings applied: layout='%s' switchShortcut=%d caps=%d compose=%d",
                    app.settings.keyboardLayout.c_str(), app.settings.keyboardSwitchShortcut,
                    app.settings.keyboardCapsLockBehavior, app.settings.keyboardComposeKey);
          save_settings(app.settings);
          keyboard_push_to_compositor(app);
          if (switchNowIdx >= 0) keyboard_activate_now(app, switchNowIdx);
          draw(app);
          return true;
        }
      }
    }
    // Click outside closes
    if (nItems > 0) {
      int ddY = 0;
      switch (app.settingsKeyboardDdKind) {
        case 0: ddY = kLayoutBodyTop + 0 * kSliderRowH + 40; break;
        case 1: ddY = kLayoutBodyTop + 1 * kSliderRowH + 40; break;
        case 2: ddY = behaviorTop + 52 + 0 * kSliderRowH + 40; break;
        case 3: ddY = behaviorTop + 52 + 1 * kSliderRowH + 40; break;
        case 4: ddY = add_button_y() + 34; break;
      }
      if (point_in_rect(app.pointerX, py, cbx, ddY - 4, cbw, nItems * rowH + cbh + 8)) {
        app.settingsKeyboardDdKind = -1;
        app.settingsKeyboardDdHoverRow = -1;
        draw(app);
        return true;
      }
    }
    app.settingsKeyboardDdKind = -1;
    app.settingsKeyboardDdHoverRow = -1;
    draw(app);
    return true;
  }

  // Toggle hits (toggle painted at rightRail-60, rowY-6, 52x26).
  {
    constexpr int swW = 52;
    constexpr int swH = 26;
    const int swX = rightRail - swW - 8;
    int swY = 0;

    swY = (kLayoutBodyTop + 2 * kSliderRowH) - 6;
    if (point_in_rect(app.pointerX, py, swX, swY, swW, swH)) {
      app.settings.keyboardNumlock = !app.settings.keyboardNumlock;
      save_settings(app.settings);
      keyboard_push_to_compositor(app);
      draw(app);
      return true;
    }

    swY = (keyboard_tab::behavior_card_top(app) + 52 + 2 * kSliderRowH) - 6;
    if (point_in_rect(app.pointerX, py, swX, swY, swW, swH)) {
      app.settings.keyboardMiddleClickPaste = !app.settings.keyboardMiddleClickPaste;
      save_settings(app.settings);
      keyboard_push_to_compositor(app);
      draw(app);
      return true;
    }
  }

  // Input sources rows: chevrons reorder, x removes, click activates now.
  {
    const int n = static_cast<int>(app.settings.keyboardLayouts.size());
    const int edge = cardXi + cardWi - kCardPad;
    int srcY = keyboard_tab::sources_card_top() + 52;
    for (int li = 0; li < n; ++li) {
      const int rowTop = srcY + li * keyboard_tab::kSourcesRowH;
      if (li > 0 && point_in_rect(app.pointerX, py, edge - 98, rowTop, 28,
                                  keyboard_tab::kSourcesRowH)) {
        std::swap(app.settings.keyboardLayouts[static_cast<size_t>(li)],
                  app.settings.keyboardLayouts[static_cast<size_t>(li - 1)]);
        save_settings(app.settings);
        keyboard_push_to_compositor(app);
        draw(app);
        return true;
      }
      if (li + 1 < n && point_in_rect(app.pointerX, py, edge - 64, rowTop, 28,
                                      keyboard_tab::kSourcesRowH)) {
        std::swap(app.settings.keyboardLayouts[static_cast<size_t>(li)],
                  app.settings.keyboardLayouts[static_cast<size_t>(li + 1)]);
        save_settings(app.settings);
        keyboard_push_to_compositor(app);
        draw(app);
        return true;
      }
      const int rmX = edge - 30;
      if (n > 1 && point_in_rect(app.pointerX, py, rmX, rowTop, 30, keyboard_tab::kSourcesRowH)) {
        const bool wasActive = app.settings.keyboardLayouts[static_cast<size_t>(li)] == app.settings.keyboardLayout;
        app.settings.keyboardLayouts.erase(app.settings.keyboardLayouts.begin() + li);
        if (wasActive) app.settings.keyboardLayout = app.settings.keyboardLayouts.front();
        save_settings(app.settings);
        keyboard_push_to_compositor(app);
        draw(app);
        return true;
      }
      if (point_in_rect(app.pointerX, py, cardXi + kCardPad, rowTop,
                        cardWi - 2 * kCardPad, keyboard_tab::kSourcesRowH)) {
        app.settings.keyboardLayout = app.settings.keyboardLayouts[static_cast<size_t>(li)];
        save_settings(app.settings);
        keyboard_push_to_compositor(app);
        keyboard_activate_now(app, li);
        draw(app);
        return true;
      }
    }
    // Add-layout button.
    const int addY = srcY + n * keyboard_tab::kSourcesRowH + 6;
    if (point_in_rect(app.pointerX, py, cardXi + kCardPad, addY, 170, 34)) {
      if (keyboard_available_layouts(app).empty()) {
        draw(app);
        return true;
      }
      app.settingsKeyboardDdKind = 4;
      app.settingsKeyboardDdHoverRow = -1;
      draw(app);
      return true;
    }
  }

  // Combo box hits.
  {
    const int cbx = rightRail - kComboW;
    const int cbw = kComboW;
    const int cbh = kComboH;
    const int behaviorTop = keyboard_tab::behavior_card_top(app);

    auto combo_hit = [&](int cby, int ddKind) -> bool {
      if (point_in_rect(app.pointerX, py, cbx, cby, cbw, cbh)) {
        app.settingsKeyboardDdKind = ddKind;
        app.settingsKeyboardDdHoverRow = -1;
        draw(app);
        return true;
      }
      return false;
    };

    if (combo_hit(kLayoutBodyTop + 0 * kSliderRowH + 40, 0)) return true;
    if (combo_hit(kLayoutBodyTop + 1 * kSliderRowH + 40, 1)) return true;
    if (combo_hit(behaviorTop + 52 + 0 * kSliderRowH + 40, 2)) return true;
    if (combo_hit(behaviorTop + 52 + 1 * kSliderRowH + 40, 3)) return true;
  }

  // Slider hits (same track geometry as paint and pointer-motion).
  {
    const int repeatTop = keyboard_tab::repeat_card_top(app);
    auto slider_hit = [&](int sliderRowY, int which) -> bool {
      const int titleY = sliderRowY + 14;
      int trX = 0, trW = 0;
      keyboard_tab::repeat_slider_track_geom(contentX, contentW, &trX, &trW);
      const int trY = titleY + 26;
      if (point_in_rect(app.pointerX, py, trX - 6, trY, trW + 12, 28)) {
        app.keyboardSliderDrag = which;
        app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
        if (which == 0) {
          app.settings.keyboardRepeatRate = slider_value_from_x(app.pointerX, trX, trW, 15, 50);
        } else {
          app.settings.keyboardRepeatDelay = slider_value_from_x(app.pointerX, trX, trW, 150, 1000);
        }
        draw(app);
        return true;
      }
      return false;
    };

    if (slider_hit(repeatTop + 52, 0)) return true;
    if (slider_hit(repeatTop + 52 + kSliderRowH + 8, 1)) return true;
  }

  // Language card: stage a locale per row, Apply commits, X11 button applies
  // the enabled layouts to the login screen. Geometry mirrors paint.
  {
    const int langTop = keyboard_tab::language_card_top(app);
    const auto locales = keyboard_system_locales();
    int y = langTop + 52 + 44;
    const int rowW = cardWi - 2 * kCardPad;
    for (const auto& loc : locales) {
      if (point_in_rect(app.pointerX, py, cardXi + kCardPad, y, rowW, keyboard_tab::kLocaleRowH)) {
        if (app.keyboardPendingLocale != loc) {
          app.keyboardPendingLocale = loc;
          app.keyboardStatusMsg = "Select Apply to switch the system language (next login).";
          app.keyboardStatusErr = false;
        }
        draw(app);
        return true;
      }
      y += keyboard_tab::kLocaleRowH;
    }
    y += 12;
    if (point_in_rect(app.pointerX, py, cardXi + kCardPad, y, 200, 34)) {
      const std::string target =
          app.keyboardPendingLocale.empty() ? keyboard_system_locale() : app.keyboardPendingLocale;
      if (target.empty()) {
        app.keyboardStatusMsg = "No system language selected.";
        app.keyboardStatusErr = true;
      } else {
        std::string msg;
        const bool ok = keyboard_apply_locale(target, msg);
        app.keyboardStatusMsg = msg;
        app.keyboardStatusErr = !ok;
        if (ok) app.keyboardPendingLocale.clear();
      }
      draw(app);
      return true;
    }
    y += 34 + 10;
    if (point_in_rect(app.pointerX, py, cardXi + kCardPad, y + 18, 250, 34)) {
      std::string msg;
      const bool ok = keyboard_apply_x11_keymap(app, msg);
      app.keyboardStatusMsg = msg;
      app.keyboardStatusErr = !ok;
      draw(app);
      return true;
    }
  }

  return false;
}
