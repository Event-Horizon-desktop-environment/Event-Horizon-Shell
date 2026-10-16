#pragma once

#include "backends/contract/desk_types.h"
#include "backends/contract/desk_source.h"
#include "backends/contract/protocol_binders.h"
#include "backends/contract/output_link.h"

#include <unordered_map>

struct ext_workspace_group_handle_v1;
struct ext_workspace_handle_v1;
struct wl_array;

class ExtWorkspaceManager final : public IExtWorkspaceBinding, public IDeskSource {
public:
  [[nodiscard]] const char* name() const override { return "ext-workspace"; }
  [[nodiscard]] bool ready() const noexcept override { return m_mgr != nullptr; }

  // Protocol event sinks (called by the generated listeners).
  void noteManagerDone();
  void noteManagerFinished();
  void noteGroupCreated(ext_workspace_group_handle_v1* group);
  void noteGroupRemoved(ext_workspace_group_handle_v1* group);
  void noteGroupOutputEnter(ext_workspace_group_handle_v1* group, wl_output* output);
  void noteGroupOutputLeave(ext_workspace_group_handle_v1* group, wl_output* output);
  void noteGroupWorkspaceEnter(ext_workspace_group_handle_v1* group, ext_workspace_handle_v1* workspace);
  void noteGroupWorkspaceLeave(ext_workspace_group_handle_v1* group, ext_workspace_handle_v1* workspace);
  void noteWorkspaceCreated(ext_workspace_handle_v1* workspace);
  void noteWorkspaceRemoved(ext_workspace_handle_v1* workspace);
  void noteWorkspaceIdChanged(ext_workspace_handle_v1* workspace, const char* id);
  void noteWorkspaceNameChanged(ext_workspace_handle_v1* workspace, const char* name);
  void noteWorkspaceCoordsChanged(ext_workspace_handle_v1* workspace, wl_array* coordinates);
  void noteWorkspaceStateChanged(ext_workspace_handle_v1* workspace, std::uint32_t state);

  void onStateChange(IDeskSource::Notify callback) override;
  [[nodiscard]] std::vector<DeskSlot> regionsOnOutput(wl_output* output) const override;
  [[nodiscard]] std::vector<DeskSlot> allRegions() const override;
  void jumpToOnOutput(wl_output* output, const DeskSlot& workspace) override;
  void jumpToOnOutput(wl_output* output, const std::string& id) override;
  void jumpTo(const std::string& id) override;
  void bindExtProtocol(ext_workspace_manager_v1* manager) override;
  void teardown() override;

private:
  struct GroupState {
    std::vector<wl_output*> outs;
    std::vector<ext_workspace_handle_v1*> ws;
    ext_workspace_group_handle_v1* h = nullptr;
  };

  bool fireOnGroup(GroupState const& g, DeskSlot const& want, int rank);
  bool groupServesOutput(GroupState const& g, wl_output* output) const;
  DeskSlot* lookupSlot(ext_workspace_handle_v1* w);
  void setSlotText(ext_workspace_handle_v1* w, const char* text, std::string DeskSlot::*field);
  GroupState* locateGroup(ext_workspace_group_handle_v1* h);
  int matchRank(DeskSlot const& cand, DeskSlot const& want) const;
  void orderRegions(std::vector<DeskSlot>& regions) const;

  std::unordered_map<ext_workspace_handle_v1*, DeskSlot> m_regions;
  std::vector<GroupState> m_groups;
  ext_workspace_manager_v1* m_mgr = nullptr;
  IDeskSource::Notify m_notify;
};
