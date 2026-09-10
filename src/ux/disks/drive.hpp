#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <map>

#include <sdbus-c++/Types.h>

#include "ux/disks/item.hpp"

namespace sdbus {
class IProxy;
class IConnection;
}

namespace eh::disks {

class Manager;
class Block;

class Drive : public Item, public std::enable_shared_from_this<Drive> {
public:
  Drive(const std::string& object_path,
        const std::map<std::string,
          std::map<std::string, sdbus::Variant>>& ifaces,
        Manager* manager);

  // Item interface
  std::string get_description() const override;
  std::string get_partition_type() const override;
  uint64_t get_size() const override;
  Feature get_features() override;
  ItemPtr get_parent() const override { return nullptr; }
  std::vector<ItemPtr> get_children() const override;
  std::string get_object_path() const override { return object_path_; }
  bool is_drive() const override { return true; }

  // Drive-specific accessors
  std::string get_model() const;
  std::string get_serial() const;
  std::string get_vendor() const;
  std::string get_wwn() const;
  std::string get_firmware_version() const;
  std::string get_media() const;
  double get_rotation_rate() const;
  bool is_removable() const;
  bool is_ejectable() const;
  bool can_power_off() const;
  bool is_media_detected() const;

  // SMART (if available)
  bool smart_supported() const;
  bool smart_failing() const;
  int64_t smart_power_on_hours() const;
  double smart_temperature() const;
  std::string smart_one_liner_assessment(bool* out_warn) const;

  // Loop info (if loop device)
  bool is_loop() const;
  std::string loop_file() const;

  // Children management
  void add_block(std::shared_ptr<Block> block);
  const std::vector<std::shared_ptr<Block>>& blocks() const { return blocks_; }

  // Async operations
  void standby_async(std::function<void(bool)> cb);
  void wakeup_async(std::function<void(bool)> cb);
  void eject_async(std::function<void(bool)> cb);
  void power_off_async(std::function<void(bool)> cb);
  void detach_async(std::function<void(bool)> cb);

  // SMART self-test
  void run_smart_test_async(const std::string& type,
                            std::function<void(bool)> cb);

private:
  void compute_features();

  std::string object_path_;
  Manager* manager_ = nullptr;

  // Property caches (from D-Bus)
  std::map<std::string, sdbus::Variant> drive_props_;
  std::map<std::string, sdbus::Variant> ata_props_;
  std::map<std::string, sdbus::Variant> loop_props_;
  bool has_drive_ = false;
  bool has_ata_ = false;
  bool has_loop_ = false;

  std::string media_;
  std::string model_;
  std::string serial_;
  std::string vendor_;
  uint64_t size_ = 0;

  std::vector<std::shared_ptr<Block>> blocks_;
};

}
