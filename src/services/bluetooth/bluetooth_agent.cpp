#include "services/bluetooth/bluetooth_agent.hpp"

#include <iostream>
#include <optional>
#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IObject.h>
#include <sdbus-c++/MethodResult.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/VTableItems.h>
#include <utility>

namespace eh::bt {
namespace {

const sdbus::ServiceName kBluezBusName{"org.bluez"};
const sdbus::ObjectPath kBluezRoot{"/org/bluez"};
const sdbus::ObjectPath kAgentObjectPath{"/org/ehde/BluetoothAgent"};
constexpr auto kAgentInterface = "org.bluez.Agent1";
constexpr auto kAgentManagerInterface = "org.bluez.AgentManager1";
constexpr auto kCapability = "KeyboardDisplay";

const sdbus::Error::Name kErrRejected{"org.bluez.Error.Rejected"};
const sdbus::Error::Name kErrCanceled{"org.bluez.Error.Canceled"};

}

struct BluetoothAgent::Impl {
  sdbus::IConnection& bus;
  std::unique_ptr<sdbus::IObject> object;
  BluetoothAgent::RequestCallback requestCallback;

  BluetoothPairingRequest pending;
  std::optional<sdbus::Result<std::string>> pendingString;
  std::optional<sdbus::Result<std::uint32_t>> pendingUint;
  std::optional<sdbus::Result<>> pendingVoid;

  explicit Impl(sdbus::IConnection& b) : bus(b) {
     
  }

  [[nodiscard]] bool hasPending() const noexcept {
    return pendingString.has_value() || pendingUint.has_value() || pendingVoid.has_value();
  }

  void rejectIfBusy(auto& result, const char* what) {
     
    std::cerr << "[bluetooth] " << what << " while another request pending -> rejected" << std::endl;
    result.returnError(sdbus::Error{kErrRejected, "another pairing request is already pending"});
  }

  void clearPending() {
     
    pending = BluetoothPairingRequest{};
    pendingString.reset();
    pendingUint.reset();
    pendingVoid.reset();
  }

  void fireCallback() {
     
    if (requestCallback) {
      requestCallback(pending);
    } else {
      std::cerr << "[bluetooth] pairing request with no UI handler -> rejecting" << std::endl;
      cancelPending();
    }
  }

  void cancelPending() {
     
    if (pendingString) {
      pendingString->returnError(sdbus::Error{kErrCanceled, "user canceled"});
    }
    if (pendingUint) {
      pendingUint->returnError(sdbus::Error{kErrCanceled, "user canceled"});
    }
    if (pendingVoid) {
      pendingVoid->returnError(sdbus::Error{kErrRejected, "user canceled"});
    }
    clearPending();
  }

  void onRequestPinCode(sdbus::Result<std::string>&& result, sdbus::ObjectPath device) {
     
    if (hasPending()) { rejectIfBusy(result, "RequestPinCode"); return; }
    pending.kind = BluetoothPairingKind::PinCode;
    pending.devicePath = device;
    pendingString = std::move(result);
    fireCallback();
  }

  void onDisplayPinCode(sdbus::Result<>&& result, sdbus::ObjectPath device, std::string pincode) {
     
    if (hasPending()) { rejectIfBusy(result, "DisplayPinCode"); return; }
    pending.kind = BluetoothPairingKind::DisplayPinCode;
    pending.devicePath = device;
    pending.pin = std::move(pincode);
    pendingVoid = std::move(result);
    fireCallback();
  }

  void onRequestPasskey(sdbus::Result<std::uint32_t>&& result, sdbus::ObjectPath device) {
     
    if (hasPending()) { rejectIfBusy(result, "RequestPasskey"); return; }
    pending.kind = BluetoothPairingKind::Passkey;
    pending.devicePath = device;
    pendingUint = std::move(result);
    fireCallback();
  }

  void onDisplayPasskey(sdbus::ObjectPath device, std::uint32_t passkey, std::uint16_t entered) {
     
    if (pending.kind == BluetoothPairingKind::DisplayPasskey && pending.devicePath == std::string(device)) {
      pending.passkey = passkey;
      pending.entered = entered;
      if (requestCallback) requestCallback(pending);
      return;
    }
    if (hasPending()) return;
    pending.kind = BluetoothPairingKind::DisplayPasskey;
    pending.devicePath = device;
    pending.passkey = passkey;
    pending.entered = entered;
    if (requestCallback) requestCallback(pending);
  }

  void onRequestConfirmation(sdbus::Result<>&& result, sdbus::ObjectPath device, std::uint32_t passkey) {
     
    if (hasPending()) { rejectIfBusy(result, "RequestConfirmation"); return; }
    pending.kind = BluetoothPairingKind::Confirm;
    pending.devicePath = device;
    pending.passkey = passkey;
    pendingVoid = std::move(result);
    fireCallback();
  }

  void onRequestAuthorization(sdbus::Result<>&& result, sdbus::ObjectPath device) {
     
    if (hasPending()) { rejectIfBusy(result, "RequestAuthorization"); return; }
    pending.kind = BluetoothPairingKind::Authorize;
    pending.devicePath = device;
    pendingVoid = std::move(result);
    fireCallback();
  }

  void onAuthorizeService(sdbus::Result<>&& result, sdbus::ObjectPath device, std::string uuid) {
     
    if (hasPending()) { rejectIfBusy(result, "AuthorizeService"); return; }
    pending.kind = BluetoothPairingKind::AuthorizeService;
    pending.devicePath = device;
    pending.uuid = std::move(uuid);
    pendingVoid = std::move(result);
    fireCallback();
  }

