#pragma once

#include "backends/interfaces/workspace_manager.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct wl_output;

namespace wspace::mango {
class MangoRuntime;
}

class MangoWorkspaceManager final : public IWorkspaceManager,
                                     public IOutputWatcher,
                                     public IOutputNameLookup,
                                     public ISocketConnector {
public:
  explicit MangoWorkspaceManager(wspace::mango::MangoRuntime& rt);
  ~MangoWorkspaceManager() override = default;

  [[nodiscard]] const char* name() const override { return "mango-ipc"; }
  [[nodiscard]] bool ready() const noexcept override;
  void onStateChange(IWorkspaceManager::Notify cb) override;
  void setOutputResolver(IOutputNameLookup::Resolver r) override;
  bool openConnection() override;
  void jumpTo(const std::string& id) override;
  void jumpToOnOutput(wl_output* output, const std::string& id) override;
  void jumpToOnOutput(wl_output* output, const DeskRegion& ws) override;
  [[nodiscard]] std::vector<DeskRegion> allRegions() const override;
  [[nodiscard]] std::vector<DeskRegion> regionsOnOutput(wl_output* output) const override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(wl_output* output) const override;
  [[nodiscard]] std::vector<DeskWindow> windowsOnDesk(wl_output* output) const override;
  void bringToFront(const std::string& windowId) override;
  void teardown() override;

  void outputAttached(wl_output* output) override;
  void outputDetached(wl_output* output) override;

  [[nodiscard]] int eventFd() const noexcept override;
  [[nodiscard]] int eventTimeout() const noexcept override;
  void onEvent(short revents) override;

  [[nodiscard]] wl_output* ipcSelectedOutput() const;
  [[nodiscard]] std::optional<std::pair<std::string, std::string>>
  ipcFocusedClientForOutput(wl_output* output) const;

private:
  struct TagInfo {
    std::uint32_t index = 0;
    bool active = false, urgent = false, occupied = false, focused = false;

    friend bool operator==(TagInfo const& a, TagInfo const& b) {
      return a.index == b.index && a.active == b.active && a.urgent == b.urgent
          && a.occupied == b.occupied && a.focused == b.focused;
    }
  };

  struct OutputState {
    std::string name;
    bool active = false;
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    std::string clientTitle;
    std::string clientAppId;
    std::vector<TagInfo> tags;
  };

  struct ClientState {
    std::string id, title, appId, outName;
    std::vector<std::uint32_t> tags;
    bool focused = false;
    std::int32_t x = 0, y = 0;
  };

  bool connectSocket();
  void closeSocket();
  void drainSocket();
  bool processMsg(std::string_view line);
  void fetchClients();
  void emitChange();
  std::string resolveName(wl_output* output) const;
  OutputState* currentOutput();
  OutputState const* currentOutput() const;
  OutputState* findOutput(wl_output* output);
  OutputState const* findOutput(wl_output* output) const;
  static std::optional<std::size_t> parseIdx(std::string const& id);
  static std::optional<std::size_t> idxFromRegion(DeskRegion const& ws);
  std::optional<std::size_t> activeTagIdx(std::vector<TagInfo> const& tags) const;
  static DeskRegion buildRegion(TagInfo const& tag, bool shellActive);
  static std::optional<OutputState> decodeMonitor(nlohmann::json const& json);
  static std::optional<ClientState> decodeClient(nlohmann::json const& json);

  wspace::mango::MangoRuntime& m_backend;
  IOutputNameLookup::Resolver m_resolver;
  int m_fd = -1;
  std::string m_buf;
  std::vector<wl_output*> m_knownOutputs;
  std::unordered_map<std::string, OutputState> m_outputData;
  std::vector<ClientState> m_clients;
  IWorkspaceManager::Notify m_notify;
};
