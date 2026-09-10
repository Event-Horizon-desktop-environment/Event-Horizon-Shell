#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct DockApp;

namespace eh::config {
struct ShellConfig;
}

namespace eh::widgets {

// Mediated Bluetooth operations. These wrap the concrete BluezService so that
// UI code goes through the widget layer instead of calling the service
// singleton directly.

enum class BluetoothDeviceKind {
  Unknown,
  Headset,
  Headphones,
  Earbuds,
  Speaker,
  Microphone,
  Mouse,
  Keyboard,
  Phone,
  Computer,
  Gamepad,
  Watch,
  Tv,
};

struct BluetoothDevice {
  std::string path;
  std::string address;
  std::string alias;
  BluetoothDeviceKind kind = BluetoothDeviceKind::Unknown;
  bool paired = false;
  bool connected = false;
  bool connecting = false;
  int battery_pct = -1;
};

struct BluetoothSnapshot {
  bool available = false;
  bool powered = false;
  bool connected = false;
  int paired_count = 0;
  int connected_count = 0;
};

[[nodiscard]] bool widget_list_contains_bluetooth(const eh::config::ShellConfig& sc,
                                                     const std::vector<std::string>& widgets);

[[nodiscard]] double dock_bluetooth_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc,
                                                std::string_view instance_id, double icon_ref_px, double bar_height);

void paint_bluetooth_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                          double x, double y, double slot_w, double slot_h, double icon_ref_px,
                          bool hovered, bool pressed);

void bluetooth_widget_init();
bool bluetooth_widget_poll();
bool bluetooth_widget_needs_immediate_draw();
void bluetooth_widget_shutdown();

// Ensure the Bluetooth service is running.
void bluetooth_ensure_service();
// Start device discovery (scan).
void bluetooth_start_discovery();
// Stop device discovery.
void bluetooth_stop_discovery();
// Return the current list of known devices.
[[nodiscard]] std::vector<BluetoothDevice> bluetooth_devices();
// Return a snapshot of the current Bluetooth state.
[[nodiscard]] BluetoothSnapshot bluetooth_snapshot();
// Return a material glyph name for the device kind.
[[nodiscard]] const char* bluetooth_device_kind_glyph(BluetoothDeviceKind kind);
// Connect to a device by its object path.
void bluetooth_connect_device(const std::string& path);
// Disconnect a device by its object path.
void bluetooth_disconnect_device(const std::string& path);
// Initiate pairing with a device.
void bluetooth_pair_device(const std::string& path);
// Forget (unpair + remove) a device.
void bluetooth_forget_device(const std::string& path);

// Popup API.
constexpr int kBluetoothPopupW = 340;

int bluetooth_popup_height();

void dock_bluetooth_popup_paint(double pointerX, double pointerY, cairo_t* cr, const eh::config::ShellConfig& sc);

void dock_bluetooth_popup_handle_click(::DockApp& app, double x, double y, uint32_t serial);

}

