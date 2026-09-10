#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sdbus {
class IConnection;
class IProxy;
}

namespace eh::drives {

struct DriveInfo {
  std::string device;
  std::string label;
  std::string mount_point;
  std::string object_path;
  std::string id_uuid;
  std::string id_type;
  uint64_t size = 0;
  bool mounted = false;
};

class UDisks2DriveService {
public:
  using ChangeCallback = std::function<void()>;

  static UDisks2DriveService& instance();

  void start();
  void set_change_callback(ChangeCallback cb);
  std::vector<DriveInfo> query_drives();

  // Mount unmounted drive. Returns mount point on success, empty on failure.
  std::string mount(const std::string& object_path, const std::string& options = "rw");

  // Unmount mounted drive.
  bool unmount(const std::string& object_path);

  // Check if drive already has an fstab entry.
  bool has_fstab_entry(const std::string& object_path);

  // Add fstab entry for mount-at-boot. Returns true on success.
  bool add_fstab_entry(const std::string& object_path, const std::string& mount_point,
                       const std::string& fstype, const std::string& uuid);

  // Async mount using callMethodAsync — keeps PolKit auth on the main DBus connection.
  // Callback fires on the sdbus event loop thread (not the main thread).
  void mount_async(const std::string& object_path, std::function<void(bool)> cb,
                   const std::string& options = "rw");

  // Async unmount.
  void unmount_async(const std::string& object_path, std::function<void(bool)> cb);

  // Async add fstab entry.
  void add_fstab_async(const std::string& object_path, const std::string& mount_point,
                       const std::string& fstype, const std::string& uuid,
                       std::function<void(bool)> cb);

private:
  UDisks2DriveService();
  ~UDisks2DriveService();
  UDisks2DriveService(const UDisks2DriveService&) = delete;
  UDisks2DriveService& operator=(const UDisks2DriveService&) = delete;
  UDisks2DriveService(UDisks2DriveService&&) = delete;
  UDisks2DriveService& operator=(UDisks2DriveService&&) = delete;

  void bind_signals();

  std::mutex mtx_{};
  bool started_ = false;
  ChangeCallback on_change_{};

  std::unique_ptr<sdbus::IConnection> bus_{};
  std::unique_ptr<sdbus::IProxy> proxy_{};
};

}
