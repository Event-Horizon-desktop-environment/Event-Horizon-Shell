#include "services/keyring/secret_service_daemon.hpp"

#include "desktop_shell/common/log/verbose_log.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>

namespace eh::keyring {

namespace {

constexpr auto kServiceName = "org.freedesktop.Secret.Service";
constexpr auto kServicePath = "/org/freedesktop/Secrets";
constexpr auto kServiceInterface = "org.freedesktop.Secret.Service";
constexpr auto kCollectionInterface = "org.freedesktop.Secret.Collection";
constexpr auto kItemInterface = "org.freedesktop.Secret.Item";
constexpr auto kSessionInterface = "org.freedesktop.Secret.Session";

constexpr auto kLoginCollectionPath = "/org/freedesktop/secrets/collection/login";
constexpr auto kLoginCollectionLabel = "Login";
constexpr auto kDefaultAlias = "default";

constexpr auto kKeyringDir = "keyrings";
constexpr auto kDbFileName = "login.keyring";

constexpr int kAesGcmKeyLen = 32;
constexpr int kAesGcmIvLen = 12;
constexpr int kAesGcmTagLen = 16;
constexpr int kSaltLen = 16;
constexpr int kPbkdf2Iterations = 100000;

struct EvpCipherCtxDeleter {
  void operator()(EVP_CIPHER_CTX* p) const noexcept { EVP_CIPHER_CTX_free(p); }
};
using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, EvpCipherCtxDeleter>;

std::string bytesToHex(const std::vector<uint8_t>& data) {
   
  std::ostringstream oss;
  for (auto b : data) oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
  return oss.str();
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
   
  std::vector<uint8_t> bytes;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    unsigned int byte;
    if (std::sscanf(hex.c_str() + i, "%2x", &byte) == 1) {
      bytes.push_back(static_cast<uint8_t>(byte));
    }
  }
  return bytes;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
   
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return false;
  auto sz = static_cast<size_t>(in.tellg());
  in.seekg(0);
  out.resize(sz);
  return in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz)).good();
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
   
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

} // anonymous namespace

// Singleton accessor.

SecretServiceDaemon& SecretServiceDaemon::instance() {
   
  static SecretServiceDaemon inst;
  return inst;
}

SecretServiceDaemon::SecretServiceDaemon() {
   
  detectState();
}

SecretServiceDaemon::~SecretServiceDaemon() {
   
  MANGOWM_INFO("{}", __func__);
  stop();
}

// Public lifecycle.

bool SecretServiceDaemon::start() {
    
  try {
    ensureDirsExist();
    bus_ = sdbus::createSessionBusConnection();

    // Check if another Secret Service provider is already running.
    // Claiming org.freedesktop.Secret.Service when another provider owns it would break
    // xdg-desktop-portal and other applications that depend on the existing keyring.
    {
      auto daemonProxy = sdbus::createProxy(*bus_,
          sdbus::ServiceName{"org.freedesktop.DBus"},
          sdbus::ObjectPath{"/org/freedesktop/DBus"});

      bool alreadyRunning = false;
      try {
        daemonProxy->callMethod("NameHasOwner")
            .onInterface("org.freedesktop.DBus")
            .withArguments(std::string{kServiceName})
            .storeResultsTo(alreadyRunning);
      } catch (const sdbus::Error&) {
        // NameHasOwner failed — assume no one owns it
      }

      if (alreadyRunning) {
        std::cerr << "[keyring] Secret Service already running — deferring to existing provider\n";
        bus_.reset();
        return false;
      }
    }

    serviceObj_ = sdbus::createObject(*bus_, sdbus::ObjectPath{kServicePath});
    registerServiceInterface();
    bus_->requestName(sdbus::ServiceName{kServiceName});

    std::cerr << "[keyring] Secret Service daemon started on session bus\n";
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[keyring] Failed to start Secret Service daemon: " << e.what() << "\n";
    return false;
  }
}

void SecretServiceDaemon::stop() {
   
  serviceObj_.reset();
  collectionObj_.reset();
  sessions_.clear();
  bus_.reset();
}

