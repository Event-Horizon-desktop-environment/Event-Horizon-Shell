#include "services/udisks2/udisks2_drive_service.hpp"

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>

#include <iostream>
#include <map>
#include <optional>
#include <vector>

namespace eh::drives {

namespace {

const sdbus::ServiceName kUDisks2{"org.freedesktop.UDisks2"};
const sdbus::ObjectPath kUDisks2Root{"/org/freedesktop/UDisks2"};
constexpr auto kObjMgr = "org.freedesktop.DBus.ObjectManager";
constexpr auto kProps = "org.freedesktop.DBus.Properties";
constexpr auto kFilesystem = "org.freedesktop.UDisks2.Filesystem";
constexpr auto kBlock = "org.freedesktop.UDisks2.Block";

using IfaceProps = std::map<std::string, sdbus::Variant>;
using ManagedObjects = std::map<sdbus::ObjectPath, std::map<std::string, IfaceProps>>;

template <typename T>
T get_or(const IfaceProps& p, const char* key, T fallback) {
   
  auto it = p.find(key);
  if (it == p.end()) return fallback;
  try {
    return static_cast<T>(it->second);
  } catch (const sdbus::Error&) {
    return fallback;
  }
}

std::vector<uint8_t> string_to_bytes(const std::string& s) {
   
  return std::vector<uint8_t>(s.begin(), s.end());
}

std::string bytes_to_string(const std::vector<uint8_t>& bytes) {
   
  std::string s;
  s.reserve(bytes.size());
  for (uint8_t b : bytes) {
    if (b == 0) break;
    s.push_back(static_cast<char>(b));
  }
  return s;
}

}

UDisks2DriveService& UDisks2DriveService::instance() {
   
  static UDisks2DriveService s{};
  return s;
}

UDisks2DriveService::UDisks2DriveService() = default;
UDisks2DriveService::~UDisks2DriveService() = default;

void UDisks2DriveService::start() {
   
  std::lock_guard<std::mutex> lock(mtx_);
  if (started_) return;
  started_ = true;

  try {
    bus_ = sdbus::createSystemBusConnection();
    proxy_ = sdbus::createProxy(*bus_, kUDisks2, kUDisks2Root);
    bind_signals();
    bus_->enterEventLoopAsync();
  } catch (const sdbus::Error& e) {
    std::cerr << "[udisks2] failed to connect: " << e.what() << '\n';
  }
}

void UDisks2DriveService::set_change_callback(ChangeCallback cb) {
   
  std::lock_guard<std::mutex> lock(mtx_);
  on_change_ = std::move(cb);
}

void UDisks2DriveService::bind_signals() {
   
  if (!proxy_) return;

  auto notify = [this]() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (on_change_) on_change_();
  };

  proxy_->uponSignal("InterfacesAdded").onInterface(kObjMgr).call(
      [notify](const sdbus::ObjectPath&,
               const std::map<std::string, std::map<std::string, sdbus::Variant>>&) { notify(); });

  proxy_->uponSignal("InterfacesRemoved").onInterface(kObjMgr).call(
      [notify](const sdbus::ObjectPath&, const std::vector<std::string>&) { notify(); });

  proxy_->uponSignal("PropertiesChanged").onInterface(kProps).call(
      [notify](const std::string& iface,
               const std::map<std::string, sdbus::Variant>&,
               const std::vector<std::string>&) {
         
        if (iface == kFilesystem) notify();
      });
}

