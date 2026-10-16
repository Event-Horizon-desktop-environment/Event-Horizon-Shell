#pragma once

#include "backends/contract/desk_types.h"
#include "backends/contract/desk_source.h"
#include "backends/contract/output_link.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct wl_output;

namespace wspace::mango {
class MangoSession;
}

class MangoWorkspaceManager final : public IDeskSource,
                                     public IOutputLifecycle,
                                     public IOutputResolver,
                                     public ILinkOpener {
public:
  explicit MangoWorkspaceManager(wspace::mango::MangoSession& rt);
  ~MangoWorkspaceManager() override = default;

  [[nodiscard]] const char* name() const override { return "mango-ipc"; }

  [[nodiscard]] wl_output* chosenOutput() const;
  [[nodiscard]] std::optional<std::pair<std::string, std::string>>
  focusedToplevel(wl_output* output) const;

  void onEvent(short revents) override;
  [[nodiscard]] int eventTimeout() const noexcept override;
  [[nodiscard]] int eventFd() const noexcept override;

  void outputDetached(wl_output* output) override;
  void outputAttached(wl_output* output) override;

  void teardown() override;
  void bringToFront(const std::string& windowId) override;
  [[nodiscard]] std::vector<ToplevelRef> windowsOnDesk(wl_output* output) const override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(wl_output* output) const override;
  [[nodiscard]] std::vector<DeskSlot> regionsOnOutput(wl_output* output) const override;
  [[nodiscard]] std::vector<DeskSlot> allRegions() const override;
  void jumpToOnOutput(wl_output* output, const DeskSlot& ws) override;
  void jumpToOnOutput(wl_output* output, const std::string& id) override;
  void jumpTo(const std::string& id) override;
  bool openConnection() override;
  void setOutputResolver(IOutputResolver::Resolver r) override;
  void onStateChange(IDeskSource::Notify cb) override;
  [[nodiscard]] bool ready() const noexcept override;

private:
  struct TagRecord {
    std::uint32_t index = 0;
    bool active = false, urgent = false, occupied = false, focused = false;

    friend bool operator==(TagRecord const& a, TagRecord const& b) {
      return a.index == b.index && a.active == b.active && a.urgent == b.urgent
          && a.occupied == b.occupied && a.focused == b.focused;
    }
  };

  struct OutputRecord {
    std::string name;
    bool active = false;
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    std::string clientTitle;
    std::string clientAppId;
    std::vector<TagRecord> tags;
  };

  struct ClientRecord {
    std::string id, title, appId, outName;
    std::vector<std::uint32_t> tags;
    bool focused = false;
    std::int32_t x = 0, y = 0;
  };

  bool openSocket();
  void closeSocket();
  void drainSocket();
  bool consumeLines();
  bool processMsg(std::string_view line);
  void fetchClients();
  void emitChange();
  std::string resolveName(wl_output* output) const;
  OutputRecord* currentOutput();
  OutputRecord const* currentOutput() const;
  OutputRecord* findOutput(wl_output* output);
  OutputRecord const* findOutput(wl_output* output) const;
  // Output name to filter by, or empty for all outputs.
  std::string outputFilter(wl_output* output) const;
  static std::optional<std::size_t> parseIdx(std::string const& id);
  static std::optional<std::size_t> idxFromRegion(DeskSlot const& ws);
  std::optional<std::size_t> activeTagIdx(std::vector<TagRecord> const& tags) const;
  static DeskSlot buildRegion(TagRecord const& tag, bool shellActive);
  static bool tagVisible(TagRecord const& tag);
  std::vector<DeskSlot> regionsForTags(std::vector<TagRecord> const& tags) const;
  static TagRecord parseTag(nlohmann::json const& t);
  static std::string resolveClientId(nlohmann::json const& json);
  static void applyActiveTags(OutputRecord& st, std::vector<std::uint32_t> const& at);
  static std::optional<OutputRecord> decodeMonitor(nlohmann::json const& json);
  static std::optional<std::unordered_map<std::string, OutputRecord>> decodeMonitors(
      nlohmann::json const& payload);
  static std::optional<ClientRecord> decodeClient(nlohmann::json const& json);

  wspace::mango::MangoSession& m_backend;
  IOutputResolver::Resolver m_resolver;
  int m_fd = -1;
  std::string m_buf;
  std::vector<wl_output*> m_knownOutputs;
  std::unordered_map<std::string, OutputRecord> m_outputData;
  std::vector<ClientRecord> m_clients;
  IDeskSource::Notify m_notify;
};
