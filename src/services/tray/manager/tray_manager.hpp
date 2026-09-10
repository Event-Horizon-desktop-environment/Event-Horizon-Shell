#pragma once

#include "services/tray/manager/tray_item.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace eh::tray {

class TrayManager {
public:
  static TrayManager& instance();

  bool start();
  void shutdown();

  std::vector<TrayItem> copy_items() const;

  sdbus::IConnection* connection() const;

  int subscribe();
  void unsubscribe(int fd);

private:
  TrayManager() = default;
  ~TrayManager();
  TrayManager(const TrayManager&) = delete;
  TrayManager& operator=(const TrayManager&) = delete;
  TrayManager(TrayManager&&) = delete;
  TrayManager& operator=(TrayManager&&) = delete;

  static std::pair<std::string, std::string> parse_service_path(const std::string& arg);

  void add_item(const std::string& svc, const std::string& path);
  void remove_item(const std::string& svc, const std::string& path);
  void remove_items_for_service(const std::string& svc);
  void update_item_properties(TrayItem& item);
  void notify_subscribers();
  void setup_name_owner_watch();

  void start_watcher();
  void start_client();
  void discover_existing_items();

  std::unique_ptr<sdbus::IConnection> bus_;
  std::unique_ptr<sdbus::IObject> watcherObj_;
  std::unique_ptr<sdbus::IProxy> dbusDaemonProxy_;
  sdbus::Slot nameOwnerChangedSlot_;

  mutable std::mutex mutex_;
  std::vector<TrayItem> items_;
  std::vector<int> subscribers_;

  bool running_ = false;
};

}
