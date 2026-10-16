#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace sdbus {
class IConnection;
}

namespace eh::bt {

// BlueZ org.bluez.Agent1 request flavors the agent knows how to answer.
enum class PairingMode : std::uint8_t {
  Authorization,
  ServiceAuthorization,
  Confirmation,
  ShownPasskey,
  ShownPin,
  PasskeyEntry,
  PinEntry,
};

// One in-flight pairing interaction, as reported by BlueZ.
struct PairingPrompt {
  std::string uuid;
  std::string pin;
  std::uint16_t entered = 0;
  std::uint32_t passkey = 0;
  std::string deviceAlias;
  std::string devicePath;
  PairingMode mode = PairingMode::Authorization;
};

// Registers an org.bluez.Agent1 on the system bus and forwards pairing
// interactions to the UI through a single-slot callback.
class BluetoothAgent {
public:
  using PairingHandler = std::function<void(const PairingPrompt&)>;

  explicit BluetoothAgent(sdbus::IConnection& bus);
  ~BluetoothAgent();

  BluetoothAgent(const BluetoothAgent&) = delete;
  BluetoothAgent& operator=(const BluetoothAgent&) = delete;
  BluetoothAgent(BluetoothAgent&&) = delete;
  BluetoothAgent& operator=(BluetoothAgent&&) = delete;

  [[nodiscard]] PairingPrompt pendingRequest() const;

  void abortPending();
  void sendPasskey(std::uint32_t passkey);
  void sendPin(const std::string& pin);
  void rejectPairing();
  void acceptPairing();
  void setPairingHandler(PairingHandler handler);

  [[nodiscard]] bool hasPendingPairing() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}
