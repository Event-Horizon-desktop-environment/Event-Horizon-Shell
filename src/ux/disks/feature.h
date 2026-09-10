#pragma once

#include <cstdint>

namespace eh::disks {

using Feature = uint32_t;

constexpr Feature FEATURE_NONE              = 0;
constexpr Feature FEATURE_FORMAT            = 1u << 0;
constexpr Feature FEATURE_CREATE_IMAGE      = 1u << 1;
constexpr Feature FEATURE_RESTORE_IMAGE     = 1u << 2;
constexpr Feature FEATURE_CREATE_PARTITION  = 1u << 3;
constexpr Feature FEATURE_EDIT_PARTITION    = 1u << 4;
constexpr Feature FEATURE_DELETE_PARTITION  = 1u << 5;
constexpr Feature FEATURE_RESIZE_PARTITION  = 1u << 6;
constexpr Feature FEATURE_CHECK_FILESYSTEM  = 1u << 7;
constexpr Feature FEATURE_REPAIR_FILESYSTEM = 1u << 8;
constexpr Feature FEATURE_EDIT_LABEL        = 1u << 9;
constexpr Feature FEATURE_CAN_SWAPON        = 1u << 10;
constexpr Feature FEATURE_CAN_SWAPOFF       = 1u << 11;
constexpr Feature FEATURE_CAN_MOUNT         = 1u << 12;
constexpr Feature FEATURE_CAN_UNMOUNT       = 1u << 13;
constexpr Feature FEATURE_CAN_LOCK          = 1u << 14;
constexpr Feature FEATURE_CAN_UNLOCK        = 1u << 15;
constexpr Feature FEATURE_CHANGE_PASSPHRASE = 1u << 16;
constexpr Feature FEATURE_TAKE_OWNERSHIP    = 1u << 17;
constexpr Feature FEATURE_BENCHMARK         = 1u << 18;
constexpr Feature FEATURE_CONFIGURE_FSTAB   = 1u << 19;
constexpr Feature FEATURE_CONFIGURE_CRYPTTAB = 1u << 20;
constexpr Feature FEATURE_SMART             = 1u << 21;
constexpr Feature FEATURE_SETTINGS          = 1u << 22;
constexpr Feature FEATURE_STANDBY           = 1u << 23;
constexpr Feature FEATURE_WAKEUP            = 1u << 24;
constexpr Feature FEATURE_POWEROFF          = 1u << 25;
constexpr Feature FEATURE_EJECT             = 1u << 26;
constexpr Feature FEATURE_DETACH            = 1u << 27;

}
