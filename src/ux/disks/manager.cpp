#include "ux/disks/manager.hpp"
#include "ux/disks/drive.hpp"
#include "ux/disks/block.hpp"

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>

#include <algorithm>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <set>
#include <unistd.h>
#include <vector>

namespace eh::disks {

namespace {

const sdbus::ServiceName kUDisks2{"org.freedesktop.UDisks2"};
const sdbus::ObjectPath kUDisks2Root{"/org/freedesktop/UDisks2"};
constexpr auto kObjMgr = "org.freedesktop.DBus.ObjectManager";
constexpr auto kProps = "org.freedesktop.DBus.Properties";

constexpr auto kDrive = "org.freedesktop.UDisks2.Drive";
constexpr auto kBlock = "org.freedesktop.UDisks2.Block";

using PropMap = std::map<std::string, sdbus::Variant>;
using IfaceMap = std::map<std::string, PropMap>;
using ManagedObjects = std::map<sdbus::ObjectPath, IfaceMap>;

template <typename T>
T get_or(const PropMap& p, const char* key, T fallback) {
  auto it = p.find(key);
  if (it == p.end()) return fallback;
  try { return static_cast<T>(it->second); }
  catch (const sdbus::Error&) { return fallback; }
}

bool has_interface(const IfaceMap& ifaces, const char* name) {
  return ifaces.find(name) != ifaces.end();
}

}

// Instance.

Manager& Manager::instance() {
  static Manager m;
  return m;
}

void Manager::start() {
  if (started_) return;
  started_ = true;

  try {
    bus_ = sdbus::createSystemBusConnection();
    proxy_ = sdbus::createProxy(*bus_, kUDisks2, kUDisks2Root);
    bind_signals();
    rebuild_object_tree();
    // Fire initial change notification
    if (on_change_) on_change_();
    bus_->enterEventLoopAsync();
  } catch (const sdbus::Error& e) {
    std::cerr << "[disks] failed to connect to UDisks2: " << e.what() << '\n';
  }
}

void Manager::set_change_callback(ChangeCallback cb) {
  on_change_ = std::move(cb);
}

// Signals.

void Manager::bind_signals() {
  if (!proxy_) return;

  auto notify = [this]() {
    rebuild_object_tree();
    if (on_change_) on_change_();
  };

  // We need a mutable container to track objects across signal callbacks.
  // Allocate it on the heap so it's shared across all lambda captures.
  auto objects = std::make_shared<std::map<sdbus::ObjectPath, IfaceMap>>();

  // Initial population
  ManagedObjects initial;
  try {
    proxy_->callMethod("GetManagedObjects")
        .onInterface(kObjMgr)
        .storeResultsTo(initial);
  } catch (const sdbus::Error& e) {
    std::cerr << "[disks] GetManagedObjects failed: " << e.what() << '\n';
    return;
  }
  for (auto& [path, ifaces] : initial) {
    (*objects)[path] = std::move(ifaces);
  }

  proxy_->uponSignal("InterfacesAdded")
      .onInterface(kObjMgr)
      .call([objects, notify](const sdbus::ObjectPath& path,
             const IfaceMap& ifaces) {
        (*objects)[path] = ifaces;
        notify();
      });

  proxy_->uponSignal("InterfacesRemoved")
      .onInterface(kObjMgr)
      .call([objects, notify](const sdbus::ObjectPath& path,
             const std::vector<std::string>&) {
        objects->erase(path);
        notify();
      });

  proxy_->uponSignal("PropertiesChanged")
      .onInterface(kProps)
      .call([objects, notify](const std::string& interface,
             const PropMap& changed,
             const std::vector<std::string>& invalidated) {
        (void)changed;
        (void)invalidated;
        if (interface == kDrive || interface == kBlock ||
            interface == "org.freedesktop.UDisks2.Filesystem" ||
            interface == "org.freedesktop.UDisks2.Encrypted" ||
            interface == "org.freedesktop.UDisks2.Swapspace" ||
            interface == "org.freedesktop.UDisks2.Drive.Ata") {
          notify();
        }
      });
}

// Object tree.

void Manager::rebuild_object_tree() {
  // Re-query the full object tree from UDisks2
  ManagedObjects objs;
  try {
    proxy_->callMethod("GetManagedObjects")
        .onInterface(kObjMgr)
        .storeResultsTo(objs);
  } catch (const sdbus::Error& e) {
    std::cerr << "[disks] GetManagedObjects failed: " << e.what() << '\n';
    return;
  }

  std::lock_guard<std::mutex> lock(mtx_);
  drives_.clear();
  drive_map_.clear();
  block_map_.clear();

  struct BlockInfo {
    std::string path;
    sdbus::ObjectPath drive_path;
    sdbus::ObjectPath crypto_backing;
    BlockInfo(std::string p, sdbus::ObjectPath dp, sdbus::ObjectPath cb)
      : path(std::move(p)), drive_path(std::move(dp)),
        crypto_backing(std::move(cb)) {}
  };
  std::vector<BlockInfo> block_infos;

  // First pass: collect drives and block info
  for (auto& [path, ifaces] : objs) {
    if (has_interface(ifaces, kDrive)) {
      auto drive = std::make_shared<Drive>(path, ifaces, this);
      drives_.push_back(drive);
      drive_map_[path] = drive;
    }
    if (has_interface(ifaces, kBlock)) {
      const auto& block_props = ifaces.at(kBlock);
      auto dp = get_or<sdbus::ObjectPath>(block_props, "Drive", sdbus::ObjectPath{""});
      auto cb = get_or<sdbus::ObjectPath>(block_props, "CryptoBackingDevice", sdbus::ObjectPath{""});
      block_infos.emplace_back(path, dp, cb);
    }
  }

  // Second pass: create blocks and attach to drives
  for (auto& bi : block_infos) {
    auto& ifaces = objs[sdbus::ObjectPath{bi.path}];
    auto block = std::make_shared<Block>(bi.path, ifaces, this);

    if (!bi.drive_path.empty()) {
      auto it = drive_map_.find(bi.drive_path);
      if (it != drive_map_.end()) {
        if (auto drive = it->second.lock()) {
          drive->add_block(block);
        }
      }
    }

    // If this block is a cleartext (unlocked LUKS), link it to its backing
    if (!bi.crypto_backing.empty()) {
      auto cb_it = block_map_.find(bi.crypto_backing);
      if (cb_it != block_map_.end()) {
        if (auto cb = cb_it->second.lock()) {
          cb->set_cleartext(block);
          block->set_backing_device(cb);
        }
      }
    }

    block_map_[bi.path] = block;
  }
}

// Accessors.

std::vector<std::shared_ptr<Drive>> Manager::get_drives() const {
  std::lock_guard<std::mutex> lock(mtx_);
  return drives_;
}

std::shared_ptr<Drive> Manager::find_drive(
    const std::string& object_path) const {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = drive_map_.find(object_path);
  if (it != drive_map_.end()) return it->second.lock();
  return nullptr;
}

std::shared_ptr<Block> Manager::find_block(
    const std::string& object_path) const {
  std::lock_guard<std::mutex> lock(mtx_);
  auto it = block_map_.find(object_path);
  if (it != block_map_.end()) return it->second.lock();
  return nullptr;
}

// Loop operations.

void Manager::open_loop_async(const std::string& file_path, bool read_only,
                               std::function<void(bool, std::string)> cb) {
  if (!bus_) {
    if (cb) cb(false, "Not connected to UDisks2");
    return;
  }

  try {
    auto mgr = sdbus::createProxy(*bus_, kUDisks2,
        sdbus::ObjectPath{"/org/freedesktop/UDisks2/Manager"});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(mgr));

    int fd = open(file_path.c_str(), read_only ? O_RDONLY : O_RDWR);
    if (fd < 0) {
      if (cb) cb(false, "Cannot open file: " + file_path);
      return;
    }

    std::map<std::string, sdbus::Variant> opts;
    opts["read-only"] = sdbus::Variant(read_only);

    (*sp)->callMethodAsync("LoopSetup")
        .onInterface("org.freedesktop.UDisks2.Manager")
        .withArguments(fd, std::move(opts))
        .uponReplyInvoke([sp, cb, fd](std::optional<sdbus::Error> error,
                           sdbus::ObjectPath result) {
          close(fd);
          if (cb) cb(!error.has_value(),
                     error.has_value() ? error->what() : std::string{result});
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false, e.what());
  }
}

void Manager::close_loop_async(const std::string& object_path,
                                std::function<void(bool)> cb) {
  if (!bus_) {
    if (cb) cb(false);
    return;
  }

  try {
    auto mgr = sdbus::createProxy(*bus_, kUDisks2,
        sdbus::ObjectPath{"/org/freedesktop/UDisks2/Manager"});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(mgr));

    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("LoopTeardown")
        .onInterface("org.freedesktop.UDisks2.Manager")
        .withArguments(sdbus::ObjectPath{object_path}, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

}
