#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace sdbus {
class IConnection;
}

namespace eh::bt {

enum class BluetoothPairingKind : std::uint8_t {
  None,
  PinCode,
  Passkey,
  DisplayPinCode,
  DisplayPasskey,
  Confirm,
  Authorize,
  AuthorizeService,
};

struct BluetoothPairingRequest {
  BluetoothPairingKind kind = BluetoothPairingKind::None;
  std::string devicePath;
  std::string deviceAlias;
  std::uint32_t passkey = 0;
  std::uint16_t entered = 0;
  std::string pin;
  std::string uuid;
};

class BluetoothAgent {
public:
  using RequestCallback = std::function<void(const BluetoothPairingRequest&)>;

  explicit BluetoothAgent(sdbus::IConnection& bus);
  ~BluetoothAgent();

  BluetoothAgent(const BluetoothAgent&) = delete;
  BluetoothAgent& operator=(const BluetoothAgent&) = delete;
  BluetoothAgent(BluetoothAgent&&) = delete;
  BluetoothAgent& operator=(BluetoothAgent&&) = delete;

  void setRequestCallback(RequestCallback callback);

  void acceptConfirm();
  void rejectConfirm();
  void submitPin(const std::string& pin);
  void submitPasskey(std::uint32_t passkey);
  void cancelPending();

  [[nodiscard]] bool hasPendingRequest() const noexcept;
  [[nodiscard]] BluetoothPairingRequest pendingRequest() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}