std::vector<DriveInfo> UDisks2DriveService::query_drives() {
   
  // Capture the proxy pointer under the lock, then release the lock before
  // making the synchronous D-Bus call. Holding the lock across callMethod
  // can deadlock because sd_bus_call internally dispatches pending signals
  // on the calling thread, and the signal handlers (InterfacesAdded,
  // PropertiesChanged, etc.) try to acquire the same mutex.
  sdbus::IProxy* proxy;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!proxy_) return {};
    proxy = proxy_.get();
  }

  ManagedObjects objs;
  try {
    proxy->callMethod("GetManagedObjects").onInterface(kObjMgr).storeResultsTo(objs);
  } catch (const sdbus::Error& e) {
#ifndef NDEBUG
    std::cerr << "[udisks2] GetManagedObjects failed: " << e.what() << '\n';
#endif
    return {};
  }

  std::vector<DriveInfo> drives;
  for (const auto& [path, ifaces] : objs) {
    auto block_it = ifaces.find(kBlock);
    auto fs_it = ifaces.find(kFilesystem);
    if (block_it == ifaces.end() || fs_it == ifaces.end()) continue;

    const IfaceProps& block = block_it->second;

    if (get_or<bool>(block, "HintIgnore", false)) continue;

    const std::vector<uint8_t> dev_bytes = get_or<std::vector<uint8_t>>(block, "Device", {});
    if (dev_bytes.empty()) continue;
    std::string device = bytes_to_string(dev_bytes);
    if (device.empty()) continue;

    std::string label = get_or<std::string>(block, "IdLabel", "");
    if (label.empty()) label = device.substr(device.find_last_of('/') + 1);

    const IfaceProps& fs = fs_it->second;
    const std::vector<std::vector<uint8_t>> mount_bytes =
        get_or<std::vector<std::vector<uint8_t>>>(fs, "MountPoints", {});

    DriveInfo di;
    di.device = std::move(device);
    di.label = std::move(label);
    di.mounted = !mount_bytes.empty();
    if (di.mounted && !mount_bytes[0].empty())
      di.mount_point = bytes_to_string(mount_bytes[0]);
    di.object_path = path;
    di.id_uuid = get_or<std::string>(block, "IdUUID", "");
    di.id_type = get_or<std::string>(block, "IdType", "");
    di.size = get_or<uint64_t>(block, "Size", 0);

#ifndef NDEBUG
    std::cerr << "[udisks2] drive device=\"" << di.device << "\" label=\"" << di.label << "\" mounted=" << di.mounted
              << " path=\"" << di.object_path << "\" uuid=\"" << di.id_uuid << "\" fstype=\"" << di.id_type << "\"\n";
#endif
    drives.push_back(std::move(di));
  }

#ifndef NDEBUG
  std::cerr << "[udisks2] found " << drives.size() << " drives total\n";
#endif
  return drives;
}

std::string UDisks2DriveService::mount(const std::string& object_path, const std::string& options) {
   
  if (!bus_) return {};
  try {
    auto dev = sdbus::createProxy(*bus_, kUDisks2, sdbus::ObjectPath{object_path});
    std::map<std::string, sdbus::Variant> opts;
    opts["options"] = sdbus::Variant(std::string{options});
    std::string result;
    dev->callMethod("Mount").onInterface(kFilesystem).withArguments(std::move(opts)).storeResultsTo(result);
    return result;
  } catch (const sdbus::Error& e) {
    std::cerr << "[udisks2] mount failed for " << object_path << ": " << e.what() << '\n';
    return {};
  }
}

bool UDisks2DriveService::unmount(const std::string& object_path) {
   
  if (!bus_) return false;
  try {
    auto dev = sdbus::createProxy(*bus_, kUDisks2, sdbus::ObjectPath{object_path});
    std::map<std::string, sdbus::Variant> opts;
    dev->callMethod("Unmount").onInterface(kFilesystem).withArguments(std::move(opts));
    return true;
  } catch (const sdbus::Error& e) {
    std::cerr << "[udisks2] unmount failed for " << object_path << ": " << e.what() << '\n';
    return false;
  }
}

bool UDisks2DriveService::has_fstab_entry(const std::string& object_path) {
   
  if (!bus_) return false;
  try {
    auto dev = sdbus::createProxy(*bus_, kUDisks2, sdbus::ObjectPath{object_path});
    sdbus::Variant v = dev->getProperty("Configuration").onInterface(kBlock);
    auto config = v.get<std::vector<std::tuple<std::string, std::map<std::string, sdbus::Variant>>>>();
    for (const auto& item : config) {
      if (std::get<0>(item) == "fstab") return true;
    }
  } catch (const sdbus::Error& e) {
    std::cerr << "[udisks2] has_fstab_entry failed for " << object_path << ": " << e.what() << '\n';
  }
  return false;
}

