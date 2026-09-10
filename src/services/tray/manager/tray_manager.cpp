#include "services/tray/manager/tray_manager.hpp"
#include "services/tray/dbus/tray_sni_icons.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <utility>

#include <cairo/cairo.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace eh::tray {

TrayManager& TrayManager::instance() {
   
  static TrayManager mgr;
  return mgr;
}

TrayManager::~TrayManager() {
   
  MANGOWM_INFO("{}", __func__);
  shutdown();
}

// Public API.

bool TrayManager::start() {
   
  if (running_) return true;
  start_watcher();
  running_ = true;
  return running_;
}

void TrayManager::shutdown() {
   
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) return;
    running_ = false;

    for (auto& it : items_) {
      if (it.pixSurface) {
        cairo_surface_destroy(it.pixSurface);
        it.pixSurface = nullptr;
      }
      it.proxy.reset();
    }
    items_.clear();

    for (int fd : subscribers_) {
      if (fd >= 0) close(fd);
    }
    subscribers_.clear();
  }

  nameOwnerChangedSlot_.reset();
  dbusDaemonProxy_.reset();
  watcherObj_.reset();
  bus_.reset();
}

std::vector<TrayItem> TrayManager::copy_items() const {
   
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<TrayItem> out;
  out.reserve(items_.size());
  for (const auto& item : items_) {
    TrayItem copy = item;
    if (copy.pixSurface) {
      cairo_surface_reference(copy.pixSurface);
    }
    out.push_back(std::move(copy));
  }
  return out;
}

sdbus::IConnection* TrayManager::connection() const {
   
  return bus_.get();
}

int TrayManager::subscribe() {
   
  int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (fd >= 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.push_back(fd);
  }
  return fd;
}

void TrayManager::unsubscribe(int fd) {
   
  if (fd < 0) return;
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find(subscribers_.begin(), subscribers_.end(), fd);
  if (it != subscribers_.end()) {
    subscribers_.erase(it);
  }
  close(fd);
}

// Internal.

std::pair<std::string, std::string> TrayManager::parse_service_path(const std::string& arg) {
   
  const auto slash = arg.find('/');
  if (slash == std::string::npos) return {arg, "/StatusNotifierItem"};
  if (slash == 0) return {"", arg};
  return {arg.substr(0, slash), arg.substr(slash)};
}

void TrayManager::update_item_properties(TrayItem& item) {
   
  if (!item.proxy) return;
  eh::tray::sni::update_item_from_proxy(*item.proxy, item.id, item.title, item.iconName,
                                        item.pixW, item.pixH, item.pixData,
                                        item.pixDataCairo, item.pixSurface);
}

void TrayManager::add_item(const std::string& svc, const std::string& path) {
   
  if (!bus_) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) return;
  auto it = std::find_if(items_.begin(), items_.end(), [&](const TrayItem& t) {
    return t.service == svc && t.path == path;
  });
  if (it != items_.end()) return;

  TrayItem item;
  item.service = svc;
  item.path = path;
  {
    auto up = sdbus::createProxy(*bus_, sdbus::ServiceName{svc}, sdbus::ObjectPath{path});
    item.proxy = std::shared_ptr<sdbus::IProxy>(up.release());
  }
  update_item_properties(item);
  std::cerr << "[tray] registered: service='" << item.service << "' path='" << item.path
            << "' id='" << item.id << "' title='" << item.title
            << "' icon='" << item.iconName << "' hasPixmap=" << (item.pixSurface ? "yes" : "no") << "\n";
  if (item.iconName.empty() && !item.pixSurface) {
    std::cerr << "[tray-icon] miss: service='" << item.service << "' path='" << item.path
              << "' id='" << item.id << "' title='" << item.title
              << "' reason=empty_IconName\n";
  }
  if (items_.size() >= 64) {
    std::cerr << "[tray] cap reached (64), dropping: " << svc << path << "\n";
    return;
  }
  items_.push_back(std::move(item));
}

void TrayManager::remove_item(const std::string& svc, const std::string& path) {
   
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) return;
  for (auto& it : items_) {
    if (it.service == svc && it.path == path) {
      if (it.pixSurface) {
        cairo_surface_destroy(it.pixSurface);
        it.pixSurface = nullptr;
      }
    }
  }
  items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const TrayItem& t) {
    return t.service == svc && t.path == path;
  }), items_.end());
}