SecretServiceDaemon::State SecretServiceDaemon::state() const {
   
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

bool SecretServiceDaemon::create_keyring(const std::string& password) {
   
  std::lock_guard<std::mutex> lock(mutex_);
  ensureDirsExist();

  // Check if DB already exists
  struct stat st{};
  if (::stat(dbPath_.c_str(), &st) == 0 && st.st_size > 0) {
    std::cerr << "[keyring] Keyring already exists, refusing to create\n";
    return false;
  }

  // Generate random salt
  salt_.resize(kSaltLen);
  if (RAND_bytes(salt_.data(), static_cast<int>(salt_.size())) != 1) {
    std::cerr << "[keyring] Failed to generate salt\n";
    return false;
  }

  // Derive key
  masterKey_ = deriveKey(password, salt_);

  // Write empty DB file with salt prepended
  std::vector<uint8_t> db;
  db.insert(db.end(), salt_.begin(), salt_.end());
  // Encrypt empty JSON "{}"
  std::string emptyJson = "{}";
  auto encrypted = aes256GcmEncrypt(
      std::vector<uint8_t>(emptyJson.begin(), emptyJson.end()), masterKey_);
  db.insert(db.end(), encrypted.begin(), encrypted.end());
  writeFile(dbPath_, db);

  items_.clear();
  state_ = State::Unlocked;
  std::cerr << "[keyring] Keyring created and unlocked\n";
  return true;
}

bool SecretServiceDaemon::unlock_keyring(const std::string& password) {
   
  std::lock_guard<std::mutex> lock(mutex_);
  ensureDirsExist();

  // Read DB file (contains salt + encrypted payload)
  std::vector<uint8_t> db;
  if (!readFile(dbPath_, db) || db.size() < static_cast<size_t>(kSaltLen + kAesGcmIvLen + kAesGcmTagLen + 1)) {
    std::cerr << "[keyring] Keyring file missing or too small\n";
    return false;
  }

  // Extract salt
  salt_.assign(db.begin(), db.begin() + kSaltLen);

  // Derive key from provided password
  auto key = deriveKey(password, salt_);

  // Try to decrypt — extract encrypted portion (after salt)
  std::vector<uint8_t> encrypted(db.begin() + kSaltLen, db.end());
  auto decrypted = aes256GcmDecrypt(encrypted, key);
  if (!decrypted) {
    std::cerr << "[keyring] Wrong password or corrupted keyring\n";
    return false;
  }

  // Parse JSON
  try {
    auto json = nlohmann::json::parse(std::string(decrypted->begin(), decrypted->end()));
    items_.clear();
    for (const auto& [id, entry] : json.items()) {
      ItemData data;
      data.label = entry.value("label", "");
      data.secret = hexToBytes(entry.value("secret", ""));
      for (const auto& [k, v] : entry["attributes"].items()) {
        data.attributes[k] = v;
      }
      items_[id] = std::move(data);
    }
  } catch (const std::exception& e) {
    std::cerr << "[keyring] Failed to parse keyring: " << e.what() << "\n";
    return false;
  }

  masterKey_ = std::move(key);
  state_ = State::Unlocked;
  std::cerr << "[keyring] Keyring unlocked (" << items_.size() << " items)\n";
  return true;
}

void SecretServiceDaemon::lock_keyring() {
   
  std::lock_guard<std::mutex> lock(mutex_);
  masterKey_.clear();
  salt_.clear();
  state_ = State::Locked;
  std::cerr << "[keyring] Keyring locked\n";
}

bool SecretServiceDaemon::change_keyring_password(const std::string& /*old_password*/, const std::string& new_password) {
   
  // Must be unlocked with the old password first
  if (state() != State::Unlocked) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != State::Unlocked) return false;

  // Verify old password by checking the stored salt
  // (We already have masterKey_ cached, so we just re-save with new key)

  // Generate new salt
  std::vector<uint8_t> newSalt(kSaltLen);
  if (RAND_bytes(newSalt.data(), static_cast<int>(newSalt.size())) != 1) return false;

  auto newKey = deriveKey(new_password, newSalt);

  // Re-encrypt existing items with new key
  saveDbWithKey(newKey, newSalt);

  masterKey_ = std::move(newKey);
  salt_ = std::move(newSalt);
  std::cerr << "[keyring] Keyring password changed\n";
  return true;
}

// WiFi helpers.

