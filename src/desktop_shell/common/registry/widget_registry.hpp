#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eh::shell {

class WidgetRegistry {
public:
  using ActivateFn = std::function<void()>;

  // Register a widget: role is the canonical type (e.g. "launchpad"),
  // fn is how to activate it, component is which shell component hosts it.
  // Multiple components can register the same role.
  void add(const std::string& role, ActivateFn fn, const std::string& component);

  // Remove all widgets for a component (e.g. when it restarts).
  void remove_component(const std::string& component);

  // Activate the first registered handler for a role.
  // Returns true if a handler was found and called.
  bool activate(const std::string& role);

  // Check if at least one widget with this role is registered.
  bool has(const std::string& role) const;

  // Returns all (role, component) pairs currently registered.
  struct Entry {
    std::string role;
    std::string component;
  };
  std::vector<Entry> list() const;

private:
  struct Registration {
    ActivateFn fn;
    std::string component;
  };
  mutable std::mutex mutex_;
  // role -> list of registrations (preserves registration order)
  std::unordered_map<std::string, std::vector<Registration>> widgets_;
};

} // namespace eh::shell
