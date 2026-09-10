#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace sdbus {
class IConnection;
class IProxy;
}

namespace eh::disks {

class Drive;
class Block;

using ChangeCallback = std::function<void()>;

class Manager {
public:
  static Manager& instance();

  void start();
  void set_change_callback(ChangeCallback cb);

  std::vector<std::shared_ptr<Drive>> get_drives() const;
  std::shared_ptr<Drive> find_drive(const std::string& object_path) const;
  std::shared_ptr<Block> find_block(const std::string& object_path) const;

  void open_loop_async(const std::string& file_path, bool read_only,
                       std::function<void(bool, std::string)> cb);
  void close_loop_async(const std::string& object_path,
                        std::function<void(bool)> cb);

  // Access the underlying D-Bus proxy for sub-operations
  sdbus::IProxy* proxy() { return proxy_.get(); }
  sdbus::IConnection* bus() { return bus_.get(); }

private:
  Manager() = default;
  ~Manager() = default;
  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;

  void bind_signals();
  void rebuild_object_tree();

  bool started_ = false;
  ChangeCallback on_change_{};

  std::unique_ptr<sdbus::IConnection> bus_;
  std::unique_ptr<sdbus::IProxy> proxy_;

  // Object tree caches (keyed by object_path)
  mutable std::mutex mtx_;
  std::vector<std::shared_ptr<Drive>> drives_;
  std::unordered_map<std::string, std::weak_ptr<Drive>> drive_map_;
  std::unordered_map<std::string, std::weak_ptr<Block>> block_map_;
};

}