void SecretServiceDaemon::store_wifi_password(const std::string& ssid, const std::string& password) {
   
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != State::Unlocked) {
    std::cerr << "[keyring] Cannot store: keyring not unlocked\n";
    return;
  }

  std::string existingId;
  for (const auto& [id, data] : items_) {
    auto svcIt = data.attributes.find("service");
    auto ssidIt = data.attributes.find("ssid");
    if (svcIt != data.attributes.end() && svcIt->second == "wifi" &&
        ssidIt != data.attributes.end() && ssidIt->second == ssid) {
      existingId = id;
      break;
    }
  }

  auto secret = std::vector<uint8_t>(password.begin(), password.end());
  if (!existingId.empty()) {
    items_[existingId].secret = secret;
  } else {
    ItemData data;
    data.label = "WiFi: " + ssid;
    data.attributes["service"] = "wifi";
    data.attributes["ssid"] = ssid;
    data.secret = secret;
    items_[generateId()] = data;
  }
  saveDb();
}

std::optional<std::string> SecretServiceDaemon::lookup_wifi_password(const std::string& ssid) {
   
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != State::Unlocked) return std::nullopt;

  for (const auto& [id, data] : items_) {
    auto svcIt = data.attributes.find("service");
    auto ssidIt = data.attributes.find("ssid");
    if (svcIt != data.attributes.end() && svcIt->second == "wifi" &&
        ssidIt != data.attributes.end() && ssidIt->second == ssid) {
      return std::string(data.secret.begin(), data.secret.end());
    }
  }
  return std::nullopt;
}

// Private helpers.

void SecretServiceDaemon::ensureDirsExist() {
   
  const char* home = std::getenv("HOME");
  if (!home) throw std::runtime_error("HOME not set");
  dataDir_ = std::string(home) + "/.local/share/event-horizon/" + kKeyringDir;
  dbPath_ = dataDir_ + "/" + kDbFileName;

  std::string path = dataDir_;
  size_t pos = 0;
  while ((pos = path.find('/', pos)) != std::string::npos) {
    std::string sub = path.substr(0, pos);
    if (!sub.empty()) {
      struct stat st{};
      if (::stat(sub.c_str(), &st) != 0) {
        if (::mkdir(sub.c_str(), 0700) != 0 && errno != EEXIST) {
          throw std::system_error(errno, std::generic_category(), "mkdir " + sub);
        }
      }
    }
    ++pos;
  }
  struct stat st{};
  if (::stat(dataDir_.c_str(), &st) != 0) {
    if (::mkdir(dataDir_.c_str(), 0700) != 0 && errno != EEXIST) {
      throw std::system_error(errno, std::generic_category(), "mkdir " + dataDir_);
    }
  }
}

void SecretServiceDaemon::detectState() {
   
  try {
    ensureDirsExist();
  } catch (const std::exception&) {
    state_ = State::Uninitialized;
    return;
  }
  struct stat st{};
  if (::stat(dbPath_.c_str(), &st) == 0 && st.st_size > 0) {
    state_ = State::Locked;
  } else {
    state_ = State::Uninitialized;
  }
}

std::vector<uint8_t> SecretServiceDaemon::deriveKey(const std::string& password,
                                                     const std::vector<uint8_t>& salt) const {
   
  std::vector<uint8_t> key(kAesGcmKeyLen);
  if (PKCS5_PBKDF2_HMAC_SHA1(
          password.c_str(), static_cast<int>(password.size()),
          salt.data(), static_cast<int>(salt.size()),
          kPbkdf2Iterations,
          static_cast<int>(key.size()),
          key.data()) != 1) {
     
    throw std::runtime_error("PBKDF2 key derivation failed");
  }
  return key;
}

