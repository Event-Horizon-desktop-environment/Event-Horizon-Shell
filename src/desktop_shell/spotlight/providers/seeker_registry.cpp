#include "desktop_shell/spotlight/providers/seeker_registry.hpp"

namespace eh::shell::seeker {

namespace {

std::vector<Provider>& registry() {
  static std::vector<Provider> r;
  return r;
}

}

void register_provider(Provider p) {
  for (auto& e : registry()) {
    if (e.name == p.name) {
      e = std::move(p);
      return;
    }
  }
  registry().push_back(std::move(p));
}

namespace {

std::function<std::vector<Window>()>& window_source() {
  static std::function<std::vector<Window>()> fn;
  return fn;
}

}

void set_window_list_source(std::function<std::vector<Window>()> fn) {
  window_source() = std::move(fn);
}

std::vector<Window> fetch_window_list() {
  const auto& fn = window_source();
  if (!fn) return {};
  return fn();
}

namespace {

std::vector<Window>& staged_windows() {
  static std::vector<Window> w;
  return w;
}

}

void query_providers(std::string_view text, std::vector<SpotlightHit>& out,
                     const std::vector<Window>& windows) {
  staged_windows() = windows;
  std::string t(text);
  for (const auto& p : registry()) {
    if (!p.prefix.empty()) {
      const std::string trig = "/" + p.prefix;
      if (t.size() > trig.size() && t.compare(0, trig.size(), trig) == 0 &&
          (t[trig.size()] == ' ' || t[trig.size()] == '\t')) {
        std::string_view sub(t.data() + trig.size() + 1, t.size() - trig.size() - 1);
        while (!sub.empty() && (sub.front() == ' ' || sub.front() == '\t')) sub.remove_prefix(1);
        p.query(sub, out);
        continue;
      }
      if (!p.global) continue;
    }
    p.query(t, out);
  }
  staged_windows().clear();
}

const std::vector<Window>& staged_window_list() { return staged_windows(); }

}