bool UDisks2DriveService::add_fstab_entry(const std::string& object_path, const std::string& mount_point,
                                          const std::string& fstype, const std::string& uuid) {
   
  if (!bus_) return false;
  try {
    auto dev = sdbus::createProxy(*bus_, kUDisks2, sdbus::ObjectPath{object_path});

    std::string fsname = uuid.empty() ? "" : "UUID=" + uuid;
    std::map<std::string, sdbus::Variant> item;
    if (!fsname.empty()) item["fsname"] = sdbus::Variant{string_to_bytes(fsname)};
    item["dir"] = sdbus::Variant{string_to_bytes(mount_point)};
    item["type"] = sdbus::Variant{string_to_bytes(fstype.empty() ? "auto" : fstype)};
    item["opts"] = sdbus::Variant{string_to_bytes("defaults")};
    item["freq"] = sdbus::Variant{int32_t{0}};
    item["passno"] = sdbus::Variant{int32_t{0}};

    auto fstab_item = std::make_tuple(std::string{"fstab"}, std::move(item));
    std::map<std::string, sdbus::Variant> empty_opts;
    dev->callMethod("AddConfigurationItem").onInterface("org.freedesktop.UDisks2.Block").withArguments(std::move(fstab_item), std::move(empty_opts));
    return true;
  } catch (const sdbus::Error& e) {
    std::cerr << "[udisks2] add_fstab_entry failed for " << object_path << ": " << e.what() << '\n';
    return false;
  }
}

void UDisks2DriveService::mount_async(const std::string& object_path, std::function<void(bool)> cb,
                                       const std::string& options) {
   
  if (!bus_) { if (cb) cb(false); return; }
  try {
    auto proxy = sdbus::createProxy(*bus_, kUDisks2, sdbus::ObjectPath{object_path});
    auto sp = std::make_shared<std::unique_ptr<sdbus::IProxy>>(std::move(proxy));
    std::map<std::string, sdbus::Variant> opts;
    opts["options"] = sdbus::Variant(std::string{options});
    (*sp)->callMethodAsync("Mount")
        .onInterface(kFilesystem)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error, std::string) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    std::cerr << "[udisks2] mount_async failed: " << e.what() << '\n';
    if (cb) cb(false);
  }
}

void UDisks2DriveService::unmount_async(const std::string& object_path, std::function<void(bool)> cb) {
   
  if (!bus_) { if (cb) cb(false); return; }
  try {
    auto proxy = sdbus::createProxy(*bus_, kUDisks2, sdbus::ObjectPath{object_path});
    auto sp = std::make_shared<std::unique_ptr<sdbus::IProxy>>(std::move(proxy));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Unmount")
        .onInterface(kFilesystem)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    std::cerr << "[udisks2] unmount_async failed: " << e.what() << '\n';
    if (cb) cb(false);
  }
}

void UDisks2DriveService::add_fstab_async(const std::string& object_path, const std::string& mount_point,
                                           const std::string& fstype, const std::string& uuid,
                                           std::function<void(bool)> cb) {
   
  if (!bus_) { if (cb) cb(false); return; }
  try {
    auto proxy = sdbus::createProxy(*bus_, kUDisks2, sdbus::ObjectPath{object_path});
    auto sp = std::make_shared<std::unique_ptr<sdbus::IProxy>>(std::move(proxy));
    std::string fsname = uuid.empty() ? "" : "UUID=" + uuid;
    std::map<std::string, sdbus::Variant> item;
    if (!fsname.empty()) item["fsname"] = sdbus::Variant{string_to_bytes(fsname)};
    item["dir"] = sdbus::Variant{string_to_bytes(mount_point)};
    item["type"] = sdbus::Variant{string_to_bytes(fstype.empty() ? "auto" : fstype)};
    item["opts"] = sdbus::Variant{string_to_bytes("defaults")};
    item["freq"] = sdbus::Variant{int32_t{0}};
    item["passno"] = sdbus::Variant{int32_t{0}};
    auto fstab_item = std::make_tuple(std::string{"fstab"}, std::move(item));
    std::map<std::string, sdbus::Variant> empty_opts;
    (*sp)->callMethodAsync("AddConfigurationItem")
        .onInterface(kBlock)
        .withArguments(std::move(fstab_item), std::move(empty_opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    std::cerr << "[udisks2] add_fstab_async failed: " << e.what() << '\n';
    if (cb) cb(false);
  }
}

}
