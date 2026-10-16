#pragma once

// Optional protocol bindings a backend may accept from the Wayland
// connection owner.
//
// These exist because the Wayland side binds global protocols once, then
// hands the raw objects to whichever backend is active. Rather than have the
// connection know every backend's type, each backend implements the small
// binding interface for the protocols it understands.
//
// Naming note: this project uses `bindExtProtocol` / `bindDwlProtocol`.
// Another project uses `bindExtWorkspace`. Keep them distinct — see
// Docs/Audit/PROVENANCE.md §7.

struct ext_workspace_manager_v1;
struct zdwl_ipc_manager_v2;

// Backend that consumes the ext-workspace protocol.
class IExtWorkspaceBinding {
public:
  virtual ~IExtWorkspaceBinding() = default;
  virtual void bindExtProtocol(ext_workspace_manager_v1* manager) = 0;
};

// Backend that consumes the dwl IPC workspace protocol.
//
// Note: dwl support is routed onto the Mango backend, which speaks a
// tag-based workspace model rather than numbered desks.
class IDwlWorkspaceBinding {
public:
  virtual ~IDwlWorkspaceBinding() = default;
  virtual void bindDwlProtocol(zdwl_ipc_manager_v2* manager) = 0;
};
