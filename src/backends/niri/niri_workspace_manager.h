#pragma once

#include "backends/contract/desk_types.h"
#include "backends/contract/desk_source.h"
#include "backends/contract/desk_catalog.h"
#include "backends/contract/output_link.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class NiriSession;

class NiriWorkspaceManager final : public wspace::IDeskCatalog {
public:
  explicit NiriWorkspaceManager(NiriSession& rt);
  ~NiriWorkspaceManager() override;

  NiriWorkspaceManager(NiriWorkspaceManager const&) = delete;
  NiriWorkspaceManager& operator=(NiriWorkspaceManager const&) = delete;

  void onStateChange(wspace::IDeskCatalog::Notify cb) override;
  void onOverviewChange(wspace::IDeskCatalog::Notify cb) override;
  [[nodiscard]] bool supportsOverview() const noexcept override;
  [[nodiscard]] bool hasOverview() const noexcept override { return m_knownOverview; }
  [[nodiscard]] bool overviewActive() const noexcept override { return m_overviewOpen; }
  [[nodiscard]] int eventFd() const noexcept override { return m_fd; }
  [[nodiscard]] short eventFlags() const noexcept override { return POLLIN | POLLHUP | POLLERR; }
  [[nodiscard]] int eventTimeout() const noexcept override;
  void onEvent(short revents) override;
  void sync(std::vector<DeskSlot>& workspaces, const std::string& outputName = {}) const override;
  [[nodiscard]] std::vector<std::string> deskKeys(const std::string& outputName = {}) const override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(const std::string& outputName = {}) const override;
  [[nodiscard]] std::vector<ToplevelRef> windowsOnDesk(const std::string& outputName = {}) const override;
  void teardown() override;

private:
  struct WinState {
    std::optional<std::uint64_t> wsId;
    std::string appId, title;
    std::int32_t x = 0, y = 0;
    bool operator==(WinState const&) const = default;
  };

  struct WsState {
    std::uint64_t id = 0;
    std::uint8_t idx = 0;
    std::string name, output;
    bool operator==(WsState const&) const = default;
  };

  void tryConnect();
  void closeFd(bool doReconnect);
  void scheduleRetry();
  void readFd();
  void parseLines();
  bool dispatchMsg(std::string_view line);
  bool noteWorkspacesChanged(nlohmann::json const& payload);
  bool noteWindowsChanged(nlohmann::json const& payload);
  bool noteOverviewChanged(nlohmann::json const& payload);
  bool handleWindowOpened(nlohmann::json const& payload);
  bool handleWindowLayout(nlohmann::json const& payload);
  bool applyLayoutItem(nlohmann::json const& item);
  static WsState const* claimMatching(std::vector<WsState const*> const& cand,
                                      std::unordered_map<std::uint64_t, bool>& used,
                                      DeskSlot const& row,
                                      std::optional<std::uint64_t> const& byId,
                                      std::optional<std::size_t> const& byIdx);
  bool noteWindowClosed(nlohmann::json const& payload);
  static std::optional<std::pair<std::uint64_t, WsState>> decodeWorkspace(nlohmann::json const& json);
  static std::optional<std::pair<std::uint64_t, WinState>> decodeWindow(nlohmann::json const& json);
  static bool mergeWindowFields(nlohmann::json const& json, WinState& st);
  static bool sameWindowMembership(WinState const& a, WinState const& b) noexcept;
  static bool sameWindowMembership(
      std::unordered_map<std::uint64_t, WinState> const& a,
      std::unordered_map<std::uint64_t, WinState> const& b) noexcept;
  static std::optional<std::uint64_t> parseUint(std::string const& s);
  static std::optional<std::size_t> parseLeadingDigits(std::string const& s);
  static std::string deskFileKey(WsState const& ws);
  static ToplevelRef windowRef(std::uint64_t wid, WinState const& win, std::string const& deskKey);
  [[nodiscard]] std::unordered_map<std::uint64_t, std::string>
  deskKeysByWorkspaceId(std::string const& outputName) const;
  std::vector<WsState const*> sortedWorkspaceCandidates(std::string const& outputName) const;
  void refreshOccupancy();
  void resetState();
  void emitChange() const;
  void emitOverviewChange() const;

  NiriSession& m_backend;
  int m_fd = -1;
  std::vector<char> m_buf;
  std::unordered_map<std::uint64_t, WinState> m_windows;
  std::unordered_map<std::uint64_t, std::size_t> m_occupancy;
  std::unordered_map<std::uint64_t, WsState> m_workspaces;
  bool m_knownOverview = false, m_overviewOpen = false;
  std::chrono::steady_clock::time_point m_retryAt{};
  std::chrono::seconds m_backoff{2};
  wspace::IDeskCatalog::Notify m_notify;
  wspace::IDeskCatalog::Notify m_viewNotify;
};
