#include "services/udisks2/udisks2_drive_service.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <thread>

namespace eh::drives {

namespace {

// Detect mounted drives by parsing /proc/mounts
std::vector<DriveInfo> detect_mounted_drives() {
  std::vector<DriveInfo> drives;
  std::ifstream mounts("/proc/mounts");
  std::string line;
  while (std::getline(mounts, line)) {
    // Format: device mount_point fstype options dump pass
    auto dev_end = line.find(' ');
    if (dev_end == std::string::npos) continue;
    std::string device = line.substr(0, dev_end);

    auto rest = line.substr(dev_end + 1);
    auto mp_end = rest.find(' ');
    if (mp_end == std::string::npos) continue;
    std::string mount_point = rest.substr(0, mp_end);

    // Skip non-real filesystems
    if (device.starts_with("/dev/")) {
      DriveInfo di;
      di.device = device;
      di.mount_point = mount_point;
      di.mounted = true;
      di.label = device.substr(5); // strip "/dev/"
      di.object_path = device;
      drives.push_back(std::move(di));
    }
  }
  return drives;
}

}

UDisks2DriveService& UDisks2DriveService::instance() {
  static UDisks2DriveService inst;
  return inst;
}

UDisks2DriveService::UDisks2DriveService() = default;

UDisks2DriveService::~UDisks2DriveService() = default;

void UDisks2DriveService::start() {
  started_ = true;
}

void UDisks2DriveService::set_change_callback(ChangeCallback cb) {
  on_change_ = std::move(cb);
}

std::vector<DriveInfo> UDisks2DriveService::query_drives() {
  return detect_mounted_drives();
}

static std::string dev_to_udisks_path(const std::string& path) {
  if (path.find("/dev/") == 0) {
    auto name = path.substr(5); // strip "/dev/"
    return "/org/freedesktop/UDisks2/block_devices/" + name;
  }
  return path; // already a UDisks2 object path
}

std::string UDisks2DriveService::mount(const std::string& object_path, const std::string& options) {
  try {
    auto conn = sdbus::createSystemBusConnection();
    auto proxy = sdbus::createProxy(*conn,
                                     sdbus::ServiceName{"org.freedesktop.UDisks2"},
                                     sdbus::ObjectPath{dev_to_udisks_path(object_path)});
    std::map<std::string, sdbus::Variant> mount_opts;
    std::string mount_path;
    proxy->callMethod("Mount")
        .onInterface("org.freedesktop.UDisks2.Filesystem")
        .withArguments(mount_opts)
        .storeResultsTo(mount_path);
    return mount_path;
  } catch (const sdbus::Error&) {
  }

  // Fallback: use udisksctl
  std::string cmd = "udisksctl mount -b " + object_path + " 2>&1";
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return {};
  char buf[512];
  std::string result;
  while (fgets(buf, sizeof(buf), f)) result += buf;
  pclose(f);
  auto pos = result.find("Mounted at ");
  if (pos != std::string::npos) {
    auto start = pos + 11;
    auto end = result.find('\n', start);
    if (end != std::string::npos) return result.substr(start, end - start);
    return result.substr(start);
  }
  return {};
}

bool UDisks2DriveService::unmount(const std::string& object_path) {
  try {
    auto conn = sdbus::createSystemBusConnection();
    auto proxy = sdbus::createProxy(*conn,
                                     sdbus::ServiceName{"org.freedesktop.UDisks2"},
                                     sdbus::ObjectPath{dev_to_udisks_path(object_path)});
    std::map<std::string, sdbus::Variant> unmount_opts;
    proxy->callMethod("Unmount")
        .onInterface("org.freedesktop.UDisks2.Filesystem")
        .withArguments(unmount_opts);
    return true;
  } catch (const sdbus::Error&) {
  }

  // Fallback
  std::string cmd = "udisksctl unmount -b " + object_path + " 2>/dev/null";
  return std::system(cmd.c_str()) == 0;
}

bool UDisks2DriveService::has_fstab_entry(const std::string&) { return false; }

bool UDisks2DriveService::add_fstab_entry(const std::string&, const std::string&,
                                            const std::string&, const std::string&) {
  return false;
}

void UDisks2DriveService::mount_async(const std::string& object_path,
                                        std::function<void(bool)> cb,
                                        const std::string& options) {
  std::thread([this, object_path, options, cb = std::move(cb)] {
    std::string result = mount(object_path, options);
    if (cb) cb(!result.empty());
  }).detach();
}

void UDisks2DriveService::unmount_async(const std::string& object_path,
                                           std::function<void(bool)> cb) {
  std::thread([this, object_path, cb = std::move(cb)] {
    bool ok = unmount(object_path);
    if (cb) cb(ok);
  }).detach();
}

void UDisks2DriveService::add_fstab_async(const std::string&, const std::string&,
                                            const std::string&, const std::string&,
                                            std::function<void(bool)> cb) {
  if (cb) cb(false);
}

void UDisks2DriveService::bind_signals() {}

}
