#include "desktop_shell/common/registry/widget_registry.hpp"

namespace eh::shell {

void WidgetRegistry::add(const std::string& role, ActivateFn fn,
                          const std::string& component) {
  std::lock_guard<std::mutex> lock(mutex_);
  widgets_[role].push_back({std::move(fn), component});
}

void WidgetRegistry::remove_component(const std::string& component) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = widgets_.begin(); it != widgets_.end();) {
    auto& vec = it->second;
    for (auto vit = vec.begin(); vit != vec.end();) {
      if (vit->component == component)
        vit = vec.erase(vit);
      else
        ++vit;
    }
    if (vec.empty())
      it = widgets_.erase(it);
    else
      ++it;
  }
}

bool WidgetRegistry::activate(const std::string& role) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = widgets_.find(role);
  if (it == widgets_.end() || it->second.empty())
    return false;
  it->second.front().fn();
  return true;
}

bool WidgetRegistry::has(const std::string& role) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = widgets_.find(role);
  return it != widgets_.end() && !it->second.empty();
}

std::vector<WidgetRegistry::Entry> WidgetRegistry::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Entry> result;
  for (const auto& [role, vec] : widgets_) {
    for (const auto& reg : vec) {
      result.push_back({role, reg.component});
    }
  }
  return result;
}

} // namespace eh::shell
