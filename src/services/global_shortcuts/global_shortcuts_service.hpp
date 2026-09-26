#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace eh::dbus::global_shortcuts {

struct ShortcutBinding {
  std::string shortcut_id;
  std::map<std::string, sdbus::Variant> options;
};

struct Session {
  std::string session_handle;
  std::string owner;
  std::vector<ShortcutBinding> bindings;
};

class GlobalShortcutsService {
public:
  GlobalShortcutsService();
  ~GlobalShortcutsService();

  GlobalShortcutsService(const GlobalShortcutsService&) = delete;
  GlobalShortcutsService& operator=(const GlobalShortcutsService&) = delete;
  GlobalShortcutsService(GlobalShortcutsService&&) = delete;
  GlobalShortcutsService& operator=(GlobalShortcutsService&&) = delete;

  void notifyKeyEvent(uint32_t key_sym, uint32_t state);

private:
  uint32_t onCreateSession(const std::string& sender, const std::string& session_handle,
                           const std::map<std::string, sdbus::Variant>& session_details,
                           const std::map<std::string, sdbus::Variant>& options,
                           std::map<std::string, sdbus::Variant>& results);

  uint32_t onBindShortcuts(const std::string& sender, const std::string& session_handle,
                           const std::vector<std::tuple<std::string, std::map<std::string, sdbus::Variant>>>& shortcuts,
                           const std::string& parent_window,
                           const std::map<std::string, sdbus::Variant>& options,
                           std::map<std::string, sdbus::Variant>& results);

  std::tuple<uint32_t, std::vector<std::tuple<std::string, std::map<std::string, sdbus::Variant>>>>
  onListShortcuts(const std::string& sender, const std::string& session_handle);

  Session* findSession(const std::string& session_handle);
  Session* findOwnedSession(const std::string& sender, const std::string& session_handle);
  void emitShortcutsChanged(const std::string& session_handle);

  std::unique_ptr<sdbus::IConnection> bus_;
  std::unique_ptr<sdbus::IObject> object_;
  mutable std::mutex mu_;
  std::atomic<uint32_t> next_session_id_{1};
  std::vector<Session> sessions_;
};

}
