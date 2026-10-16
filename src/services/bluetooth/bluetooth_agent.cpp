#include "services/bluetooth/bluetooth_agent.hpp"

#include <iostream>
#include <mutex>
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

const sdbus::Error::Name kErrCanceled{"org.bluez.Error.Canceled"};
const sdbus::Error::Name kErrRejected{"org.bluez.Error.Rejected"};

constexpr auto kAgentInterface = "org.bluez.Agent1";
constexpr auto kAgentManagerInterface = "org.bluez.AgentManager1";
constexpr auto kCapability = "NoInputNoOutput";

const sdbus::ObjectPath kAgentObjectPath{"/org/ehde/BluetoothAgent"};
const sdbus::ObjectPath kBluezRoot{"/org/bluez"};
const sdbus::ServiceName kBluezBusName{"org.bluez"};

}

struct BluetoothAgent::Impl {
  PairingPrompt pending;
  std::optional<sdbus::Result<std::string>> pendingString;
  std::optional<sdbus::Result<std::uint32_t>> pendingUint;
  std::optional<sdbus::Result<>> pendingVoid;

  BluetoothAgent::PairingHandler requestCallback;
  sdbus::IConnection& bus;
  std::unique_ptr<sdbus::IObject> object;
  mutable std::recursive_mutex mu_;

  explicit Impl(sdbus::IConnection& b) : bus(b) {

  }

  void abortPending() {
    std::lock_guard<std::recursive_mutex> lock(mu_);

    if (pendingString) {
      pendingString->returnError(sdbus::Error{kErrCanceled, "user canceled"});
    }
    if (pendingUint) {
      pendingUint->returnError(sdbus::Error{kErrCanceled, "user canceled"});
    }
    if (pendingVoid) {
      pendingVoid->returnError(sdbus::Error{kErrCanceled, "user canceled"});
    }
    resetPending();
  }

  void resetPending() {
    std::lock_guard<std::recursive_mutex> lock(mu_);

    pending = PairingPrompt{};
    pendingString.reset();
    pendingUint.reset();
    pendingVoid.reset();
  }

  [[nodiscard]] bool hasPending() const noexcept {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    return pendingString.has_value() || pendingUint.has_value() || pendingVoid.has_value();
  }

  void rejectIfBusy(auto& result, const char* what) {
    std::lock_guard<std::recursive_mutex> lock(mu_);

    std::cerr << "[bluetooth] " << what << " while another request pending -> rejected" << std::endl;
    result.returnError(sdbus::Error{kErrRejected, "a pairing request is already in progress"});
  }

  void fireCallback() {
    std::lock_guard<std::recursive_mutex> lock(mu_);

    if (requestCallback) {
      requestCallback(pending);
    } else {
      std::cerr << "[bluetooth] pairing request with no UI handler -> rejecting" << std::endl;
      abortPending();
    }
  }

  // Shared entry for the five BlueZ request flavors that behave the same:
  // refuse when a request is already live, record the prompt, stash the
  // reply slot via `store`, then hand the prompt to the UI via `decorate`.
  template <typename Reply, typename Store, typename Decorate>
  void openRequest(Reply&& result, const char* what, PairingMode mode, sdbus::ObjectPath device,
                   Store&& store, Decorate&& decorate) {
    std::lock_guard<std::recursive_mutex> lock(mu_);

    if (hasPending()) { rejectIfBusy(result, what); return; }
    pending.mode = mode;
    pending.devicePath = std::move(device);
    decorate(pending);
    store(std::move(result));
    fireCallback();
  }

  void onRequestPinCode(sdbus::Result<std::string>&& result, sdbus::ObjectPath device) {
    openRequest(std::move(result), "RequestPinCode", PairingMode::PinEntry, std::move(device),
                [this](auto&& r) { pendingString = std::move(r); },
                [](PairingPrompt&) {});
  }

