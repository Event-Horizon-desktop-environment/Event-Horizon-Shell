#pragma once
// Stub for standalone file browser

#include <memory>
#include <string>
#include <vector>

class CompositorRuntimeRegistry;

class WaylandWorkspaces {
public:
  explicit WaylandWorkspaces(CompositorRuntimeRegistry&) {}
  ~WaylandWorkspaces() = default;
  WaylandWorkspaces(const WaylandWorkspaces&) = delete;
  WaylandWorkspaces& operator=(const WaylandWorkspaces&) = delete;
};
