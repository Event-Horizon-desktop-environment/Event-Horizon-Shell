#include "ux/disks/block.hpp"
#include "ux/disks/manager.hpp"
#include "ux/disks/drive.hpp"

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/IConnection.h>

#include <sys/statvfs.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <cstdio>

namespace eh::disks {

namespace {

constexpr auto kBlock = "org.freedesktop.UDisks2.Block";
constexpr auto kPartition = "org.freedesktop.UDisks2.Partition";
constexpr auto kFilesystem = "org.freedesktop.UDisks2.Filesystem";
constexpr auto kSwapspace = "org.freedesktop.UDisks2.Swapspace";
constexpr auto kEncrypted = "org.freedesktop.UDisks2.Encrypted";

template <typename T>
T get_or(const std::map<std::string, sdbus::Variant>& p,
         const char* key, T fallback) {
  auto it = p.find(key);
  if (it == p.end()) return fallback;
  try { return static_cast<T>(it->second); }
  catch (const sdbus::Error&) { return fallback; }
}

}

Block::Block(const std::string& object_path,
             const std::map<std::string,
               std::map<std::string, sdbus::Variant>>& ifaces,
             Manager* manager)
  : object_path_(object_path), manager_(manager) {

  for (const auto& [iface, props] : ifaces) {
    if (iface == kBlock) {
      has_block_ = true;
      block_props_ = props;
      size_ = get_or<uint64_t>(props, "Size", 0);
      auto dev_bytes = get_or<std::vector<uint8_t>>(props, "Device", {});
      device_ = bytes_to_string(dev_bytes);
    } else if (iface == kPartition) {
      has_partition_ = true;
      partition_props_ = props;
    } else if (iface == kFilesystem) {
      has_filesystem_ = true;
      fs_props_ = props;
    } else if (iface == kSwapspace) {
      has_swap_ = true;
      swap_props_ = props;
    } else if (iface == kEncrypted) {
      has_encrypted_ = true;
      encrypted_props_ = props;
    }
  }
}

std::string Block::bytes_to_string(const std::vector<uint8_t>& bytes) const {
  std::string s;
  s.reserve(bytes.size());
  for (uint8_t b : bytes) {
    if (b == 0) break;
    s.push_back(static_cast<char>(b));
  }
  return s;
}

bool Block::has_tool(const std::string& tool) const {
  std::string cmd = "which " + tool + " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

// Item interface.

std::string Block::get_description() const {
  std::string label = get_label();
  if (!label.empty()) return label;

  if (!device_.empty()) {
    auto pos = device_.find_last_of('/');
    if (pos != std::string::npos) return device_.substr(pos + 1);
    return device_;
  }

  return "Block Device";
}

std::string Block::get_partition_type() const {
  if (has_partition_) {
    return get_or<std::string>(partition_props_, "Type", "—");
  }
  return "—";
}

uint64_t Block::get_size() const {
  return size_;
}

Feature Block::get_features() {
  if (features_cached_) return cached_features_;
  compute_features();
  features_cached_ = true;
  return cached_features_;
}

void Block::compute_features() {
  Feature f = FEATURE_NONE;

  if (!has_block_) return;

  bool read_only = is_read_only();
  bool hint_partitionable = is_hint_partitionable();
  bool hint_ignore = is_hint_ignore();

  if (hint_ignore) {
    cached_features_ = FEATURE_NONE;
    return;
  }

  // Inherit drive-level features for image/benchmark
  f |= FEATURE_CREATE_IMAGE;
  f |= FEATURE_BENCHMARK;
  if (!read_only) {
    f |= FEATURE_RESTORE_IMAGE;
  }

  if (!read_only) {
    if (!has_partition_ || !is_partition_container()) {
      f |= FEATURE_FORMAT;
    }
    if (has_partition_) {
      f |= FEATURE_DELETE_PARTITION;
      f |= FEATURE_EDIT_PARTITION;
    }
  }

  // Format is available for block devices that can hold filesystems
  if (hint_partitionable && !read_only) {
    f |= FEATURE_FORMAT;
  }

  // Filesystem actions
  if (has_filesystem_) {
    auto mps = get_mount_points();
    if (!mps.empty()) {
      f |= FEATURE_CAN_UNMOUNT;
    } else {
      f |= FEATURE_CAN_MOUNT;
    }

    if (!read_only) {
      f |= FEATURE_EDIT_LABEL;
    }
    f |= FEATURE_CONFIGURE_FSTAB;

    // Resize / Check / Repair / Ownership
    std::string fstype = get_fstype();
    if (!read_only) {
      if (has_partition_) {
        bool can_resize = (fstype == "ext4" || fstype == "ext3" ||
                           fstype == "xfs" || fstype == "btrfs" ||
                           fstype == "ntfs");
        if (can_resize)
          f |= FEATURE_RESIZE_PARTITION;
      }
      if (has_tool("fsck." + fstype)) {
        f |= FEATURE_CHECK_FILESYSTEM;
        f |= FEATURE_REPAIR_FILESYSTEM;
      }
      if (fstype == "ext4" || fstype == "ext3" || fstype == "btrfs" ||
          fstype == "xfs") {
        f |= FEATURE_TAKE_OWNERSHIP;
      }
    }
  }

  // Swap actions
  if (has_swap_) {
    if (is_swap_active())
      f |= FEATURE_CAN_SWAPOFF;
    else
      f |= FEATURE_CAN_SWAPON;
    f |= FEATURE_CONFIGURE_FSTAB;
  }

  // Crypto/LUKS actions
  if (has_encrypted_) {
    if (has_cleartext())
      f |= FEATURE_CAN_LOCK;
    else
      f |= FEATURE_CAN_UNLOCK;

    if (is_luks()) {
      f |= FEATURE_CONFIGURE_CRYPTTAB;
      f |= FEATURE_CHANGE_PASSPHRASE;
    }
  }

  cached_features_ = f;
}

// Block accessors.

std::string Block::get_device() const {
  return device_;
}

std::string Block::get_device_path() const {
  return device_;
}

std::string Block::get_uuid() const {
  return get_or<std::string>(block_props_, "IdUUID", "");
}

std::string Block::get_label() const {
  return get_or<std::string>(block_props_, "IdLabel", "");
}

std::string Block::get_fstype() const {
  return get_or<std::string>(block_props_, "IdType", "");
}

std::string Block::get_fsusage() const {
  return get_or<std::string>(block_props_, "IdUsage", "");
}

std::string Block::get_fsversion() const {
  return get_or<std::string>(block_props_, "IdVersion", "");
}

bool Block::is_read_only() const {
  return get_or<bool>(block_props_, "ReadOnly", false);
}

bool Block::is_hint_partitionable() const {
  return get_or<bool>(block_props_, "HintPartitionable", false);
}

bool Block::is_hint_ignore() const {
  return get_or<bool>(block_props_, "HintIgnore", false);
}

// Partition.

bool Block::has_partition() const {
  return has_partition_;
}

uint64_t Block::get_partition_offset() const {
  return get_or<uint64_t>(partition_props_, "Offset", 0);
}

uint64_t Block::get_partition_number() const {
  return get_or<uint64_t>(partition_props_, "Number", 0);
}

std::string Block::get_partition_type_guid() const {
  return get_or<std::string>(partition_props_, "Type", "");
}

std::string Block::get_partition_name() const {
  return get_or<std::string>(partition_props_, "Name", "");
}

uint64_t Block::get_partition_flags() const {
  return get_or<uint64_t>(partition_props_, "Flags", 0);
}

bool Block::is_partition_container() const {
  return get_or<bool>(partition_props_, "IsContainer", false);
}

// Filesystem.

bool Block::has_filesystem() const {
  return has_filesystem_;
}

std::vector<std::string> Block::get_mount_points() const {
  std::vector<std::string> result;
  auto mps = get_or<std::vector<std::vector<uint8_t>>>(fs_props_,
      "MountPoints", {});
  for (const auto& mp_bytes : mps) {
    std::string mp = bytes_to_string(mp_bytes);
    if (!mp.empty()) result.push_back(std::move(mp));
  }
  return result;
}

bool Block::is_mounted() const {
  return !get_mount_points().empty();
}

// Swap.

bool Block::is_swap() const {
  return has_swap_;
}

bool Block::is_swap_active() const {
  return get_or<bool>(swap_props_, "Active", false);
}

// Crypto / LUKS.

bool Block::is_encrypted() const {
  return has_encrypted_ || get_fsusage() == "crypto";
}

bool Block::is_luks() const {
  return get_fstype() == "crypto_LUKS";
}

std::string Block::get_crypto_backing_device() const {
  return get_or<sdbus::ObjectPath>(block_props_, "CryptoBackingDevice", sdbus::ObjectPath{""});
}

void Block::set_cleartext(std::shared_ptr<Block> ct) {
  cleartext_ = std::move(ct);
}

void Block::set_backing_device(std::shared_ptr<Block> backing) {
  backing_device_ = std::move(backing);
}

ItemPtr Block::get_parent() const {
  return backing_device_;
}

// Async operations.

void Block::mount_async(const std::string& options,
                        std::function<void(bool, std::string)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false, ""); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    opts["options"] = sdbus::Variant(options);
    (*sp)->callMethodAsync("Mount")
        .onInterface(kFilesystem)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error,
                           std::string path) {
          if (cb) cb(!error.has_value(), error.has_value() ? "" : path);
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false, e.what());
  }
}

