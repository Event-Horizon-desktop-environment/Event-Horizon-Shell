#include "ux/disks/drive.hpp"
#include "ux/disks/manager.hpp"
#include "ux/disks/block.hpp"

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/IConnection.h>

#include <algorithm>
#include <iostream>

namespace eh::disks {

namespace {

constexpr auto kDrive = "org.freedesktop.UDisks2.Drive";
constexpr auto kDriveAta = "org.freedesktop.UDisks2.Drive.Ata";
constexpr auto kLoop = "org.freedesktop.UDisks2.Loop";
constexpr auto kBlock = "org.freedesktop.UDisks2.Block";

template <typename T>
T get_or(const std::map<std::string, sdbus::Variant>& p,
         const char* key, T fallback) {
  auto it = p.find(key);
  if (it == p.end()) return fallback;
  try { return static_cast<T>(it->second); }
  catch (const sdbus::Error&) { return fallback; }
}

}

Drive::Drive(const std::string& object_path,
             const std::map<std::string,
               std::map<std::string, sdbus::Variant>>& ifaces,
             Manager* manager)
  : object_path_(object_path), manager_(manager) {

  for (const auto& [iface, props] : ifaces) {
    if (iface == kDrive) {
      has_drive_ = true;
      drive_props_ = props;
      model_ = get_or<std::string>(props, "Model", "");
      serial_ = get_or<std::string>(props, "Serial", "");
      vendor_ = get_or<std::string>(props, "Vendor", "");
      size_ = get_or<uint64_t>(props, "Size", 0);
      media_ = get_or<std::string>(props, "Media", "");
    } else if (iface == kDriveAta) {
      has_ata_ = true;
      ata_props_ = props;
    } else if (iface == kLoop) {
      has_loop_ = true;
      loop_props_ = props;
    }
  }
}

std::string Drive::get_description() const {
  if (!model_.empty()) return model_;
  if (!vendor_.empty()) return vendor_;
  return "Drive";
}

std::string Drive::get_partition_type() const {
  return "—";
}

uint64_t Drive::get_size() const {
  return size_;
}

Feature Drive::get_features() {
  if (features_cached_) return cached_features_;
  compute_features();
  features_cached_ = true;
  return cached_features_;
}

void Drive::compute_features() {
  Feature f = FEATURE_NONE;

  if (has_drive_) {
    bool ejectable = get_or<bool>(drive_props_, "Ejectable", false);
    bool removable = get_or<bool>(drive_props_, "MediaRemovable", false);
    bool can_power = get_or<bool>(drive_props_, "CanPowerOff", false);

    if (ejectable && removable) f |= FEATURE_EJECT;
    if (can_power) f |= FEATURE_POWEROFF;
  }

  if (has_loop_) {
    f |= FEATURE_DETACH;
  }

  if (has_ata_ && !get_or<bool>(drive_props_, "MediaRemovable", false)) {
    bool smart_supported = false;
    auto one_liner = smart_one_liner_assessment(nullptr);
    if (!one_liner.empty()) smart_supported = true;
    if (smart_supported) f |= FEATURE_SMART;
  }

  // Settings always available for non-removable drives
  if (has_drive_ && !get_or<bool>(drive_props_, "MediaRemovable", false)) {
    f |= FEATURE_SETTINGS;
  }

  if (size_ > 0) {
    f |= FEATURE_CREATE_IMAGE;
    f |= FEATURE_BENCHMARK;
    if (!get_or<bool>(drive_props_, "ReadOnly", false)) {
      f |= FEATURE_RESTORE_IMAGE;
      f |= FEATURE_CREATE_PARTITION;
      // Format is available if there's a block with HintPartitionable
      f |= FEATURE_FORMAT;
    }
  }

  cached_features_ = f;
}

// Drive accessors.

std::string Drive::get_model() const {
  return model_;
}

std::string Drive::get_serial() const {
  return serial_;
}

std::string Drive::get_vendor() const {
  return vendor_;
}

std::string Drive::get_wwn() const {
  return get_or<std::string>(drive_props_, "WWN", "");
}

std::string Drive::get_firmware_version() const {
  return get_or<std::string>(drive_props_, "Revision", "");
}

std::string Drive::get_media() const {
  return media_;
}

double Drive::get_rotation_rate() const {
  return static_cast<double>(get_or<uint64_t>(drive_props_, "RotationRate", 0));
}

bool Drive::is_removable() const {
  return get_or<bool>(drive_props_, "MediaRemovable", false);
}

bool Drive::is_ejectable() const {
  return get_or<bool>(drive_props_, "Ejectable", false);
}

bool Drive::can_power_off() const {
  return get_or<bool>(drive_props_, "CanPowerOff", false);
}

bool Drive::is_media_detected() const {
  return get_or<bool>(drive_props_, "MediaDetected", false);
}

// SMART.

bool Drive::smart_supported() const {
  return has_ata_ && get_or<bool>(ata_props_, "SmartEnabled", false);
}

bool Drive::smart_failing() const {
  return get_or<bool>(ata_props_, "SmartFailing", false);
}

int64_t Drive::smart_power_on_hours() const {
  auto attrs = get_or<std::vector<std::tuple<uint16_t, uint16_t, uint16_t,
      uint16_t, uint16_t, uint16_t, uint64_t>>>(ata_props_,
      "SmartAttributes", {});
  for (const auto& attr : attrs) {
    if (std::get<0>(attr) == 9) return static_cast<int64_t>(std::get<6>(attr));
  }
  return -1;
}

double Drive::smart_temperature() const {
  auto attrs = get_or<std::vector<std::tuple<uint16_t, uint16_t, uint16_t,
      uint16_t, uint16_t, uint16_t, uint64_t>>>(ata_props_,
      "SmartAttributes", {});
  for (const auto& attr : attrs) {
    if (std::get<0>(attr) == 190 || std::get<0>(attr) == 194) {
      return static_cast<double>(std::get<6>(attr));
    }
  }
  return -1;
}

std::string Drive::smart_one_liner_assessment(bool* out_warn) const {
  if (!has_ata_) {
    if (out_warn) *out_warn = false;
    return "";
  }

  bool smart_enabled = get_or<bool>(ata_props_, "SmartEnabled", false);
  bool smart_failing = get_or<bool>(ata_props_, "SmartFailing", false);
  int64_t power_on_hours = smart_power_on_hours();

  if (!smart_enabled) {
    if (out_warn) *out_warn = false;
    return "SMART not enabled";
  }

  if (smart_failing) {
    if (out_warn) *out_warn = true;
    return "Disk is failing";
  }

  if (out_warn) *out_warn = false;
  if (power_on_hours >= 0) {
    return "Disk is OK, " + std::to_string(power_on_hours) + " hours";
  }
  return "Disk is OK";
}

// Loop.

bool Drive::is_loop() const {
  return has_loop_;
}

std::string Drive::loop_file() const {
  if (!has_loop_) return "";
  auto file_bytes = get_or<std::vector<uint8_t>>(loop_props_, "File", {});
  std::string result;
  result.reserve(file_bytes.size());
  for (uint8_t b : file_bytes) {
    if (b == 0) break;
    result.push_back(static_cast<char>(b));
  }
  return result;
}

// Children.

void Drive::add_block(std::shared_ptr<Block> block) {
  blocks_.push_back(std::move(block));
}

std::vector<ItemPtr> Drive::get_children() const {
  std::vector<ItemPtr> children;
  for (const auto& b : blocks_) {
    children.push_back(b);
  }
  return children;
}

// Async operations.

void Drive::standby_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    opts["standby"] = sdbus::Variant(true);
    (*sp)->callMethodAsync("Eject")
        .onInterface(kDrive)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    std::cerr << "[drive] standby failed: " << e.what() << '\n';
    if (cb) cb(false);
  }
}

