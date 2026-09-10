#include "services/tray/manager/tray.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "services/tray/manager/tray_manager.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <iostream>
#include <mutex>
#include <utility>

#include <cairo/cairo.h>
#include <sys/eventfd.h>
#include <unistd.h>

static void destroy_items(std::vector<DockApp::TrayItem>& items) {
   
  for (auto& it : items) {
    if (it.pixSurface) {
      cairo_surface_destroy(it.pixSurface);
      it.pixSurface = nullptr;
    }
    it.proxy.reset();
  }
  items.clear();
}

void dock_tray_sync_items(DockApp& app) {
   
  auto newItems = eh::tray::TrayManager::instance().copy_items();
  std::lock_guard<std::mutex> lock(app.trayMutex);
  for (auto& it : app.trayItems) {
    if (it.pixSurface) {
      cairo_surface_destroy(it.pixSurface);
      it.pixSurface = nullptr;
    }
    it.proxy.reset();
  }
  app.trayItems = std::move(newItems);
}

void dock_tray_init(DockApp& app) {
   
  app.trayEventFd = eh::tray::TrayManager::instance().subscribe();
  if (app.trayEventFd < 0) {
    std::cerr << "[dock-tray] failed to subscribe to TrayManager\n";
  }
  try {
    app.trayBus = sdbus::createSessionBusConnection();
  } catch (const std::exception& e) {
    std::cerr << "[dock-tray] bus connection failed: " << e.what() << "\n";
  }
  dock_tray_sync_items(app);
}

void dock_tray_shutdown(DockApp& app) {
   
  eh::tray::TrayManager::instance().unsubscribe(app.trayEventFd);
  app.trayEventFd = -1;

  {
    std::lock_guard<std::mutex> lock(app.trayMutex);
    destroy_items(app.trayItems);
  }
}