void TrayManager::remove_items_for_service(const std::string& svc) {
   
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) return;
  for (auto& it : items_) {
    if (it.service == svc) {
      if (it.pixSurface) {
        cairo_surface_destroy(it.pixSurface);
        it.pixSurface = nullptr;
      }
    }
  }
  items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const TrayItem& t) {
    return t.service == svc;
  }), items_.end());
}

void TrayManager::setup_name_owner_watch() {
   
  if (!bus_) return;
  try {
    dbusDaemonProxy_ = sdbus::createProxy(*bus_,
      sdbus::ServiceName{"org.freedesktop.DBus"},
      sdbus::ObjectPath{"/org/freedesktop/DBus"});

    nameOwnerChangedSlot_ = dbusDaemonProxy_->uponSignal("NameOwnerChanged")
      .onInterface("org.freedesktop.DBus")
      .call([this](const std::string& name, const std::string& oldOwner, const std::string& newOwner) {
        try {
          // Name lost: newOwner is empty, oldOwner is set
          if (!newOwner.empty() || oldOwner.empty()) return;
          std::cerr << "[tray] name lost: '" << name << "' old=" << oldOwner << "\n";
          remove_items_for_service(name);
          notify_subscribers();
        } catch (const std::exception& e) {
          std::cerr << "[tray] error in NameOwnerChanged: " << e.what() << "\n";
        }
      }, sdbus::return_slot);
  } catch (const std::exception& e) {
    std::cerr << "[tray] failed to setup NameOwnerChanged watch: " << e.what() << "\n";
  }
}

void TrayManager::notify_subscribers() {
   
  std::vector<int> subs;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    subs = subscribers_;
  }
  for (int fd : subs) {
    if (fd >= 0) {
      const uint64_t one = 1;
      (void)write(fd, &one, sizeof(one));
    }
  }
}

void TrayManager::start_watcher() {
    
  try {
    try {
      bus_ = sdbus::createSessionBusConnection();
    } catch (const std::exception& e) {
      std::cerr << "[tray] watcher connect failed: " << e.what() << "\n";
      start_client();
      return;
    }

    // Check if another StatusNotifierWatcher (e.g. ksni) already owns the bus name.
    // Claiming it would break system tray for all applications.
    {
      auto daemonProxy = sdbus::createProxy(*bus_,
          sdbus::ServiceName{"org.freedesktop.DBus"},
          sdbus::ObjectPath{"/org/freedesktop/DBus"});

      bool alreadyRunning = false;
      try {
        daemonProxy->callMethod("NameHasOwner")
            .onInterface("org.freedesktop.DBus")
            .withArguments(std::string{"org.kde.StatusNotifierWatcher"})
            .storeResultsTo(alreadyRunning);
      } catch (const sdbus::Error&) {
      }

      if (alreadyRunning) {
        std::cerr << "[tray] StatusNotifierWatcher already owned -> client mode\n";
        bus_.reset();
        start_client();
        return;
      }
    }

    bus_->requestName(sdbus::ServiceName{"org.kde.StatusNotifierWatcher"});

    watcherObj_ = sdbus::createObject(*bus_, sdbus::ObjectPath{"/StatusNotifierWatcher"});

    watcherObj_->addVTable(
      sdbus::registerMethod("RegisterStatusNotifierItem")
        .withInputParamNames("service")
        .implementedAs([this](const std::string& serviceArg) {
          try {
            auto [svc, path] = parse_service_path(serviceArg);
            if (svc.empty()) {
              notify_subscribers();
              return;
            }
            std::cerr << "[tray] RegisterStatusNotifierItem: arg='" << serviceArg << "'\n";
            add_item(svc, path);
            notify_subscribers();
          } catch (const std::exception& e) {
            std::cerr << "[tray] error handling RegisterStatusNotifierItem: " << e.what() << "\n";
          }
        }),
      sdbus::registerProperty("RegisteredStatusNotifierItems")
        .withGetter([this]() {
          std::vector<std::string> out;
          std::lock_guard<std::mutex> lock(mutex_);
          out.reserve(items_.size());
          for (const auto& it : items_) out.push_back(it.service + it.path);
          return out;
        }),
      sdbus::registerProperty("IsStatusNotifierHostRegistered")
        .withGetter([]() { return true; }),
      sdbus::registerProperty("ProtocolVersion")
        .withGetter([]() { return int32_t{0}; })
    ).forInterface("org.kde.StatusNotifierWatcher");

    setup_name_owner_watch();
    bus_->enterEventLoopAsync();
    std::cerr << "[tray] watcher running on session bus\n";
    discover_existing_items();
  } catch (const std::exception& e) {
    const std::string msg = e.what();

    if (msg.find("FileExists") != std::string::npos) {
      std::cerr << "[tray] StatusNotifierWatcher already owned -> client mode\n";
      watcherObj_.reset();
      start_client();
      return;
    }
    std::cerr << "[tray] failed to start watcher: " << msg << "\n";
    watcherObj_.reset();
    bus_.reset();
  }
}

