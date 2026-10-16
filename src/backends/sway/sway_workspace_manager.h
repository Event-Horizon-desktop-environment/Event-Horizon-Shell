#pragma once

#include "backends/contract/desk_types.h"
#include "backends/contract/desk_source.h"
#include "backends/contract/output_link.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wspace::sway {
class SwaySession;
}

class SwayWorkspaceManager final : public IDeskSource,
                                    public IOutputResolver,
                                    public ILinkOpener {
public:
  explicit SwayWorkspaceManager(wspace::sway::SwaySession& runtime);
  ~SwayWorkspaceManager() override;

  SwayWorkspaceManager(const SwayWorkspaceManager&) = delete;
  SwayWorkspaceManager& operator=(const SwayWorkspaceManager&) = delete;

  [[nodiscard]] const char* name() const override { return "sway-ipc"; }
  [[nodiscard]] bool ready() const noexcept override;
  [[nodiscard]] int eventFd() const noexcept { return m_fd; }
  void onStateChange(IDeskSource::Notify callback) override;
  void setOutputResolver(Resolver resolver) override;
  bool openConnection() override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(wl_output* output) const override;
  [[nodiscard]] std::vector<DeskSlot> regionsOnOutput(wl_output* output) const override;
  [[nodiscard]] std::vector<DeskSlot> allRegions() const override;
  void jumpToOnOutput(wl_output* output, const DeskSlot& region) override;
  void jumpToOnOutput(wl_output* output, const std::string& id) override;
  void jumpTo(const std::string& id) override;
  void teardown() override;

private:
  struct WorkspaceRecord {
    bool occupied = false;
    bool urgent = false;
    bool visible = false;
    int num = -1;
    std::size_t ordinal = 0;
    std::string output;
    std::string name;

    bool operator==(const WorkspaceRecord&) const = default;
  };

  [[nodiscard]] int resolveWsId(const std::string& id) const;
  static bool swayOrder(WorkspaceRecord const& a, WorkspaceRecord const& b);

  bool loadTree(const std::string& payload);
  bool loadWorkspaceList(const std::string& payload);
  bool onIpcMessage(std::uint32_t type, const std::string& payload);
  void processMessages();
  void drainEvents();
  void fetchTree();
  void fetchSnapshot();
  void writeMessage(std::uint32_t type, std::string_view payload);
  void planRetry();
  void closeIfOpen(bool scheduleRetry);
  void openStream();
  void emitChange() const;

  wspace::sway::SwaySession& m_backend;
  int m_fd = -1;
  std::vector<char> m_buf;
  std::vector<WorkspaceRecord> m_spaces;
  std::unordered_map<std::string, std::size_t> m_occMap;
  std::unordered_map<std::string, std::vector<std::string>> m_appMap;
  IDeskSource::Notify m_notify;
  Resolver m_resolver;
  std::chrono::steady_clock::time_point m_retryAt{};
  std::chrono::seconds m_backoff{2};
};

namespace wspace::sway {

}

