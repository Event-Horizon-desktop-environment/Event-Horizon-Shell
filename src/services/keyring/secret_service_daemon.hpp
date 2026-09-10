#pragma once

#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IObject.h>
#include <sdbus-c++/Types.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace eh::keyring {

struct ItemData {
  std::string label;
  std::map<std::string, std::string> attributes;
  std::vector<uint8_t> secret;
};

class SecretServiceDaemon {
public:
  enum class State {
    Uninitialized,  // no master key exists yet
    Locked,         // master key exists but not unlocked
    Unlocked,       // ready to use
  };

  static SecretServiceDaemon& instance();
  bool start();
  void stop();

  // Keyring lifecycle.

  State state() const;

  // First-time setup: create keyring with a password.
  // Returns true on success. Stores the encrypted (empty) DB + salt.
  bool create_keyring(const std::string& password);

  // Unlock with existing password. Derives key from password + stored salt,
  // tries to decrypt DB. If GCM auth passes, key is cached and state → Unlocked.
  bool unlock_keyring(const std::string& password);

  // Lock the keyring (clear cached key). Items remain in memory for the
  // remainder of the process lifetime but can no longer be persisted.
  void lock_keyring();

  // Change the keyring password.
  bool change_keyring_password(const std::string& old_password, const std::string& new_password);

  // WiFi helpers — thread-safe, require the Unlocked state.

  void store_wifi_password(const std::string& ssid, const std::string& password);
  std::optional<std::string> lookup_wifi_password(const std::string& ssid);

private:
  SecretServiceDaemon();
  ~SecretServiceDaemon();
  SecretServiceDaemon(const SecretServiceDaemon&) = delete;
  SecretServiceDaemon& operator=(const SecretServiceDaemon&) = delete;
  SecretServiceDaemon(SecretServiceDaemon&&) = delete;
  SecretServiceDaemon& operator=(SecretServiceDaemon&&) = delete;

  // D-Bus service
  std::unique_ptr<sdbus::IConnection> bus_;
  std::unique_ptr<sdbus::IObject> serviceObj_;
  std::unique_ptr<sdbus::IObject> collectionObj_;

  // State
  mutable std::mutex mutex_;
  State state_ = State::Uninitialized;
  std::vector<uint8_t> masterKey_;     // cached after unlock
  std::vector<uint8_t> salt_;           // loaded from DB file header

  // File paths
  std::string dataDir_;
  std::string dbPath_;

  // In-memory cache (always available after unlock or load)
  std::map<std::string, ItemData> items_;
  std::map<std::string, std::unique_ptr<sdbus::IObject>> sessions_;

  // Helpers
  void ensureDirsExist();
  void detectState();
  std::vector<uint8_t> deriveKey(const std::string& password, const std::vector<uint8_t>& salt) const;
  std::vector<uint8_t> aes256GcmEncrypt(const std::vector<uint8_t>& plaintext, const std::vector<uint8_t>& key);
  std::optional<std::vector<uint8_t>> aes256GcmDecrypt(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key);
  void saveDb();
  void saveDbWithKey(const std::vector<uint8_t>& key, const std::vector<uint8_t>& salt);
  std::string generateId();

  // D-Bus
  void registerServiceInterface();
  void registerCollectionInterface(sdbus::IObject& obj);
};

} // namespace eh::keyring
