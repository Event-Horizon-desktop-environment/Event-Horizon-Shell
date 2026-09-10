#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "ux/disks/feature.h"

namespace eh::disks {

class Item;
using ItemPtr = std::shared_ptr<Item>;

class Item {
public:
  virtual ~Item() = default;

  virtual std::string get_description() const = 0;
  virtual std::string get_partition_type() const = 0;
  virtual uint64_t get_size() const = 0;
  virtual Feature get_features() = 0;
  virtual ItemPtr get_parent() const = 0;
  virtual std::vector<ItemPtr> get_children() const = 0;

  virtual std::string get_device_path() const { return {}; }
  virtual std::string get_object_path() const { return {}; }
  virtual bool is_drive() const { return false; }
  virtual bool is_block() const { return false; }

  void invalidate_features() { features_cached_ = false; }
  void invalidate_all() {
    features_cached_ = false;
    invalidated_ = true;
  }

protected:
  bool features_cached_ = false;
  Feature cached_features_ = FEATURE_NONE;
  bool invalidated_ = true;
};

}