void TrayManager::discover_existing_items() {
  if (!bus_) return;
  try {
    auto dbusProxy = sdbus::createProxy(*bus_,
      sdbus::ServiceName{"org.freedesktop.DBus"},
      sdbus::ObjectPath{"/org/freedesktop/DBus"});

    std::vector<std::string> names;
    dbusProxy->callMethod("ListNames")
      .onInterface("org.freedesktop.DBus")
      .storeResultsTo(names);

    bool foundAny = false;
    for (const auto& name : names) {
      if (name.find("org.kde.StatusNotifier") == 0) continue;
      if (name.find("org.freedesktop.") == 0) continue;
      if (name.find(":") == 0) continue;
      try {
        auto probe = sdbus::createProxy(*bus_,
          sdbus::ServiceName{name},
          sdbus::ObjectPath{"/StatusNotifierItem"});
        auto id = probe->getProperty("Id")
          .onInterface("org.kde.StatusNotifierItem")
          .get<std::string>();
        if (!id.empty()) {
          std::cerr << "[tray] discover: found existing SNI item name='" << name << "' id='" << id << "'\n";
          add_item(name, "/StatusNotifierItem");
          foundAny = true;
        }
      } catch (const sdbus::Error&) {
      } catch (const std::exception&) {
      }
    }
    if (foundAny) notify_subscribers();
  } catch (const std::exception& e) {
    std::cerr << "[tray] discover_existing_items failed: " << e.what() << "\n";
  }
}

void TrayManager::start_client() {
   
  try {
    if (!bus_) {
      try {
        bus_ = sdbus::createSessionBusConnection();
      } catch (const std::exception& e) {
        std::cerr << "[tray] connect failed, skipping tray: " << e.what() << "\n";
        return;
      }
    }

    auto watcher = sdbus::createProxy(*bus_,
                                     sdbus::ServiceName{"org.kde.StatusNotifierWatcher"},
                                     sdbus::ObjectPath{"/StatusNotifierWatcher"});

    auto items = watcher->getProperty("RegisteredStatusNotifierItems")
                   .onInterface("org.kde.StatusNotifierWatcher")
                   .get<std::vector<std::string>>();
    for (const auto& s : items) {
      auto [svc, path] = parse_service_path(s);
      if (svc.empty()) continue;
      add_item(svc, path);
    }
    notify_subscribers();

    watcher->uponSignal("StatusNotifierItemRegistered")
      .onInterface("org.kde.StatusNotifierWatcher")
      .call([this](const std::string& serviceAndPath) {
        try {
          auto [svc, path] = parse_service_path(serviceAndPath);
          if (svc.empty()) return;
          std::cerr << "[tray] client registered: '" << serviceAndPath << "'\n";
          add_item(svc, path);
          notify_subscribers();
        } catch (const std::exception& e) {
          std::cerr << "[tray] error in StatusNotifierItemRegistered: " << e.what() << "\n";
        }
      });

    watcher->uponSignal("StatusNotifierItemUnregistered")
      .onInterface("org.kde.StatusNotifierWatcher")
      .call([this](const std::string& serviceAndPath) {
        try {
          auto [svc, path] = parse_service_path(serviceAndPath);
          if (svc.empty()) return;
          std::cerr << "[tray] client unregistered: '" << serviceAndPath << "'\n";
          remove_item(svc, path);
          notify_subscribers();
        } catch (const std::exception& e) {
          std::cerr << "[tray] error in StatusNotifierItemUnregistered: " << e.what() << "\n";
        }
      });

    setup_name_owner_watch();
    bus_->enterEventLoopAsync();
  } catch (const std::exception& e) {
    std::cerr << "[tray] failed to connect as client: " << e.what() << "\n";
  }
}

}
