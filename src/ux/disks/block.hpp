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

class Block : public Item, public std::enable_shared_from_this<Block> {
public:
  Block(const std::string& object_path,
        const std::map<std::string,
          std::map<std::string, sdbus::Variant>>& ifaces,
        Manager* manager);

  // Item interface
  std::string get_description() const override;
  std::string get_partition_type() const override;
  uint64_t get_size() const override;
  Feature get_features() override;
  ItemPtr get_parent() const override;
  std::vector<ItemPtr> get_children() const override { return {}; }
  std::string get_object_path() const override { return object_path_; }
  std::string get_device_path() const override;
  bool is_block() const override { return true; }

  // Block-specific accessors
  std::string get_device() const;
  std::string get_uuid() const;
  std::string get_label() const;
  std::string get_fstype() const;
  std::string get_fsusage() const;
  std::string get_fsversion() const;
  bool is_read_only() const;
  bool is_hint_partitionable() const;
  bool is_hint_ignore() const;

  // Partition info
  bool has_partition() const;
  uint64_t get_partition_offset() const;
  uint64_t get_partition_number() const;
  std::string get_partition_type_guid() const;
  std::string get_partition_name() const;
  uint64_t get_partition_flags() const;
  bool is_partition_container() const;

  // Filesystem
  bool has_filesystem() const;
  std::vector<std::string> get_mount_points() const;
  bool is_mounted() const;

  // Filesystem usage (statvfs on first mount point; all 0 if unmounted or fails)
  uint64_t get_fs_total_bytes() const;
  uint64_t get_fs_used_bytes() const;
  uint64_t get_fs_free_bytes() const;

  // Swap
  bool is_swap() const;
  bool is_swap_active() const;

  // Crypto / LUKS
  bool is_encrypted() const;
  bool is_luks() const;
  std::string get_crypto_backing_device() const;
  bool has_cleartext() const { return !!cleartext_; }
  std::shared_ptr<Block> cleartext() const { return cleartext_; }

  // Cleartext relationship
  void set_cleartext(std::shared_ptr<Block> ct);
  void set_backing_device(std::shared_ptr<Block> backing);

  // Async operations
  void mount_async(const std::string& options, std::function<void(bool, std::string)> cb);
  void unmount_async(std::function<void(bool)> cb);
  void format_async(const std::string& fstype, const std::string& label,
                    const std::string& passphrase,
                    std::function<void(bool)> cb);
  void set_label_async(const std::string& label, std::function<void(bool)> cb);
  void set_partition_type_async(const std::string& type,
                                std::function<void(bool)> cb);
  void set_partition_name_async(const std::string& name,
                                std::function<void(bool)> cb);
  void create_partition_table_async(const std::string& scheme,
                                    std::function<void(bool)> cb);
  void create_partition_async(uint64_t offset, uint64_t size,
                              const std::string& type,
                              const std::string& name,
                              std::function<void(bool)> cb);
  void set_partition_flags_async(uint64_t flags,
                                 std::function<void(bool)> cb);
  void delete_partition_async(std::function<void(bool)> cb);
  void resize_async(uint64_t new_size, std::function<void(bool)> cb);
  void check_async(std::function<void(bool)> cb);
  void repair_async(std::function<void(bool)> cb);
  void take_ownership_async(std::function<void(bool)> cb);
  void swapon_async(std::function<void(bool)> cb);
  void swapoff_async(std::function<void(bool)> cb);
  void unlock_async(const std::string& passphrase, bool store_in_keyring,
                    std::function<void(bool, std::string)> cb);
  void lock_async(std::function<void(bool)> cb);
  void change_passphrase_async(const std::string& old_pw,
                               const std::string& new_pw,
                               std::function<void(bool)> cb);
  void add_fstab_entry_async(const std::string& mount_point,
                             const std::string& fstype,
                             const std::string& uuid,
                             std::function<void(bool)> cb);

private:
  void compute_features();
  std::string bytes_to_string(const std::vector<uint8_t>& bytes) const;
  bool has_tool(const std::string& tool) const;

  std::string object_path_;
  Manager* manager_ = nullptr;

  // Property caches
  std::map<std::string, sdbus::Variant> block_props_;
  std::map<std::string, sdbus::Variant> partition_props_;
  std::map<std::string, sdbus::Variant> fs_props_;
  std::map<std::string, sdbus::Variant> swap_props_;
  std::map<std::string, sdbus::Variant> encrypted_props_;
  bool has_block_ = false;
  bool has_partition_ = false;
  bool has_filesystem_ = false;
  bool has_swap_ = false;
  bool has_encrypted_ = false;

  uint64_t size_ = 0;
  std::string device_;

  // Cleartext relationship (for LUKS)
  std::shared_ptr<Block> cleartext_;
  std::shared_ptr<Block> backing_device_;
};

}