  void onDisplayPinCode(sdbus::Result<>&& result, sdbus::ObjectPath device, std::string pincode) {
    openRequest(std::move(result), "DisplayPinCode", PairingMode::ShownPin, std::move(device),
                [this](auto&& r) { pendingVoid = std::move(r); },
                [&pincode](PairingPrompt& p) { p.pin = std::move(pincode); });
  }

  void onRequestPasskey(sdbus::Result<std::uint32_t>&& result, sdbus::ObjectPath device) {
    openRequest(std::move(result), "RequestPasskey", PairingMode::PasskeyEntry, std::move(device),
                [this](auto&& r) { pendingUint = std::move(r); },
                [](PairingPrompt&) {});
  }

  void onRequestConfirmation(sdbus::Result<>&& result, sdbus::ObjectPath device, std::uint32_t passkey) {
    openRequest(std::move(result), "RequestConfirmation", PairingMode::Confirmation, std::move(device),
                [this](auto&& r) { pendingVoid = std::move(r); },
                [passkey](PairingPrompt& p) { p.passkey = passkey; });
  }

  void onRequestAuthorization(sdbus::Result<>&& result, sdbus::ObjectPath device) {
    openRequest(std::move(result), "RequestAuthorization", PairingMode::Authorization, std::move(device),
                [this](auto&& r) { pendingVoid = std::move(r); },
                [](PairingPrompt&) {});
  }

  void onAuthorizeService(sdbus::Result<>&& result, sdbus::ObjectPath device, std::string uuid) {
    openRequest(std::move(result), "AuthorizeService", PairingMode::ServiceAuthorization, std::move(device),
                [this](auto&& r) { pendingVoid = std::move(r); },
                [&uuid](PairingPrompt& p) { p.uuid = std::move(uuid); });
  }

  // DisplayPasskey is fire-and-forget: repeats for the same device update
  // the passkey in place instead of opening a new request.
  void onDisplayPasskey(sdbus::ObjectPath device, std::uint32_t passkey, std::uint16_t entered) {
    std::lock_guard<std::recursive_mutex> lock(mu_);

    if (pending.mode == PairingMode::ShownPasskey && pending.devicePath == std::string(device)) {
      pending.passkey = passkey;
      pending.entered = entered;
      if (requestCallback) requestCallback(pending);
      return;
    }
    if (hasPending()) return;
    pending.mode = PairingMode::ShownPasskey;
    pending.devicePath = device;
    pending.passkey = passkey;
    pending.entered = entered;
    if (requestCallback) requestCallback(pending);
  }

  void onCancel() {
    std::lock_guard<std::recursive_mutex> lock(mu_);

    if (!hasPending()) {
      if (pending.mode == PairingMode::ShownPasskey ||
          pending.mode == PairingMode::ShownPin) {
        resetPending();
      }
      return;
    }
    std::cerr << "[bluetooth] BlueZ canceled pending pairing" << std::endl;
    abortPending();
  }
};

