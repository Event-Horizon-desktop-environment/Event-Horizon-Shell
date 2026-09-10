#pragma once

#include "backends/interfaces/workspace_manager.h"

#include <unordered_map>

struct ext_workspace_group_handle_v1;
struct ext_workspace_handle_v1;
struct wl_array;

class ExtWorkspaceManager final : public IExtProtocolHandler, public IWorkspaceManager {
public:
  [[nodiscard]] const char* name() const override { return "ext-workspace"; }
  [[nodiscard]] bool ready() const noexcept override { return m_mgr != nullptr; }
  void jumpTo(const std::string& id) override;
  void jumpToOnOutput(wl_output* output, const std::string& id) override;
  void jumpToOnOutput(wl_output* output, const DeskRegion& workspace) override;
  [[nodiscard]] std::vector<DeskRegion> allRegions() const override;
  void bindExtProtocol(ext_workspace_manager_v1* manager) override;
  void teardown() override;
  [[nodiscard]] std::vector<DeskRegion> regionsOnOutput(wl_output* output) const override;
  void onStateChange(IWorkspaceManager::Notify callback) override;

  void onGroupCreated(ext_workspace_group_handle_v1* group);
  void onGroupOutputEnter(ext_workspace_group_handle_v1* group, wl_output* output);
  void onGroupOutputLeave(ext_workspace_group_handle_v1* group, wl_output* output);
  void onGroupRemoved(ext_workspace_group_handle_v1* group);
  void onGroupWorkspaceEnter(ext_workspace_group_handle_v1* group, ext_workspace_handle_v1* workspace);
  void onGroupWorkspaceLeave(ext_workspace_group_handle_v1* group, ext_workspace_handle_v1* workspace);
  void onManagerDone();
  void onManagerFinished();
  void onWorkspaceCoordinatesChanged(ext_workspace_handle_v1* workspace, wl_array* coordinates);
  void onWorkspaceCreated(ext_workspace_handle_v1* workspace);
  void onWorkspaceIdChanged(ext_workspace_handle_v1* workspace, const char* id);
  void onWorkspaceNameChanged(ext_workspace_handle_v1* workspace, const char* name);
  void onWorkspaceRemoved(ext_workspace_handle_v1* workspace);
  void onWorkspaceStateChanged(ext_workspace_handle_v1* workspace, std::uint32_t state);

private:
  struct GroupState {
    ext_workspace_group_handle_v1* h = nullptr;
    std::vector<wl_output*> outs;
    std::vector<ext_workspace_handle_v1*> ws;
  };

  ext_workspace_manager_v1* m_mgr = nullptr;
  std::vector<GroupState> m_groups;
  std::unordered_map<ext_workspace_handle_v1*, DeskRegion> m_regions;
  IWorkspaceManager::Notify m_notify;
};
