#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <poll.h>
#include <string>
#include <unordered_map>
#include <vector>

struct wl_output;
struct ext_workspace_manager_v1;
struct zdwl_ipc_manager_v2;

// Enums & value types used across the workspace backends.

enum class TaskbarMode : std::uint8_t {
  Generic,
  WorkspaceOccurrenceTitle,
};

struct DeskRegion {
  std::string id;
  std::string name;
  std::vector<std::uint32_t> coordinates;
  std::uint32_t index = 0;
  bool active = false;
  bool urgent = false;
  bool occupied = false;
};

struct DeskWindow {
  std::string windowId;
  std::string deskKey;
  std::string appId;
  std::string title;
  std::int32_t x = 0;
  std::int32_t y = 0;
};

struct TaskbarEntry {
  std::uintptr_t handleKey = 0;
  std::vector<std::string> appIds;
  std::string title;
};

// Protocol binding helpers — each backend hooks its compositor protocol here.

class IExtProtocolHandler {
public:
  virtual ~IExtProtocolHandler() = default;
  virtual void bindExtProtocol(ext_workspace_manager_v1* mgr) = 0;
};

class IDwlProtocolHandler {
public:
  virtual ~IDwlProtocolHandler() = default;
  virtual void bindDwlProtocol(zdwl_ipc_manager_v2* mgr) = 0;
};

// Output / connection helpers for compositor sockets.

class IOutputNameLookup {
public:
  using Resolver = std::function<std::string(wl_output*)>;

  virtual ~IOutputNameLookup() = default;
  virtual void setOutputResolver(Resolver r) = 0;
};

class ISocketConnector {
public:
  virtual ~ISocketConnector() = default;
  [[nodiscard]] virtual bool openConnection() = 0;
};

class IOutputWatcher {
public:
  virtual ~IOutputWatcher() = default;
  virtual void outputAttached(wl_output* output) = 0;
  virtual void outputDetached(wl_output* output) = 0;
};

// Primary workspace manager interface (replaces the old WorkspaceBackend).

class IWorkspaceManager {
public:
  using Notify = std::function<void()>;

  virtual ~IWorkspaceManager() = default;

  // Life-cycle
  virtual void teardown() = 0;

  // Availability
  [[nodiscard]] virtual const char* name() const = 0;
  [[nodiscard]] virtual bool ready() const noexcept = 0;

  // Change notification
  virtual void onStateChange(Notify cb) = 0;

  // Navigation
  virtual void jumpTo(const std::string& id) = 0;
  virtual void jumpToOnOutput(wl_output* output, const std::string& id) = 0;
  virtual void jumpToOnOutput(wl_output* output, const DeskRegion& region) = 0;

  // Query desks
  [[nodiscard]] virtual std::vector<DeskRegion> allRegions() const = 0;
  [[nodiscard]] virtual std::vector<DeskRegion> regionsOnOutput(wl_output* output) const = 0;
  [[nodiscard]] virtual std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(wl_output* /*output*/) const {
    return {};
  }

  // Taskbar integration
  [[nodiscard]] virtual TaskbarMode taskbarMode() const noexcept {
    return TaskbarMode::Generic;
  }
  [[nodiscard]] virtual std::unordered_map<std::uintptr_t, DeskWindow>
  matchEntries(const std::vector<TaskbarEntry>& /*entries*/, wl_output* /*output*/) const {
    return {};
  }
  [[nodiscard]] virtual std::vector<DeskWindow> windowsOnDesk(wl_output* /*output*/) const { return {}; }
  virtual void bringToFront(const std::string& /*windowId*/) {}

  // Poll-based event loop integration
  [[nodiscard]] virtual int eventFd() const noexcept { return -1; }
  [[nodiscard]] virtual short eventFlags() const noexcept { return POLLIN | POLLHUP | POLLERR; }
  [[nodiscard]] virtual int eventTimeout() const noexcept { return -1; }
  virtual void onEvent(short /*revents*/) {}
};

// Per-output metadata backend — compositor-specific workspace data feeding the overview.

namespace wspace {

class IWorkspaceDataBackend {
public:
  using Notify = std::function<void()>;

  virtual ~IWorkspaceDataBackend() = default;

  [[nodiscard]] virtual bool supportsOverview() const noexcept { return false; }
  [[nodiscard]] virtual bool hasOverview() const noexcept { return false; }
  [[nodiscard]] virtual bool overviewActive() const noexcept { return true; }

  virtual void onStateChange(Notify cb) = 0;
  virtual void onOverviewChange(Notify cb) { (void)cb; }

  [[nodiscard]] virtual int eventFd() const noexcept { return -1; }
  [[nodiscard]] virtual short eventFlags() const noexcept { return POLLIN | POLLHUP | POLLERR; }
  [[nodiscard]] virtual int eventTimeout() const noexcept { return -1; }
  virtual void onEvent(short /*revents*/) {}

  virtual void sync(std::vector<DeskRegion>& /*regions*/, const std::string& /*outputName*/ = {}) const {}
  [[nodiscard]] virtual std::vector<std::string> deskKeys(const std::string& /*outputName*/ = {}) const {
    return {};
  }
  [[nodiscard]] virtual std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(const std::string& /*outputName*/ = {}) const {
    return {};
  }
  [[nodiscard]] virtual std::vector<DeskWindow> windowsOnDesk(const std::string& /*outputName*/ = {}) const {
    return {};
  }

  virtual void teardown() = 0;
};

} // namespace wspace
