#include "wl/input/clipboard.hpp"

#include <wayland-client.h>
#include <ext-data-control-v1-client-protocol.h>
#include <wlr-data-control-unstable-v1-client-protocol.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct BindState {
  void* manager = nullptr;
  const eh::wayland::DataControlOps* ops = nullptr;
  wl_seat* seat = nullptr;
};

void registry_global(void* data, wl_registry* registry, uint32_t name,
                      const char* interface, uint32_t version) {
  auto* s = static_cast<BindState*>(data);
  if (strcmp(interface, ext_data_control_manager_v1_interface.name) == 0) {
    s->manager = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface,
                                  std::min(version, 1u));
    s->ops = eh::wayland::ext_data_control_ops();
  } else if (s->manager == nullptr &&
             strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0) {
    s->manager = wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface,
                                  std::min(version, 2u));
    s->ops = eh::wayland::wlr_data_control_ops();
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    s->seat = static_cast<wl_seat*>(
        wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 1u)));
  }
}

static constexpr wl_registry_listener kRegistryListener = {
    .global = registry_global,
    .global_remove = nullptr,
};

void usage(const char* argv0) {
  std::cerr << "Usage: " << (argv0 ? argv0 : "horizonClipboardDaemon")
            << " <file.png>\n";
}

} // anonymous namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  std::string path = argv[1];

  // Read the PNG file content.
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "Failed to open: " << path << "\n";
    return 1;
  }
  auto size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::string png_data(static_cast<std::size_t>(size), '\0');
  if (!file.read(png_data.data(), size)) {
    std::cerr << "Failed to read: " << path << "\n";
    return 1;
  }

  // Connect to Wayland.
  wl_display* display = wl_display_connect(nullptr);
  if (!display) {
    std::cerr << "Failed to connect to Wayland display.\n";
    return 1;
  }

  wl_registry* registry = wl_display_get_registry(display);
  BindState state{};
  wl_registry_add_listener(registry, &kRegistryListener, &state);
  wl_display_roundtrip(display);
  wl_display_roundtrip(display);
  wl_registry_destroy(registry);

  if (!state.manager) {
    std::cerr << "No data-control protocol available.\n";
    wl_display_disconnect(display);
    return 1;
  }
  if (!state.seat) {
    std::cerr << "No wl_seat available.\n";
    wl_display_disconnect(display);
    return 1;
  }

  eh::wayland::ClipboardService clipboard;
  if (!clipboard.bind(state.manager, state.ops, state.seat, display)) {
    std::cerr << "Failed to bind clipboard.\n";
    wl_display_disconnect(display);
    return 1;
  }

  // Offer image/png with the full file content.
  if (!clipboard.copy_data("image/png", std::move(png_data))) {
    std::cerr << "Failed to copy to clipboard.\n";
    wl_display_disconnect(display);
    return 1;
  }

  (void)wl_display_flush(display);

  // Hold the clipboard until another client replaces the selection.
  while (clipboard.owns_selection()) {
    if (wl_display_dispatch(display) < 0) break;
  }

  wl_display_disconnect(display);
  return 0;
}