std::vector<uint8_t> SecretServiceDaemon::aes256GcmEncrypt(const std::vector<uint8_t>& plaintext,
                                                            const std::vector<uint8_t>& key) {
   
  std::vector<uint8_t> iv(kAesGcmIvLen);
  if (RAND_bytes(iv.data(), static_cast<int>(iv.size())) != 1) {
    throw std::runtime_error("Failed to generate IV");
  }

  EvpCipherCtxPtr ctx{EVP_CIPHER_CTX_new()};
  if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

  if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), iv.data()) != 1) {
    throw std::runtime_error("EVP_EncryptInit_ex failed");
  }

  std::vector<uint8_t> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
  int outlen = 0;
  if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &outlen, plaintext.data(),
                        static_cast<int>(plaintext.size())) != 1) {
     
    throw std::runtime_error("EVP_EncryptUpdate failed");
  }
  int totalLen = outlen;

  if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + totalLen, &outlen) != 1) {
    throw std::runtime_error("EVP_EncryptFinal_ex failed");
  }
  totalLen += outlen;
  ciphertext.resize(static_cast<size_t>(totalLen));

  std::vector<uint8_t> tag(kAesGcmTagLen);
  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) != 1) {
    throw std::runtime_error("EVP_CTRL_GCM_GET_TAG failed");
  }

  std::vector<uint8_t> result;
  result.reserve(iv.size() + ciphertext.size() + tag.size());
  result.insert(result.end(), iv.begin(), iv.end());
  result.insert(result.end(), ciphertext.begin(), ciphertext.end());
  result.insert(result.end(), tag.begin(), tag.end());
  return result;
}

std::optional<std::vector<uint8_t>> SecretServiceDaemon::aes256GcmDecrypt(
    const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& key) {
   
  if (ciphertext.size() < static_cast<size_t>(kAesGcmIvLen + kAesGcmTagLen)) {
    return std::nullopt;
  }

  const auto* iv = ciphertext.data();
  const auto* tag = ciphertext.data() + ciphertext.size() - kAesGcmTagLen;
  const auto* enc = iv + kAesGcmIvLen;
  auto encLen = ciphertext.size() - kAesGcmIvLen - kAesGcmTagLen;

  EvpCipherCtxPtr ctx{EVP_CIPHER_CTX_new()};
  if (!ctx) return std::nullopt;

  if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), iv) != 1) {
    return std::nullopt;
  }

  std::vector<uint8_t> plaintext(encLen + EVP_MAX_BLOCK_LENGTH);
  int outlen = 0;
  if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &outlen, enc, static_cast<int>(encLen)) != 1) {
    return std::nullopt;
  }
  int totalLen = outlen;

  if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, kAesGcmTagLen,
                          const_cast<uint8_t*>(tag)) != 1) {
     
    return std::nullopt;
  }

  if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + totalLen, &outlen) != 1) {
    return std::nullopt;
  }
  totalLen += outlen;
  plaintext.resize(static_cast<size_t>(totalLen));
  return plaintext;
}

void SecretServiceDaemon::saveDb() {
   
  if (masterKey_.empty()) return;
  saveDbWithKey(masterKey_, salt_);
}

void SecretServiceDaemon::saveDbWithKey(const std::vector<uint8_t>& key, const std::vector<uint8_t>& salt) {
   
  nlohmann::json json;
  for (const auto& [id, data] : items_) {
    nlohmann::json entry;
    entry["label"] = data.label;
    entry["secret"] = bytesToHex(data.secret);
    entry["attributes"] = nlohmann::json::object();
    for (const auto& [k, v] : data.attributes) {
      entry["attributes"][k] = v;
    }
    json[id] = std::move(entry);
  }

  auto plaintext = json.dump(2);
  auto encrypted = aes256GcmEncrypt(
      std::vector<uint8_t>(plaintext.begin(), plaintext.end()), key);

  std::vector<uint8_t> db;
  db.reserve(salt.size() + encrypted.size());
  db.insert(db.end(), salt.begin(), salt.end());
  db.insert(db.end(), encrypted.begin(), encrypted.end());

  writeFile(dbPath_, db);
}

std::string SecretServiceDaemon::generateId() {
   
  std::vector<uint8_t> bytes(16);
  RAND_bytes(bytes.data(), static_cast<int>(bytes.size()));
  return bytesToHex(bytes);
}

// D-Bus interface registration.