void Drive::wakeup_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    opts["wake-up"] = sdbus::Variant(true);
    (*sp)->callMethodAsync("Eject")
        .onInterface(kDrive)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    std::cerr << "[drive] wakeup failed: " << e.what() << '\n';
    if (cb) cb(false);
  }
}

void Drive::eject_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("Eject")
        .onInterface(kDrive)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    std::cerr << "[drive] eject failed: " << e.what() << '\n';
    if (cb) cb(false);
  }
}

void Drive::power_off_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("PowerOff")
        .onInterface(kDrive)
        .withArguments(std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    std::cerr << "[drive] power_off failed: " << e.what() << '\n';
    if (cb) cb(false);
  }
}

void Drive::detach_async(std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  manager_->close_loop_async(object_path_, std::move(cb));
}

void Drive::run_smart_test_async(const std::string& type,
                                  std::function<void(bool)> cb) {
  if (!manager_ || !manager_->bus()) { if (cb) cb(false); return; }
  try {
    auto dev = sdbus::createProxy(*manager_->bus(),
        sdbus::ServiceName{"org.freedesktop.UDisks2"},
        sdbus::ObjectPath{object_path_});
    auto sp = std::make_shared<
        std::unique_ptr<sdbus::IProxy>>(std::move(dev));
    std::map<std::string, sdbus::Variant> opts;
    (*sp)->callMethodAsync("SmartInitiateSelfTest")
        .onInterface(kDriveAta)
        .withArguments(type, std::move(opts))
        .uponReplyInvoke([sp, cb](std::optional<sdbus::Error> error) {
          if (cb) cb(!error.has_value());
        });
  } catch (const sdbus::Error& e) {
    std::cerr << "[drive] smart_test failed: " << e.what() << '\n';
    if (cb) cb(false);
  }
}

}
