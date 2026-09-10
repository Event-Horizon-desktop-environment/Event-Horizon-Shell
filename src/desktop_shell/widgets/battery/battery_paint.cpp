#include "desktop_shell/widgets/battery/battery_paint.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/widgets/shared/widget_settings.hpp"
#include "desktop_shell/widgets/shared/measure_scratch.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/StandardInterfaces.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cairo.h>
#include <pango/pangocairo.h>

namespace eh::widgets {
namespace {

// UPower types.

enum class BatteryState : uint8_t {
  Unknown = 0,
  Charging = 1,
  Discharging = 2,
  Empty = 3,
  FullyCharged = 4,
  PendingCharge = 5,
  PendingDischarge = 6,
};

struct UPowerState {
  double percentage = 0.0;
  BatteryState state = BatteryState::Unknown;
  bool isPresent = false;
  bool onBattery = false;
};

// UPower DBus constants.

const sdbus::ServiceName kUpowerBusName{"org.freedesktop.UPower"};
const sdbus::ObjectPath kUpowerObjectPath{"/org/freedesktop/UPower"};
constexpr auto kUpowerInterface = "org.freedesktop.UPower";
constexpr auto kDeviceInterface = "org.freedesktop.UPower.Device";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";

constexpr uint32_t kDeviceTypeBattery = 2;
constexpr uint32_t kStateCharging = 1;
constexpr uint32_t kStateDischarging = 2;
constexpr uint32_t kStateEmpty = 3;
constexpr uint32_t kStateFullyCharged = 4;
constexpr uint32_t kStatePendingCharge = 5;
constexpr uint32_t kStatePendingDischarge = 6;

// Helpers.

template <typename T>
T getPropertyOr(sdbus::IProxy& proxy, const std::string& iface, const std::string& prop, T fallback) {
   
  try {
    sdbus::Variant v = proxy.getProperty(prop).onInterface(iface);
    return v.get<T>();
  } catch (const sdbus::Error&) {
    return fallback;
  }
}

BatteryState decodeState(uint32_t raw) {
   
  switch (raw) {
    case kStateCharging: return BatteryState::Charging;
    case kStateDischarging: return BatteryState::Discharging;
    case kStateEmpty: return BatteryState::Empty;
    case kStateFullyCharged: return BatteryState::FullyCharged;
    case kStatePendingCharge: return BatteryState::PendingCharge;
    case kStatePendingDischarge: return BatteryState::PendingDischarge;
    default: return BatteryState::Unknown;
  }
}

UPowerState readDeviceState(sdbus::IProxy& proxy) {
   
  UPowerState s;
  s.isPresent = getPropertyOr<bool>(proxy, kDeviceInterface, "IsPresent", false);
  s.percentage = getPropertyOr<double>(proxy, kDeviceInterface, "Percentage", 0.0);
  uint32_t rawState = getPropertyOr<uint32_t>(proxy, kDeviceInterface, "State", 0);
  s.state = decodeState(rawState);
  return s;
}

// Internal battery service (static).

std::mutex g_batteryMutex;
UPowerState g_cachedState;

// Background polling thread state
static std::thread g_pollThread;
static int g_wakeFd = -1;
static std::atomic<bool> g_stop{false};
static std::atomic<int> g_refcount{0};

bool isBatteryType(uint32_t devType) {
   
  return devType == kDeviceTypeBattery;
}

UPowerState readDefaultState(sdbus::IProxy& proxy, sdbus::IConnection& bus) {
   
  UPowerState result;

  result.onBattery = getPropertyOr<bool>(proxy, kUpowerInterface, "OnBattery", false);

  std::vector<sdbus::ObjectPath> paths;
  try {
    proxy.callMethod("EnumerateDevices").onInterface(kUpowerInterface).storeResultsTo(paths);
  } catch (const sdbus::Error&) {
    return result;
  }

  for (const auto& path : paths) {
    try {
      auto devProxy = sdbus::createProxy(bus, kUpowerBusName, path);
      uint32_t devType = getPropertyOr<uint32_t>(*devProxy, kDeviceInterface, "Type", 0);
      if (!isBatteryType(devType)) continue;
      bool powerSupply = getPropertyOr<bool>(*devProxy, kDeviceInterface, "PowerSupply", false);
      bool isPresent = getPropertyOr<bool>(*devProxy, kDeviceInterface, "IsPresent", false);
      if (powerSupply && isPresent) {
        UPowerState devState = readDeviceState(*devProxy);
        if (devState.isPresent) {
          result.percentage = devState.percentage;
          result.state = devState.state;
          result.isPresent = true;
          result.onBattery = getPropertyOr<bool>(proxy, kUpowerInterface, "OnBattery", false);
          return result;
        }
      }
    } catch (const sdbus::Error&) {
      continue;
    }
  }

  return result;
}

static bool debug_state(UPowerState& out) {
   
  const char* env = std::getenv("EH_BATTERY_DEBUG");
  if (!env || env[0] == '\0') return false;
  out.isPresent = true;
  out.onBattery = true;
  out.state = BatteryState::Discharging;
  out.percentage = 75.0;
  const std::string_view sv(env);
  if (sv == "charging") {
    static const auto start = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
    int pct = 1 + static_cast<int>(elapsed / 14);
    if (pct >= 100) {
      out.state = BatteryState::FullyCharged;
      out.percentage = 100.0;
    } else {
      out.state = BatteryState::Charging;
      out.percentage = static_cast<double>(pct);
    }
  }
  else if (sv == "discharging") { out.state = BatteryState::Discharging; out.percentage = 65.0; }
  else if (sv == "low") { out.state = BatteryState::Discharging; out.percentage = 8.0; }
  else if (sv == "full") { out.state = BatteryState::FullyCharged; out.percentage = 100.0; }
  else if (sv == "absent") { out.isPresent = false; }
  return true;
}

static void pollThreadMain() {
   
  std::unique_ptr<sdbus::IConnection> bus;
  std::unique_ptr<sdbus::IProxy> proxy;
  try {
    bus = sdbus::createSystemBusConnection();
    proxy = sdbus::createProxy(*bus, kUpowerBusName, kUpowerObjectPath);
  } catch (const sdbus::Error&) {
    return;
  }

  // Signal handlers — wake the poll loop immediately on any UPower change
  auto onSignal = []() {
    if (g_wakeFd >= 0) {
      const std::uint64_t one = 1;
      (void)::write(g_wakeFd, &one, sizeof(one));
    }
  };
  try {
    proxy->uponSignal("PropertiesChanged")
      .onInterface(kPropertiesInterface)
      .call([onSignal](const std::string& iface,
                       const std::map<std::string, sdbus::Variant>&,
                       const std::vector<std::string>&) {
        if (iface == kUpowerInterface) onSignal();
      });
    proxy->uponSignal("DeviceAdded").onInterface(kUpowerInterface)
      .call([onSignal](const sdbus::ObjectPath&) { onSignal(); });
    proxy->uponSignal("DeviceRemoved").onInterface(kUpowerInterface)
      .call([onSignal](const sdbus::ObjectPath&) { onSignal(); });
  } catch (const sdbus::Error&) {
  }
  bus->enterEventLoopAsync();

  UPowerState state;
  if (!debug_state(state)) {
    state = readDefaultState(*proxy, *bus);
  }
  {
    std::lock_guard<std::mutex> lock(g_batteryMutex);
    g_cachedState = state;
  }

  constexpr int kPollIntervalSec = 5;
  while (!g_stop.load(std::memory_order_relaxed)) {
    pollfd pfd{};
    pfd.fd = g_wakeFd;
    pfd.events = POLLIN;
    const int ret = ::poll(&pfd, 1, kPollIntervalSec * 1000);
    if (ret < 0 && errno == EINTR) continue;
    if (g_stop.load(std::memory_order_relaxed)) break;

    // Drain the wake eventfd
    if (ret > 0 && (pfd.revents & POLLIN)) {
      std::uint64_t v = 0;
      (void)::read(g_wakeFd, &v, sizeof(v));
    }

    UPowerState newState;
    if (!debug_state(newState)) {
      newState = readDefaultState(*proxy, *bus);
    }
    {
      std::lock_guard<std::mutex> lock(g_batteryMutex);
      g_cachedState = newState;
    }
  }
}

} // namespace

// Public API.

void battery_widget_init() {
   
  if (g_refcount.fetch_add(1, std::memory_order_relaxed) > 0) return;
  g_wakeFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (g_wakeFd < 0) return;
  g_stop.store(false, std::memory_order_relaxed);
  g_pollThread = std::thread(pollThreadMain);
}

void battery_widget_poll() {
   
  if (g_wakeFd < 0) return;
  {
    std::lock_guard<std::mutex> lock(g_batteryMutex);
    if (debug_state(g_cachedState)) return;
  }
  const std::uint64_t one = 1;
  (void)::write(g_wakeFd, &one, sizeof(one));
}

void battery_widget_shutdown() {
   
  if (g_refcount.fetch_sub(1, std::memory_order_relaxed) > 1) return;
  g_stop.store(true, std::memory_order_relaxed);
  if (g_wakeFd >= 0) {
    const std::uint64_t one = 1;
    (void)::write(g_wakeFd, &one, sizeof(one));
  }
  if (g_pollThread.joinable()) g_pollThread.join();
  if (g_wakeFd >= 0) {
    ::close(g_wakeFd);
    g_wakeFd = -1;
  }
}

// Widget helpers.

namespace {

bool parse_bool_setting(const std::string& v, bool fallback) {
   
  if (v.empty()) return fallback;
  if (v == "1" || v == "true" || v == "True" || v == "yes" || v == "Yes") return true;
  if (v == "0" || v == "false" || v == "False" || v == "no" || v == "No") return false;
  return fallback;
}

const char* battery_glyph_for(double pct, BatteryState state) {
   
  if (state == BatteryState::Charging || state == BatteryState::FullyCharged || state == BatteryState::PendingCharge) {
    return "battery_charging_full";
  }
  if (state == BatteryState::Unknown || !std::isfinite(pct)) {
    return "battery_unknown";
  }
  if (pct >= 95.0) return "battery_full";
  if (pct >= 80.0) return "battery_6_bar";
  if (pct >= 60.0) return "battery_5_bar";
  if (pct >= 40.0) return "battery_4_bar";
  if (pct >= 20.0) return "battery_3_bar";
  if (pct >= 10.0) return "battery_2_bar";
  if (pct >= 5.0)  return "battery_1_bar";
  return "battery_0_bar";
}

bool is_charging(BatteryState state) {
   
  return state == BatteryState::Charging
      || state == BatteryState::FullyCharged
      || state == BatteryState::PendingCharge;
}

PangoLayout* make_layout(cairo_t* cr, const char* font_desc_str) {
   
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_from_string(font_desc_str);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  return layout;
}

struct BatteryTextCache {
  std::string text;
  std::string glyph;
  int textW = 0;
  int textH = 0;
  int glyphW = 0;
  double lastFontPx = 0;
  PangoLayout* layout = nullptr;
  ~BatteryTextCache() { if (layout) g_object_unref(layout); }
  BatteryTextCache() = default;
  BatteryTextCache(BatteryTextCache&& o) noexcept
      : text(std::move(o.text)), glyph(std::move(o.glyph)),
        textW(o.textW), textH(o.textH), glyphW(o.glyphW),
        lastFontPx(o.lastFontPx), layout(o.layout) {
    o.layout = nullptr;
  }
  BatteryTextCache& operator=(BatteryTextCache&& o) noexcept {
    if (layout) g_object_unref(layout);
    text = std::move(o.text);
    glyph = std::move(o.glyph);
    textW = o.textW;
    textH = o.textH;
    glyphW = o.glyphW;
    lastFontPx = o.lastFontPx;
    layout = o.layout;
    o.layout = nullptr;
    return *this;
  }
  BatteryTextCache(const BatteryTextCache&) = delete;
  BatteryTextCache& operator=(const BatteryTextCache&) = delete;
};
static std::unordered_map<std::string, BatteryTextCache> g_batteryTextCache;

}

// Widget list check.

bool widget_list_contains_battery(const eh::config::ShellConfig& sc, const std::vector<std::string>& widgets) {
   
  (void)sc;
  for (const auto& id : widgets) {
    if (eh::config::widget_implementation_type(id) == "battery") return true;
  }
  return false;
}

// Width measurement.

double dock_battery_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc,
                                std::string_view instance_id, double icon_ref_px, double /*bar_height*/) {
   
  const double us = dock_ui_scale(sc.dock);
  const bool showPercent = parse_bool_setting(widget_setting(sc, instance_id, "show_percent"), true);

  if (!showPercent) {
    return icon_ref_px + 4.0 * us;
  }

  if (measure_cr == nullptr) {
    measure_cr = get_measure_cr();
  }

  std::lock_guard<std::mutex> lock(g_batteryMutex);
  int pct = g_cachedState.isPresent ? static_cast<int>(std::round(g_cachedState.percentage)) : 0;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%d%%", pct);
  const char* glyph = battery_glyph_for(g_cachedState.percentage, g_cachedState.state);

  int tw = 0, th = 0, gw = 0;
  const std::string batkid(instance_id);
  auto batit = g_batteryTextCache.find(batkid);
  if (batit != g_batteryTextCache.end() && batit->second.text == buf && batit->second.glyph == glyph) {
    tw = batit->second.textW;
    th = batit->second.textH;
    gw = batit->second.glyphW;
  } else {
    const double fontPx = std::clamp(icon_ref_px * 0.38, 11.0 * us, 15.0 * us);
    std::string fd = "Inter " + std::to_string(static_cast<int>(fontPx));
    PangoLayout* layout = make_layout(measure_cr, fd.c_str());
    pango_layout_set_text(layout, buf, -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    g_object_unref(layout);

    const double glyphPx = icon_ref_px * 0.52;
    eh::shell::ensure_material_symbols_font_registered();
    PangoLayout* gl = pango_cairo_create_layout(measure_cr);
    PangoFontDescription* gd = pango_font_description_new();
    pango_font_description_set_family(gd, "Material Symbols Rounded");
    pango_font_description_set_weight(gd, PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(gd, static_cast<int>(glyphPx * PANGO_SCALE));
    pango_layout_set_font_description(gl, gd);
    pango_layout_set_text(gl, glyph, -1);
    pango_layout_get_pixel_size(gl, &gw, nullptr);
    pango_font_description_free(gd);
    g_object_unref(gl);

    BatteryTextCache& batc = g_batteryTextCache[batkid];
    batc.text = buf;
    batc.glyph = glyph;
    batc.textW = tw;
    batc.textH = th;
    batc.glyphW = gw;
  }

  const double padX = 6.0 * us;
  const double gap = 6.0 * us;
  const double textW = static_cast<double>(tw);
  const double minW = std::max(icon_ref_px * 1.25, 40.0 * us);
  const double total = static_cast<double>(gw) + gap + textW + padX * 2.0;
  return std::max(minW, total);
}

// Paint.

void paint_battery_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                          double x, double y, double slot_w, double slot_h, double icon_ref_px,
                          bool hovered, bool pressed) {
  (void)hovered; (void)pressed;
  const double us = dock_ui_scale(sc.dock);
  const bool showPercent = parse_bool_setting(widget_setting(sc, instance_id, "show_percent"), true);
  int warningThreshold = 0;
  {
    const std::string ws = widget_setting(sc, instance_id, "warning_threshold");
    if (!ws.empty()) warningThreshold = std::atoi(ws.c_str());
  }
  if (warningThreshold <= 0) warningThreshold = 10;

  std::lock_guard<std::mutex> lock(g_batteryMutex);

  cairo_save(cr);
  slot_pill_style::paint_pill(cr, x, y, slot_w, slot_h);

  if (!g_cachedState.isPresent) {
    cairo_restore(cr);
    return;
  }

  const int pct = static_cast<int>(std::round(g_cachedState.percentage));
  const bool charging = is_charging(g_cachedState.state);
  const bool warning = warningThreshold > 0 && pct <= warningThreshold && !charging;

  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  double fgR = mc.textR, fgG = mc.textG, fgB = mc.textB;
  if (warning) {
    fgR = 1.0; fgG = 0.4; fgB = 0.4;
  } else if (charging) {
    fgR = mc.accentR; fgG = mc.accentG; fgB = mc.accentB;
  }

  const double glyphPx = icon_ref_px * 0.52;
  const double gx = x + slot_w * 0.5;
  const double gy = y + slot_h * 0.5;

  if (showPercent) {
    const double fontPx = std::clamp(icon_ref_px * 0.38, 11.0 * us, 15.0 * us);
    std::string fd = "Inter " + std::to_string(static_cast<int>(fontPx));

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d%%", pct);
    const char* glyph = battery_glyph_for(g_cachedState.percentage, g_cachedState.state);

    const std::string pbatkid(instance_id);
    auto pbatit = g_batteryTextCache.find(pbatkid);
    BatteryTextCache& btc = g_batteryTextCache[pbatkid];

    if (!btc.layout || btc.lastFontPx != fontPx) {
      if (btc.layout) g_object_unref(btc.layout);
      btc.layout = make_layout(cr, fd.c_str());
      btc.lastFontPx = fontPx;
    } else {
      pango_cairo_update_layout(cr, btc.layout);
    }
    PangoLayout* layout = btc.layout;
    pango_layout_set_text(layout, buf, -1);
    pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

    int tw = 0, th = 0, gw = 0;
    if (pbatit != g_batteryTextCache.end() && pbatit->second.text == buf && pbatit->second.glyph == glyph) {
      tw = pbatit->second.textW;
      th = pbatit->second.textH;
      gw = pbatit->second.glyphW;
    } else {
      pango_layout_get_pixel_size(layout, &tw, &th);
      eh::shell::ensure_material_symbols_font_registered();
      {
        PangoLayout* gl = pango_cairo_create_layout(cr);
        PangoFontDescription* gd = pango_font_description_new();
        pango_font_description_set_family(gd, "Material Symbols Rounded");
        pango_font_description_set_weight(gd, PANGO_WEIGHT_NORMAL);
        pango_font_description_set_absolute_size(gd, static_cast<int>(glyphPx * PANGO_SCALE));
        pango_layout_set_font_description(gl, gd);
        pango_layout_set_text(gl, glyph, -1);
        pango_layout_get_pixel_size(gl, &gw, nullptr);
        pango_font_description_free(gd);
        g_object_unref(gl);
      }
      btc.text = buf;
      btc.glyph = glyph;
      btc.textW = tw;
      btc.textH = th;
      btc.glyphW = gw;
    }

    const double labelY = y + (slot_h - static_cast<double>(th)) * 0.5;

    const double glyphW = static_cast<double>(gw);
    const double gap = 6.0 * us;
    const double totalInner = glyphW + gap + static_cast<double>(tw);
    const double innerX = x + (slot_w - totalInner) * 0.5;
    const double glyphCx = innerX + glyphW * 0.5;
    const double glyphCy = y + slot_h * 0.5;

    eh::shell::draw_material_glyph(cr, glyphCx, glyphCy, glyphPx, glyph, fgR, fgG, fgB, 1.0);

    const double labelX = innerX + glyphW + gap;
    cairo_set_source_rgba(cr, fgR, fgG, fgB, 1.0);
    cairo_move_to(cr, labelX, labelY);
    pango_cairo_show_layout(cr, layout);
  } else {
    const char* glyph = battery_glyph_for(g_cachedState.percentage, g_cachedState.state);
    eh::shell::draw_material_glyph(cr, gx, gy, glyphPx, glyph, fgR, fgG, fgB, 1.0);
  }

  cairo_restore(cr);
}

// Popup.

int battery_popup_height() {
   
  return 160;
}

void dock_battery_popup_paint(double pointerX, double pointerY, cairo_t* cr, const eh::config::ShellConfig& sc) {
   
  const double W = static_cast<double>(kBatteryPopupW);
  const double H = static_cast<double>(battery_popup_height());

  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const double shellOv = static_cast<double>(
      eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::Weather));

  const double us = dock_ui_scale(sc.dock);
  const double kPad = 16.0 * us;
  const double kCornerRadius = 14.0 * us;
  const double kShadOffX = 2.0 * us, kShadOffY = 4.0 * us;
  constexpr double kShadAlpha = 0.28;

  auto round_rect = [&](double x, double y, double w, double h, double r) {
    const double rad = std::min({r, w * 0.5, h * 0.5});
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - rad, y + rad,     rad, -M_PI_2,      0);
    cairo_arc(cr, x + w - rad, y + h - rad, rad,       0, M_PI_2);
    cairo_arc(cr, x + rad,     y + h - rad, rad,  M_PI_2,   M_PI);
    cairo_arc(cr, x + rad,     y + rad,     rad,     M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
  };

  // Shadow
  cairo_save(cr);
  round_rect(kShadOffX, kShadOffY, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, 0, 0, 0, kShadAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  // Background
  {
    m3::Box box;
    box.setColor(static_cast<float>(mc.dockFillR * 0.35), static_cast<float>(mc.dockFillG * 0.35),
                 static_cast<float>(mc.dockFillB * 0.35), static_cast<float>(0.78 * shellOv));
    box.setRadius(static_cast<float>(kCornerRadius));
    box.setGeometry(0, 0, static_cast<float>(W), static_cast<float>(H));
    box.setGlassy(true);
    box.paint(cr);
  }

  round_rect(0.5, 0.5, W - 1.0, H - 1.0, kCornerRadius);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Close button
  const double cbX = W - kPad - 24.0 * us, cbY = kPad, cbW = 24.0 * us, cbH = 24.0 * us;
  const double px = pointerX, py = pointerY;
  const bool hc = px >= cbX && px < cbX + cbW && py >= cbY && py < cbY + cbH;
  round_rect(cbX, cbY, cbW, cbH, 7 * us);
  cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, hc ? 0.75 : 0.45);
  cairo_fill(cr);
  eh::shell::draw_material_glyph(cr, cbX + cbW * 0.5, cbY + cbH * 0.5,
                                 14 * us, "close", mc.textR, mc.textG, mc.textB, hc ? 1.0 : 0.85);

  // Title
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.92);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 14 * us);
  cairo_move_to(cr, kPad, kPad + 22.0 * us);
  cairo_show_text(cr, "Battery");

  {
    std::lock_guard<std::mutex> lock(g_batteryMutex);

    if (!g_cachedState.isPresent) {
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.5);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 13 * us);
      cairo_move_to(cr, kPad, 70.0 * us);
      cairo_show_text(cr, "No battery detected");
      return;
    }

    const int pct = static_cast<int>(std::round(g_cachedState.percentage));
    const bool charging = is_charging(g_cachedState.state);

    // Percentage number (big)
    {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d%%", pct);
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 1.0);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 36 * us);
      cairo_text_extents_t te;
      cairo_text_extents(cr, buf, &te);
      cairo_move_to(cr, (W - te.x_advance) * 0.5, 82.0 * us);
      cairo_show_text(cr, buf);
    }

    // Bar background
    const double barX = kPad, barW = W - kPad * 2, barH = 10.0 * us, barY = 98.0 * us;
    const double barR = barH * 0.5;
    round_rect(barX, barY, barW, barH, barR);
    cairo_set_source_rgba(cr, 0.15, 0.15, 0.18, 0.5);
    cairo_fill(cr);

    // Bar fill
    if (pct > 0) {
      const double fillW = std::max(barH, barW * (pct / 100.0));
      double fgR = mc.accentR, fgG = mc.accentG, fgB = mc.accentB;
      if (charging) {
        fgR = 0.4; fgG = 0.8; fgB = 1.0;
      } else if (pct <= 10) {
        fgR = 1.0; fgG = 0.4; fgB = 0.4;
      }
      round_rect(barX, barY, fillW, barH, barR);
      cairo_set_source_rgba(cr, fgR, fgG, fgB, 0.85);
      cairo_fill(cr);
    }

    // Status text
    {
      const char* status = nullptr;
      if (charging) status = "Charging";
      else if (g_cachedState.state == BatteryState::FullyCharged) status = "Fully charged";
      else status = "Discharging";
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.7);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 13 * us);
      cairo_text_extents_t te;
      cairo_text_extents(cr, status, &te);
      cairo_move_to(cr, (W - te.x_advance) * 0.5, barY + barH + 22.0 * us);
      cairo_show_text(cr, status);
    }

    // Glyph icon next to title
    const char* glyph = battery_glyph_for(g_cachedState.percentage, g_cachedState.state);
    eh::shell::draw_material_glyph(cr, kPad + 88.0 * us, kPad + 16.0 * us, 18 * us, glyph, mc.textR, mc.textG, mc.textB, 0.85);
  }
}

void dock_battery_popup_handle_click(::DockApp& app, double x, double y, uint32_t) {
   
  const double W = static_cast<double>(kBatteryPopupW);
  const double H = static_cast<double>(battery_popup_height());

  if (x < 0 || x >= W || y < 0 || y >= H) {
    popup_close(app);
    return;
  }

  constexpr double kPad = 16.0;
  const double cbX = W - kPad - 24.0, cbY = kPad;
  if (x >= cbX && x < cbX + 24.0 && y >= cbY && y < cbY + 24.0) {
    popup_close(app);
  }
}

}
