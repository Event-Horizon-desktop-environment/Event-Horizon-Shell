#pragma once

#include "backends/interfaces/workspace_manager.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wspace::sway {
class SwayRuntime;
}

class SwayWorkspaceManager final : public IWorkspaceManager,
                                    public IOutputNameLookup,
                                    public ISocketConnector {
public:
  explicit SwayWorkspaceManager(wspace::sway::SwayRuntime& runtime);
  ~SwayWorkspaceManager() override;

  SwayWorkspaceManager(const SwayWorkspaceManager&) = delete;
  SwayWorkspaceManager& operator=(const SwayWorkspaceManager&) = delete;

  [[nodiscard]] const char* name() const override { return "sway-ipc"; }
  [[nodiscard]] bool ready() const noexcept override;
  void onStateChange(IWorkspaceManager::Notify callback) override;
  void setOutputResolver(Resolver resolver) override;
  bool openConnection() override;
  void jumpTo(const std::string& id) override;
  void jumpToOnOutput(wl_output* output, const std::string& id) override;
  void jumpToOnOutput(wl_output* output, const DeskRegion& region) override;
  [[nodiscard]] std::vector<DeskRegion> allRegions() const override;
  [[nodiscard]] std::vector<DeskRegion> regionsOnOutput(wl_output* output) const override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(wl_output* output) const override;
  void teardown() override;

  [[nodiscard]] int eventFd() const noexcept { return m_fd; }

private:
  struct WorkspaceState {
    std::string name;
    std::string output;
    bool visible = false;
    bool urgent = false;
    int num = -1;
    std::size_t ordinal = 0;
    bool occupied = false;

    bool operator==(const WorkspaceState&) const = default;
  };

  void openStream();
  void closeIfOpen(bool scheduleRetry);
  void planRetry();
  void drainEvents();
  void processMessages();
  bool onIpcMessage(std::uint32_t type, const std::string& payload);
  bool loadWorkspaceList(const std::string& payload);
  bool loadTree(const std::string& payload);
  void fetchSnapshot();
  void fetchTree();
  void writeMessage(std::uint32_t type, std::string_view payload);
  void emitChange() const;

  [[nodiscard]] int resolveWsId(const std::string& id) const;

  wspace::sway::SwayRuntime& m_backend;
  int m_fd = -1;
  std::vector<char> m_buf;
  std::vector<WorkspaceState> m_spaces;
  std::unordered_map<std::string, std::size_t> m_occMap;
  std::unordered_map<std::string, std::vector<std::string>> m_appMap;
  IWorkspaceManager::Notify m_notify;
  Resolver m_resolver;
  std::chrono::steady_clock::time_point m_retryAt{};
  std::chrono::seconds m_backoff{2};
};

namespace wspace::sway {

}