  void onCancel() {
     
    if (!hasPending()) return;
    std::cerr << "[bluetooth] BlueZ canceled pending pairing" << std::endl;
    cancelPending();
  }
};

BluetoothAgent::BluetoothAgent(sdbus::IConnection& bus) : m_impl(std::make_unique<Impl>(bus)) {
   
  m_impl->object = sdbus::createObject(bus, kAgentObjectPath);

  m_impl->object
      ->addVTable(
          sdbus::registerMethod("Release").implementedAs([]() {}),
          sdbus::registerMethod("RequestPinCode")
              .withInputParamNames("device")
              .withOutputParamNames("pincode")
              .implementedAs([this](sdbus::Result<std::string>&& result, sdbus::ObjectPath device) {
                m_impl->onRequestPinCode(std::move(result), std::move(device));
              }),
          sdbus::registerMethod("DisplayPinCode")
              .withInputParamNames("device", "pincode")
              .implementedAs([this](sdbus::Result<>&& result, sdbus::ObjectPath device, std::string pincode) {
                m_impl->onDisplayPinCode(std::move(result), std::move(device), std::move(pincode));
              }),
          sdbus::registerMethod("RequestPasskey")
              .withInputParamNames("device")
              .withOutputParamNames("passkey")
              .implementedAs([this](sdbus::Result<std::uint32_t>&& result, sdbus::ObjectPath device) {
                m_impl->onRequestPasskey(std::move(result), std::move(device));
              }),
          sdbus::registerMethod("DisplayPasskey")
              .withInputParamNames("device", "passkey", "entered")
              .implementedAs([this](sdbus::ObjectPath device, std::uint32_t passkey, std::uint16_t entered) {
                m_impl->onDisplayPasskey(std::move(device), passkey, entered);
              }),
          sdbus::registerMethod("RequestConfirmation")
              .withInputParamNames("device", "passkey")
              .implementedAs([this](sdbus::Result<>&& result, sdbus::ObjectPath device, std::uint32_t passkey) {
                m_impl->onRequestConfirmation(std::move(result), std::move(device), passkey);
              }),
          sdbus::registerMethod("RequestAuthorization")
              .withInputParamNames("device")
              .implementedAs([this](sdbus::Result<>&& result, sdbus::ObjectPath device) {
                m_impl->onRequestAuthorization(std::move(result), std::move(device));
              }),
          sdbus::registerMethod("AuthorizeService")
              .withInputParamNames("device", "uuid")
              .implementedAs([this](sdbus::Result<>&& result, sdbus::ObjectPath device, std::string uuid) {
                m_impl->onAuthorizeService(std::move(result), std::move(device), std::move(uuid));
              }),
          sdbus::registerMethod("Cancel").implementedAs([this]() { m_impl->onCancel(); })
      )
      .forInterface(kAgentInterface);

  try {
    auto agentManager = sdbus::createProxy(bus, kBluezBusName, kBluezRoot);
    agentManager->callMethod("RegisterAgent")
        .onInterface(kAgentManagerInterface)
        .withArguments(kAgentObjectPath, std::string(kCapability));
    agentManager->callMethod("RequestDefaultAgent").onInterface(kAgentManagerInterface).withArguments(kAgentObjectPath);
    std::cerr << "[bluetooth] registered BlueZ agent at " << std::string(kAgentObjectPath) << std::endl;
  } catch (const sdbus::Error& e) {
    std::cerr << "[bluetooth] BlueZ agent registration failed: " << e.what() << std::endl;
  }
}

BluetoothAgent::~BluetoothAgent() {
   
  MANGOWM_INFO("{}", __func__);
  if (m_impl == nullptr) return;
  m_impl->cancelPending();
  try {
    auto agentManager = sdbus::createProxy(m_impl->bus, kBluezBusName, kBluezRoot);
    agentManager->callMethod("UnregisterAgent").onInterface(kAgentManagerInterface).withArguments(kAgentObjectPath);
  } catch (const sdbus::Error& e) {
    std::cerr << "[bluetooth] BlueZ agent unregister failed: " << e.what() << std::endl;
  }
}

void BluetoothAgent::setRequestCallback(RequestCallback callback) {
   
  if (m_impl != nullptr) m_impl->requestCallback = std::move(callback);
}

void BluetoothAgent::acceptConfirm() {
   
  if (m_impl == nullptr || !m_impl->pendingVoid) { m_impl->clearPending(); return; }
  m_impl->pendingVoid->returnResults();
  m_impl->clearPending();
}

void BluetoothAgent::rejectConfirm() {
   
  if (m_impl == nullptr) return;
  m_impl->cancelPending();
}

void BluetoothAgent::submitPin(const std::string& pin) {
   
  if (m_impl == nullptr || !m_impl->pendingString) return;
  m_impl->pendingString->returnResults(pin);
  m_impl->clearPending();
}

void BluetoothAgent::submitPasskey(std::uint32_t passkey) {
   
  if (m_impl == nullptr || !m_impl->pendingUint) return;
  m_impl->pendingUint->returnResults(passkey);
  m_impl->clearPending();
}

void BluetoothAgent::cancelPending() {
   
  if (m_impl != nullptr) m_impl->cancelPending();
}

bool BluetoothAgent::hasPendingRequest() const noexcept { return m_impl != nullptr && m_impl->hasPending(); }

BluetoothPairingRequest BluetoothAgent::pendingRequest() const {
   
  return m_impl != nullptr ? m_impl->pending : BluetoothPairingRequest{};
}

}