void SecretServiceDaemon::registerServiceInterface() {
   
  serviceObj_->addVTable(
      sdbus::registerMethod("OpenSession")
          .withInputParamNames("algorithm", "input")
          .withOutputParamNames("output", "session")
          .implementedAs([this](const std::string& algorithm, const sdbus::Variant& input) {
            if (algorithm != "plain") {
              throw sdbus::Error(sdbus::Error::Name{"org.freedesktop.Secret.Error.NotSupported"},
                                 "only 'plain' algorithm supported");
            }
            auto sessId = generateId();
            auto sessPath = sdbus::ObjectPath{"/org/freedesktop/secrets/session/" + sessId};
            auto sessObj = sdbus::createObject(*bus_, sessPath);
            sessObj->addVTable(
                sdbus::registerMethod("Close")
                    .implementedAs([this, sessId]() {
                      sessions_.erase(sessId);
                    })
            ).forInterface(kSessionInterface);
            sessions_[sessId] = std::move(sessObj);
            return std::make_tuple(input, sessPath);
          }),
      sdbus::registerMethod("CreateCollection")
          .withInputParamNames("properties", "alias")
          .withOutputParamNames("collection", "prompt")
          .implementedAs([this](const std::map<std::string, sdbus::Variant>& /*properties*/,
                                const std::string& alias) {
             
            std::lock_guard<std::mutex> lock(mutex_);
            auto collPath = sdbus::ObjectPath{kLoginCollectionPath};
            if (!alias.empty() && alias != kDefaultAlias) {
              std::cerr << "[keyring] CreateCollection: alias '" << alias << "' ignored\n";
            }
            return std::make_tuple(collPath, sdbus::ObjectPath{"/"});
          }),
      sdbus::registerMethod("SearchItems")
          .withInputParamNames("attributes")
          .withOutputParamNames("unlocked", "locked")
          .implementedAs([this](const std::map<std::string, std::string>& attributes) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<sdbus::ObjectPath> unlocked;
            std::vector<sdbus::ObjectPath> locked;
            for (const auto& [id, data] : items_) {
              bool match = true;
              for (const auto& [attrKey, attrVal] : attributes) {
                auto it = data.attributes.find(attrKey);
                if (it == data.attributes.end() || it->second != attrVal) {
                  match = false;
                  break;
                }
              }
              if (match) {
                unlocked.emplace_back("/org/freedesktop/secrets/item/" + id);
              }
            }
            return std::make_tuple(unlocked, locked);
          }),
      sdbus::registerMethod("Unlock")
          .withInputParamNames("objects")
          .withOutputParamNames("unlocked", "prompt")
          .implementedAs([](const std::vector<sdbus::ObjectPath>& objects) {
            return std::make_tuple(objects, sdbus::ObjectPath{"/"});
          }),
      sdbus::registerMethod("Lock")
          .withInputParamNames("objects")
          .withOutputParamNames("locked", "prompt")
          .implementedAs([](const std::vector<sdbus::ObjectPath>& objects) {
            return std::make_tuple(objects, sdbus::ObjectPath{"/"});
          }),
      sdbus::registerMethod("GetSecrets")
          .withInputParamNames("items", "session")
          .withOutputParamNames("secrets")
          .implementedAs([this](const std::vector<sdbus::ObjectPath>& items,
                                 const sdbus::ObjectPath& /*session*/) {
             
            std::lock_guard<std::mutex> lock(mutex_);
            std::map<sdbus::ObjectPath,
                     std::tuple<sdbus::ObjectPath, std::vector<uint8_t>,
                                std::vector<uint8_t>, std::string>> result;
            for (const auto& itemPath : items) {
              auto pathStr = std::string(itemPath);
              auto pos = pathStr.rfind('/');
              if (pos == std::string::npos) continue;
              auto id = pathStr.substr(pos + 1);
              auto it = items_.find(id);
              if (it == items_.end()) continue;
              result[itemPath] = std::make_tuple(
                  sdbus::ObjectPath{"/org/freedesktop/secrets/session/"},
                  std::vector<uint8_t>{},
                  it->second.secret,
                  std::string{"text/plain; charset=utf8"}
              );
            }
            return result;
          }),
      sdbus::registerMethod("ReadAlias")
          .withInputParamNames("name")
          .withOutputParamNames("collection")
          .implementedAs([this](const std::string& name) {
            if (name == kDefaultAlias) {
              return sdbus::ObjectPath{kLoginCollectionPath};
            }
            return sdbus::ObjectPath{"/"};
          }),
      sdbus::registerMethod("SetAlias")
          .withInputParamNames("name", "collection")
          .implementedAs([](const std::string& /*name*/,
                            const sdbus::ObjectPath& /*collection*/) {
           
          })
  ).forInterface(kServiceInterface);

  collectionObj_ = sdbus::createObject(*bus_, sdbus::ObjectPath{kLoginCollectionPath});
  registerCollectionInterface(*collectionObj_);
}

