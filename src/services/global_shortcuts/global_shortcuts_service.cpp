#include "services/global_shortcuts/global_shortcuts_service.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <tuple>

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IObject.h>
#include <sdbus-c++/Message.h>
#include <sdbus-c++/VTableItems.h>

namespace eh::dbus::global_shortcuts {

namespace {

const sdbus::ServiceName kBusName{"org.freedesktop.impl.portal.desktop.eventhorizon"};
const sdbus::ObjectPath kObjectPath{"/org/freedesktop/portal/desktop"};
constexpr auto kInterface = "org.freedesktop.impl.portal.GlobalShortcuts";

constexpr uint32_t kResponseSuccess = 0;
constexpr uint32_t kResponseFailed = 1;

uint32_t next_session_id() {
   
  static uint32_t id = 0;
  return ++id;
}

std::string make_session_handle() {
   
  return "/org/freedesktop/portal/desktop/session/" + std::to_string(next_session_id());
}

}

GlobalShortcutsService::GlobalShortcutsService() {
   
  bus_ = sdbus::createSessionBusConnection();
  bus_->requestName(kBusName);
  object_ = sdbus::createObject(*bus_, kObjectPath);

  sdbus::MethodVTableItem create_session;
  create_session.name = sdbus::MethodName{"CreateSession"};
  create_session.inputSignature = sdbus::Signature{"ssa{sv}"};
  create_session.inputParamNames = {"session_handle", "session_details", "options"};
  create_session.outputSignature = sdbus::Signature{"ua{sv}"};
  create_session.outputParamNames = {"result", "results"};
  create_session.callbackHandler = [this](sdbus::MethodCall call) {
    std::string session_handle;
    std::map<std::string, sdbus::Variant> session_details;
    std::map<std::string, sdbus::Variant> options;
    call >> session_handle >> session_details >> options;

    std::map<std::string, sdbus::Variant> results;
    auto rc = onCreateSession(session_handle, session_details, options, results);

    auto reply = call.createReply();
    reply << rc << results;
    reply.send();
  };

  sdbus::MethodVTableItem bind_shortcuts;
  bind_shortcuts.name = sdbus::MethodName{"BindShortcuts"};
  bind_shortcuts.inputSignature = sdbus::Signature{"sa(sa{sv})sa{sv}"};
  bind_shortcuts.inputParamNames = {"session_handle", "shortcuts", "parent_window", "options"};
  bind_shortcuts.outputSignature = sdbus::Signature{"ua{sv}"};
  bind_shortcuts.outputParamNames = {"result", "results"};
  bind_shortcuts.callbackHandler = [this](sdbus::MethodCall call) {
    std::string session_handle;
    std::vector<std::tuple<std::string, std::map<std::string, sdbus::Variant>>> shortcuts;
    std::string parent_window;
    std::map<std::string, sdbus::Variant> options;
    call >> session_handle >> shortcuts >> parent_window >> options;

    std::map<std::string, sdbus::Variant> results;
    auto rc = onBindShortcuts(session_handle, shortcuts, parent_window, options, results);

    auto reply = call.createReply();
    reply << rc << results;
    reply.send();
  };

  sdbus::MethodVTableItem list_shortcuts;
  list_shortcuts.name = sdbus::MethodName{"ListShortcuts"};
  list_shortcuts.inputSignature = sdbus::Signature{"s"};
  list_shortcuts.inputParamNames = {"session_handle"};
  list_shortcuts.outputSignature = sdbus::Signature{"ua(sa{sv})"};
  list_shortcuts.outputParamNames = {"result", "shortcuts"};
  list_shortcuts.callbackHandler = [this](sdbus::MethodCall call) {
    std::string session_handle;
    call >> session_handle;

    auto [rc, shortcuts_list] = onListShortcuts(session_handle);

    auto reply = call.createReply();
    reply << rc << shortcuts_list;
    reply.send();
  };

  object_->addVTable(create_session, bind_shortcuts, list_shortcuts).forInterface(kInterface);

  std::cerr << "[global-shortcuts] registered\n";
  bus_->enterEventLoopAsync();
}

GlobalShortcutsService::~GlobalShortcutsService() {
   
  MANGOWM_INFO("{}", __func__);
  std::cerr << "[global-shortcuts] shutting down\n";
}

Session* GlobalShortcutsService::findSession(const std::string& session_handle) {
   
  auto it = std::ranges::find_if(sessions_, [&](const Session& s) { return s.session_handle == session_handle; });
  if (it != sessions_.end()) return &*it;
  return nullptr;
}

uint32_t GlobalShortcutsService::onCreateSession(const std::string& session_handle,
                                                  const std::map<std::string, sdbus::Variant>&,
                                                  const std::map<std::string, sdbus::Variant>&,
                                                  std::map<std::string, sdbus::Variant>& results) {
   
  auto handle = session_handle.empty() ? make_session_handle() : session_handle;

  if (findSession(handle)) {
    results["handle"] = sdbus::Variant{handle};
    return kResponseSuccess;
  }

  Session s;
  s.session_handle = handle;
  sessions_.push_back(std::move(s));

  std::cerr << "[global-shortcuts] session created: " << handle << '\n';
  results["handle"] = sdbus::Variant{handle};
  results["session_handle"] = sdbus::Variant{handle};
  return kResponseSuccess;
}

uint32_t GlobalShortcutsService::onBindShortcuts(
    const std::string& session_handle,
    const std::vector<std::tuple<std::string, std::map<std::string, sdbus::Variant>>>& shortcuts,
    const std::string&,
    const std::map<std::string, sdbus::Variant>&,
    std::map<std::string, sdbus::Variant>&) {
   
  auto* session = findSession(session_handle);
  if (!session) return kResponseFailed;

  session->bindings.clear();
  session->bindings.reserve(shortcuts.size());

  for (const auto& [shortcut_id, options] : shortcuts) {
    ShortcutBinding b;
    b.shortcut_id = shortcut_id;
    b.options = options;
    session->bindings.push_back(std::move(b));
    std::cerr << "[global-shortcuts]   shortcut: " << shortcut_id << '\n';
  }

  std::cerr << "[global-shortcuts] bound " << shortcuts.size() << " shortcuts for session " << session_handle << '\n';
  emitShortcutsChanged(session_handle);
  return kResponseSuccess;
}

std::tuple<uint32_t, std::vector<std::tuple<std::string, std::map<std::string, sdbus::Variant>>>>
GlobalShortcutsService::onListShortcuts(const std::string& session_handle) {
   
  auto* session = findSession(session_handle);
  if (!session) return {kResponseFailed, {}};

  std::vector<std::tuple<std::string, std::map<std::string, sdbus::Variant>>> out;
  out.reserve(session->bindings.size());
  for (const auto& b : session->bindings) {
    out.emplace_back(b.shortcut_id, b.options);
  }
  return {kResponseSuccess, std::move(out)};
}

void GlobalShortcutsService::emitShortcutsChanged(const std::string& session_handle) {
   
  auto* session = findSession(session_handle);
  if (!session) return;

  std::vector<std::tuple<std::string, std::map<std::string, sdbus::Variant>>> shortcuts;
  shortcuts.reserve(session->bindings.size());
  for (const auto& b : session->bindings) {
    shortcuts.emplace_back(b.shortcut_id, b.options);
  }

  object_->emitSignal("ShortcutsChanged")
      .onInterface(kInterface)
      .withArguments(session_handle, shortcuts);
}

void GlobalShortcutsService::notifyKeyEvent(uint32_t key_sym, uint32_t state) {
   
  if (state != 0) return;

  for (auto& session : sessions_) {
    for (const auto& b : session.bindings) {
      (void)key_sym;
      (void)b;
    }
  }
}

}
