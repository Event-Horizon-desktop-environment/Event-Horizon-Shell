#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct hyprland_toplevel_mapping_manager_v1;
struct hyprland_toplevel_window_mapping_handle_v1;
struct zwlr_foreign_toplevel_handle_v1;
struct ext_foreign_toplevel_handle_v1;

namespace wspace::hyprland {

class HyprlandWindowTracker {
public:
  using Callback = std::function<void()>;

  ~HyprlandWindowTracker();

  void setup(hyprland_toplevel_mapping_manager_v1* mgr);
  void setCallback(Callback cb);

  void syncWlr(std::vector<zwlr_foreign_toplevel_handle_v1*> const& handles);
  void syncExt(std::vector<ext_foreign_toplevel_handle_v1*> const& handles);

  [[nodiscard]] std::optional<std::string> wlrWindowId(zwlr_foreign_toplevel_handle_v1* h) const;
  [[nodiscard]] std::optional<std::string> extWindowId(ext_foreign_toplevel_handle_v1* h) const;
  [[nodiscard]] zwlr_foreign_toplevel_handle_v1* wlrHandle(std::string_view id) const;
  [[nodiscard]] ext_foreign_toplevel_handle_v1* extHandle(std::string_view id) const;
  [[nodiscard]] bool isKnown(std::string_view id) const;

  [[nodiscard]] bool available() const noexcept { return m_mgr != nullptr; }

  static void onAddress(void* data, hyprland_toplevel_window_mapping_handle_v1* req,
                        std::uint32_t hi, std::uint32_t lo);
  static void onFail(void* data, hyprland_toplevel_window_mapping_handle_v1* req);

private:
  enum class Kind { Wlr, Ext };

  struct Pending {
    hyprland_toplevel_window_mapping_handle_v1* req = nullptr;
    Kind kind = Kind::Wlr;
    union {
      zwlr_foreign_toplevel_handle_v1* wlr;
      ext_foreign_toplevel_handle_v1* ext;
    } toplevel{};
  };

  void requestMapping(Pending p);
  void clear(Pending& p);
  void setWlrId(zwlr_foreign_toplevel_handle_v1* h, std::uint64_t addr);
  void setExtId(ext_foreign_toplevel_handle_v1* h, std::uint64_t addr);
  void fireCallback();

  hyprland_toplevel_mapping_manager_v1* m_mgr = nullptr;
  std::unordered_map<zwlr_foreign_toplevel_handle_v1*, std::string> m_wlrToId;
  std::unordered_map<ext_foreign_toplevel_handle_v1*, std::string> m_extToId;
  std::unordered_map<std::string, zwlr_foreign_toplevel_handle_v1*> m_idToWlr;
  std::unordered_map<std::string, ext_foreign_toplevel_handle_v1*> m_idToExt;
  std::unordered_map<hyprland_toplevel_window_mapping_handle_v1*, Pending> m_pending;
  Callback m_cb;
};

} // namespace wspace::hyprland
