#pragma once

#include "desktop_shell/Overview/overview_types.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

struct DockApp;

namespace eh::shell::overview {

class Host;

class Actions {
public:
  explicit Actions(Host& host);
  ~Actions() = default;

  Actions(const Actions&) = delete;
  Actions& operator=(const Actions&) = delete;

  void select_workspace(int wsIndex);
  void activate_window(int flatIdx);
  void close_window(int flatIdx);
  void move_window_to_workspace(int flatIdx, int targetWs);
  // Moves the window to a fresh workspace: the first empty strip slot if one
  // exists, otherwise a brand-new id past the last workspace.
  void move_window_to_new_workspace(int flatIdx);
  void swap_windows_in_place(int flatIdx, int targetFlatIdx);

  void run_pending_activation();
  void build_nav_windows();

  [[nodiscard]] const std::vector<std::pair<int, int>>& nav_windows() const noexcept {
    return nav_windows_;
  }

  std::unique_ptr<PendingActivation> pending_activation_;

private:
  Host& host_;
  std::vector<std::pair<int, int>> nav_windows_;
};

} // namespace eh::shell::overview