BluetoothAgent::BluetoothAgent(sdbus::IConnection& bus) : m_impl(std::make_unique<Impl>(bus)) {

  m_impl->object = sdbus::createObject(bus, kAgentObjectPath);

  m_impl->object
      ->addVTable(
          sdbus::registerMethod("Cancel").implementedAs([this]() { m_impl->onCancel(); }),
          sdbus::registerMethod("AuthorizeService")
              .withInputParamNames("device", "uuid")
              .implementedAs([this](sdbus::Result<>&& result, sdbus::ObjectPath device, std::string uuid) {
                m_impl->onAuthorizeService(std::move(result), std::move(device), std::move(uuid));
              }),
          sdbus::registerMethod("RequestAuthorization")
              .withInputParamNames("device")
              .implementedAs([this](sdbus::Result<>&& result, sdbus::ObjectPath device) {
                m_impl->onRequestAuthorization(std::move(result), std::move(device));
              }),
          sdbus::registerMethod("RequestConfirmation")
              .withInputParamNames("device", "passkey")
              .implementedAs([this](sdbus::Result<>&& result, sdbus::ObjectPath device, std::uint32_t passkey) {
                m_impl->onRequestConfirmation(std::move(result), std::move(device), passkey);
              }),
          sdbus::registerMethod("DisplayPasskey")
              .withInputParamNames("device", "passkey", "entered")
              .implementedAs([this](sdbus::ObjectPath device, std::uint32_t passkey, std::uint16_t entered) {
                m_impl->onDisplayPasskey(std::move(device), passkey, entered);
              }),
          sdbus::registerMethod("RequestPasskey")
              .withInputParamNames("device")
              .withOutputParamNames("passkey")
              .implementedAs([this](sdbus::Result<std::uint32_t>&& result, sdbus::ObjectPath device) {
                m_impl->onRequestPasskey(std::move(result), std::move(device));
              }),
          sdbus::registerMethod("DisplayPinCode")
              .withInputParamNames("device", "pincode")
              .implementedAs([this](sdbus::Result<>&& result, sdbus::ObjectPath device, std::string pincode) {
                m_impl->onDisplayPinCode(std::move(result), std::move(device), std::move(pincode));
              }),
          sdbus::registerMethod("RequestPinCode")
              .withInputParamNames("device")
              .withOutputParamNames("pincode")
              .implementedAs([this](sdbus::Result<std::string>&& result, sdbus::ObjectPath device) {
                m_impl->onRequestPinCode(std::move(result), std::move(device));
              }),
          sdbus::registerMethod("Release").implementedAs([]() {})
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
  m_impl->abortPending();
  try {
    auto agentManager = sdbus::createProxy(m_impl->bus, kBluezBusName, kBluezRoot);
    agentManager->callMethod("UnregisterAgent").onInterface(kAgentManagerInterface).withArguments(kAgentObjectPath);
  } catch (const sdbus::Error& e) {
    std::cerr << "[bluetooth] BlueZ agent unregister failed: " << e.what() << std::endl;
  }
}

PairingPrompt BluetoothAgent::pendingRequest() const {
  if (m_impl == nullptr) return PairingPrompt{};
  std::lock_guard<std::recursive_mutex> lock(m_impl->mu_);
  return m_impl->pending;
}

bool BluetoothAgent::hasPendingPairing() const noexcept { return m_impl != nullptr && m_impl->hasPending(); }

void BluetoothAgent::abortPending() {
  if (m_impl == nullptr) return;
  std::lock_guard<std::recursive_mutex> lock(m_impl->mu_);
  m_impl->abortPending();
}

void BluetoothAgent::sendPasskey(std::uint32_t passkey) {
  if (m_impl == nullptr) return;
  std::lock_guard<std::recursive_mutex> lock(m_impl->mu_);
  if (!m_impl->pendingUint) return;
  m_impl->pendingUint->returnResults(passkey);
  m_impl->resetPending();
}

void BluetoothAgent::sendPin(const std::string& pin) {
  if (m_impl == nullptr) return;
  std::lock_guard<std::recursive_mutex> lock(m_impl->mu_);
  if (!m_impl->pendingString) return;
  m_impl->pendingString->returnResults(pin);
  m_impl->resetPending();
}

void BluetoothAgent::rejectPairing() {
  if (m_impl == nullptr) return;
  std::lock_guard<std::recursive_mutex> lock(m_impl->mu_);
  m_impl->abortPending();
}

void BluetoothAgent::acceptPairing() {
  if (m_impl == nullptr) return;
  std::lock_guard<std::recursive_mutex> lock(m_impl->mu_);
  if (!m_impl->pendingVoid) { m_impl->resetPending(); return; }
  m_impl->pendingVoid->returnResults();
  m_impl->resetPending();
}

void BluetoothAgent::setPairingHandler(PairingHandler callback) {
  if (m_impl != nullptr) { std::lock_guard<std::recursive_mutex> lock(m_impl->mu_); m_impl->requestCallback = std::move(callback); }
}

}
