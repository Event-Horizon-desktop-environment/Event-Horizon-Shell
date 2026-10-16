#pragma once

#include <optional>
#include <string>

#include <cstdint>
#include <functional>

#include <unordered_map>
#include <vector>

struct ext_foreign_toplevel_handle_v1;
struct hyprland_toplevel_mapping_manager_v1;

struct hyprland_toplevel_window_mapping_handle_v1;
struct zwlr_foreign_toplevel_handle_v1;

namespace wspace::hyprland {

// Maps the compositor's window addresses onto the foreign-toplevel handles
// so the shell can match its window list against Hyprland's own titles.
class HyprlandWindowTracker {
public:
  using Callback = std::function<void()>;

  ~HyprlandWindowTracker();

  [[nodiscard]] std::optional<std::string> wlrWindowId(zwlr_foreign_toplevel_handle_v1* h) const;

  void syncExt(std::vector<ext_foreign_toplevel_handle_v1*> const& handles);

  [[nodiscard]] ext_foreign_toplevel_handle_v1* extHandle(std::string_view id) const;

  [[nodiscard]] bool isKnown(std::string_view id) const;

  void setup(hyprland_toplevel_mapping_manager_v1* mgr);

  [[nodiscard]] zwlr_foreign_toplevel_handle_v1* wlrHandle(std::string_view id) const;

  void syncWlr(std::vector<zwlr_foreign_toplevel_handle_v1*> const& handles);

  [[nodiscard]] std::optional<std::string> extWindowId(ext_foreign_toplevel_handle_v1* h) const;

  void setCallback(Callback cb);

  [[nodiscard]] bool available() const noexcept { return m_mgr != nullptr; }

  static void onFail(void* data, hyprland_toplevel_window_mapping_handle_v1* req);
  static void onAddress(void* data, hyprland_toplevel_window_mapping_handle_v1* req,
                        std::uint32_t hi, std::uint32_t lo);

private:
  enum class Kind { Ext, Wlr };

  struct Pending {
    hyprland_toplevel_window_mapping_handle_v1* req = nullptr;
    union {
      ext_foreign_toplevel_handle_v1* ext;
      zwlr_foreign_toplevel_handle_v1* wlr;
    } toplevel{};
    Kind kind = Kind::Wlr;
  };

  // One algorithm shared by the wlr and ext handle families: `slotOf`
  // selects which union arm of a Pending the current family stores into.
  template <typename Handle, typename SlotFn>
  void syncHandles(std::vector<Handle*> const& handles, Kind kind,
                   std::unordered_map<Handle*, std::string>& toId,
                   std::unordered_map<std::string, Handle*>& byId,
                   SlotFn&& slotOf);

  template <typename Handle>
  void assignId(Handle* handle, std::uint64_t addr,
                std::unordered_map<Handle*, std::string>& toId,
                std::unordered_map<std::string, Handle*>& byId);

  std::optional<Pending> takePending(hyprland_toplevel_window_mapping_handle_v1* rh);
  void requestMapping(Pending p);
  void fireCallback();
  void clear(Pending& p);
  void abandon(hyprland_toplevel_window_mapping_handle_v1* rh);

  Callback m_cb;
  std::unordered_map<hyprland_toplevel_window_mapping_handle_v1*, Pending> m_pending;
  std::unordered_map<std::string, ext_foreign_toplevel_handle_v1*> m_idToExt;
  std::unordered_map<std::string, zwlr_foreign_toplevel_handle_v1*> m_idToWlr;
  std::unordered_map<ext_foreign_toplevel_handle_v1*, std::string> m_extToId;
  std::unordered_map<zwlr_foreign_toplevel_handle_v1*, std::string> m_wlrToId;
  hyprland_toplevel_mapping_manager_v1* m_mgr = nullptr;
};

} // namespace wspace::hyprland