void SecretServiceDaemon::registerCollectionInterface(sdbus::IObject& obj) {
   
  obj.addVTable(
      sdbus::registerMethod("Delete")
          .withOutputParamNames("prompt")
          .implementedAs([]() {
            throw sdbus::Error(sdbus::Error::Name{"org.freedesktop.Secret.Error.NotSupported"},
                               "cannot delete the login collection");
          }),
      sdbus::registerMethod("SearchItems")
          .withInputParamNames("attributes")
          .withOutputParamNames("items")
          .implementedAs([this](const std::map<std::string, std::string>& attributes) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<sdbus::ObjectPath> result;
            for (const auto& [id, data] : items_) {
              bool match = true;
              for (const auto& [attrKey, attrVal] : attributes) {
                auto it = data.attributes.find(attrKey);
                if (it == data.attributes.end() || it->second != attrVal) {
                  match = false;
                  break;
                }
              }
              if (match) {
                result.emplace_back("/org/freedesktop/secrets/item/" + id);
              }
            }
            return result;
          }),
      sdbus::registerMethod("CreateItem")
          .withInputParamNames("properties", "secret", "replace")
          .withOutputParamNames("item", "prompt")
          .implementedAs([this](const std::map<std::string, sdbus::Variant>& properties,
                                 const std::tuple<sdbus::ObjectPath, std::vector<uint8_t>,
                                                   std::vector<uint8_t>, std::string>& secret,
                                 bool replace) {
             
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != State::Unlocked) {
              throw sdbus::Error(sdbus::Error::Name{"org.freedesktop.Secret.Error.IsLocked"},
                                 "keyring is locked");
            }
            const auto& secretValue = std::get<2>(secret);
            std::string label;
            {
              auto it = properties.find("org.freedesktop.Secret.Item.Label");
              if (it != properties.end()) {
                label = it->second.template get<std::string>();
              }
            }
            std::map<std::string, std::string> attributes;
            {
              auto it = properties.find("org.freedesktop.Secret.Item.Attributes");
              if (it != properties.end()) {
                attributes = it->second.template get<std::map<std::string, std::string>>();
              }
            }
            if (replace) {
              for (auto& [id, data] : items_) {
                bool match = !attributes.empty();
                for (const auto& [ak, av] : attributes) {
                  auto dit = data.attributes.find(ak);
                  if (dit == data.attributes.end() || dit->second != av) {
                    match = false;
                    break;
                  }
                }
                if (match) {
                  data.secret = secretValue;
                  if (!label.empty()) data.label = label;
                  saveDb();
                  auto itemPath = sdbus::ObjectPath{"/org/freedesktop/secrets/item/" + id};
                  return std::make_tuple(itemPath, sdbus::ObjectPath{"/"});
                }
              }
            }
            auto id = generateId();
            ItemData data;
            data.label = label;
            data.attributes = attributes;
            data.secret = secretValue;
            items_[id] = std::move(data);
            saveDb();
            auto itemPath = sdbus::ObjectPath{"/org/freedesktop/secrets/item/" + id};
            return std::make_tuple(itemPath, sdbus::ObjectPath{"/"});
          }),
      sdbus::registerProperty("Label")
          .withGetter([]() { return std::string{kLoginCollectionLabel}; }),
      sdbus::registerProperty("Locked")
          .withGetter([this]() -> bool {
            std::lock_guard<std::mutex> lock(mutex_);
            return state_ != State::Unlocked;
          }),
      sdbus::registerProperty("Created")
          .withGetter([]() -> uint64_t { return 0; }),
      sdbus::registerProperty("Modified")
          .withGetter([]() -> uint64_t { return 0; }),
      sdbus::registerProperty("ItemCount")
          .withGetter([this]() -> uint32_t {
            std::lock_guard<std::mutex> lock(mutex_);
            return static_cast<uint32_t>(items_.size());
          }),
      sdbus::registerProperty("Items")
          .withGetter([this]() -> std::vector<sdbus::ObjectPath> {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<sdbus::ObjectPath> result;
            for (const auto& [id, _] : items_) {
              result.emplace_back("/org/freedesktop/secrets/item/" + id);
            }
            return result;
          })
  ).forInterface(kCollectionInterface);
}

} // namespace eh::keyring
