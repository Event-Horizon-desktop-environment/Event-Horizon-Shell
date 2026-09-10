#pragma once

#include "backends/interfaces/workspace_manager.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wspace::triad {
class TriadRuntime;
}

class TriadWorkspaceManager final : public IWorkspaceManager,
                                    public wspace::IWorkspaceDataBackend,
                                    public IOutputNameLookup,
                                    public ISocketConnector {
public:
  explicit TriadWorkspaceManager(wspace::triad::TriadRuntime& core);
  ~TriadWorkspaceManager() override;

  TriadWorkspaceManager(TriadWorkspaceManager const&) = delete;
  TriadWorkspaceManager& operator=(TriadWorkspaceManager const&) = delete;

  [[nodiscard]] const char* name() const override { return "triad"; }
  [[nodiscard]] bool ready() const noexcept override;
  void onStateChange(IWorkspaceManager::Notify cb) override;
  void onOverviewChange(wspace::IWorkspaceDataBackend::Notify cb) override;
  void setOutputResolver(Resolver resolver) override;
  [[nodiscard]] bool openConnection() override;
  void jumpTo(std::string const& id) override;
  void jumpToOnOutput(wl_output* output, std::string const& id) override;
  void jumpToOnOutput(wl_output* output, DeskRegion const& ws) override;
  [[nodiscard]] std::vector<DeskRegion> allRegions() const override;
  [[nodiscard]] std::vector<DeskRegion> regionsOnOutput(wl_output* output) const override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>> appsByDesk(wl_output* output) const override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>> appsByDesk(std::string const& outputName = {}) const override;
  [[nodiscard]] std::vector<DeskWindow> windowsOnDesk(wl_output* output) const override;
  [[nodiscard]] std::vector<DeskWindow> windowsOnDesk(std::string const& outputName = {}) const override;
  void bringToFront(std::string const& windowId) override;
  void teardown() override;

  [[nodiscard]] int eventFd() const noexcept override { return m_fd; }
  [[nodiscard]] short eventFlags() const noexcept override { return POLLIN | POLLHUP | POLLERR; }
  [[nodiscard]] int eventTimeout() const noexcept override;
  void onEvent(short revents) override;
  void sync(std::vector<DeskRegion>& regions, std::string const& outputName = {}) const override;
  [[nodiscard]] std::vector<std::string> deskKeys(std::string const& outputName = {}) const override;
  [[nodiscard]] bool supportsOverview() const noexcept override;
  [[nodiscard]] bool hasOverview() const noexcept override { return m_overviewSeen; }
  [[nodiscard]] bool overviewActive() const noexcept override { return m_overviewVisible; }
  [[nodiscard]] std::optional<std::string> fetchFocusedWindow() const;

private:
  struct DeskState {
    std::uint32_t idx = 0;
    std::uint64_t tag = 0;
    std::string label;
    std::string monitor;
    bool focused = false;
    bool globallyActive = false;
    bool attention = false;
    bool hasContent = false;
    std::optional<std::uint64_t> activeChild;
  };

  struct WindowEntry {
    std::uint64_t id = 0;
    std::uint32_t parentWorkspace = 0;
    std::string monitor;
    std::string app;
    std::string caption;
    std::int32_t col = 0;
    std::int32_t row = 0;
  };

  void shutdownSocket(bool requeue);
  void scheduleRetry();
  void drainSocket();
  void processLines();
  bool ingestLine(std::string_view line);
  bool applyFullState(nlohmann::json const& payload);
  bool applyOutputList(nlohmann::json const& list);
  bool applyLayoutSection(nlohmann::json const& section);
  bool applyWindowList(nlohmann::json const& list);
  bool applySingleWindow(nlohmann::json const& entry);

  static std::optional<DeskState> decodeDesk(nlohmann::json const& src);
  static std::optional<WindowEntry> decodeWindow(nlohmann::json const& src);
  static std::string makeKey(DeskState const& ds);
  static bool isPhantom(DeskState const& ds);
  static std::optional<std::uint64_t> parseUnsigned(nlohmann::json const& v);
  static std::optional<std::int32_t> parseInt32(nlohmann::json const& v);
  static std::string extractStr(nlohmann::json const& obj, char const* key);
  static bool extractBool(nlohmann::json const& obj, char const* key);

  std::string resolveOutput(wl_output* output) const;
  bool shouldShow(DeskState const& ds, std::string const& monitor = {}) const;
  std::vector<DeskState const*> orderedDesks(std::string const& monitor = {}) const;
  std::optional<std::uint32_t> parseIndex(std::string const& id) const;
  static std::optional<std::uint64_t> parseNumeric(std::string const& s);

  void requestFresh();
  void fireChange();
  void fireOverviewChange();

  wspace::triad::TriadRuntime& m_core;
  int m_fd = -1;
  std::vector<char> m_pending;
  std::unordered_set<std::string> m_knownOutputs;
  std::unordered_map<std::uint32_t, DeskState> m_catalog;
  std::unordered_map<std::uint64_t, WindowEntry> m_entries;
  bool m_overviewSeen = false;
  bool m_overviewVisible = false;
  std::chrono::steady_clock::time_point m_nextRetry{};
  std::chrono::seconds m_backoff{2};
  IWorkspaceManager::Notify m_onChange;
  wspace::IWorkspaceDataBackend::Notify m_onOverviewChange;
  Resolver m_lookup;
};

namespace wspace::triad {

} // namespace wspace::triad