void Block::unmount_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Unmount")
        .onInterface(kFilesystem)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::format_async(const std::string& fstype, const std::string& label,
                         const std::string& passphrase,
                         std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    if (!label.empty()) opts["label"] = sdbus::Variant(label);
    if (!passphrase.empty()) {
      opts["encrypt.type"] = sdbus::Variant(std::string{"luks2"});
      opts["encrypt.passphrase"] = sdbus::Variant(passphrase);
    }
    (*sp)->callMethodAsync("Format")
        .onInterface(kBlock)
        .withArguments(fstype, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::set_label_async(const std::string& label,
                            std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("SetLabel")
        .onInterface(kFilesystem)
        .withArguments(label, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::set_partition_type_async(const std::string& type,
                                     std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("SetType")
        .onInterface(kPartition)
        .withArguments(type, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::set_partition_name_async(const std::string& name,
                                     std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("SetName")
        .onInterface(kPartition)
        .withArguments(name, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::create_partition_table_async(const std::string& scheme,
                                          std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Format")
        .onInterface(kBlock)
        .withArguments(scheme, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::create_partition_async(uint64_t offset, uint64_t size,
                                    const std::string& type,
                                    const std::string& name,
                                    std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    if (!name.empty()) opts["name"] = sdbus::Variant(name);
    (*sp)->callMethodAsync("CreatePartition")
        .onInterface("org.freedesktop.UDisks2.PartitionTable")
        .withArguments(sdbus::Variant{offset}, sdbus::Variant{size},
                       type, name, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::set_partition_flags_async(uint64_t flags,
                                      std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("SetFlags")
        .onInterface(kPartition)
        .withArguments(flags, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::delete_partition_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Delete")
        .onInterface(kPartition)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::resize_async(uint64_t new_size, std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Resize")
        .onInterface(kFilesystem)
        .withArguments(new_size, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::check_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Check")
        .onInterface(kFilesystem)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::repair_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Repair")
        .onInterface(kFilesystem)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::take_ownership_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("TakeOwnership")
        .onInterface(kFilesystem)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::swapon_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Start")
        .onInterface(kSwapspace)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::swapoff_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Stop")
        .onInterface(kSwapspace)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::unlock_async(const std::string& passphrase, bool,
                         std::function<void(bool, std::string)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false, ""); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Unlock")
        .onInterface(kEncrypted)
        .withArguments(passphrase, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error,
                           sdbus::ObjectPath result) {
          if (cb) cb(!error.has_value(),
                     error.has_value() ? "" : std::string{result});
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false, e.what());
  }
}

void Block::lock_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Lock")
        .onInterface(kEncrypted)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::change_passphrase_async(const std::string& old_pw,
                                    const std::string& new_pw,
                                    std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("ChangePassphrase")
        .onInterface(kEncrypted)
        .withArguments(old_pw, new_pw, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

void Block::add_fstab_entry_async(const std::string& mount_point,
                                  const std::string& fstype,
                                  const std::string& uuid,
                                  std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));

    std::string fsname = uuid.empty() ? "" : "UUID=" + uuid;
    std::map<std::string, sdbus::Variant> item;
    if (!fsname.empty()) {
      std::vector<uint8_t> fsname_bytes(fsname.begin(), fsname.end());
      item["fsname"] = sdbus::Variant(fsname_bytes);
    }
    std::vector<uint8_t> dir_bytes(mount_point.begin(), mount_point.end());
    item["dir"] = sdbus::Variant(dir_bytes);
    std::string type_str = fstype.empty() ? "auto" : fstype;
    std::vector<uint8_t> type_bytes(type_str.begin(), type_str.end());
    item["type"] = sdbus::Variant(type_bytes);
    std::string opts_str = "defaults";
    std::vector<uint8_t> opts_bytes(opts_str.begin(), opts_str.end());
    item["opts"] = sdbus::Variant(opts_bytes);
    item["freq"] = sdbus::Variant(int32_t{0});
    item["passno"] = sdbus::Variant(int32_t{0});

    auto fstab_item = std::make_tuple(std::string{"fstab"}, std::move(item));
    std::map<std::string, sdbus::Variant> empty_opts;
    (*sp)->callMethodAsync("AddConfigurationItem")
        .onInterface(kBlock)
        .withArguments(std::move(fstab_item), std::move(empty_opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    if (cb) cb(false);
  }
}

uint64_t Block::get_fs_total_bytes() const {
  auto mps = get_mount_points();
  if (mps.empty()) return 0;
  struct statvfs buf;
  if (statvfs(mps[0].c_str(), &buf) != 0) return 0;
  return static_cast<uint64_t>(buf.f_blocks) * buf.f_frsize;
}

uint64_t Block::get_fs_used_bytes() const {
  auto mps = get_mount_points();
  if (mps.empty()) return 0;
  struct statvfs buf;
  if (statvfs(mps[0].c_str(), &buf) != 0) return 0;
  return static_cast<uint64_t>(buf.f_blocks - buf.f_bfree) * buf.f_frsize;
}

uint64_t Block::get_fs_free_bytes() const {
  auto mps = get_mount_points();
  if (mps.empty()) return 0;
  struct statvfs buf;
  if (statvfs(mps[0].c_str(), &buf) != 0) return 0;
  return static_cast<uint64_t>(buf.f_bavail) * buf.f_frsize;
}

}
