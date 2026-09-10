#include "desktop_shell/dock/output/dock_layer_outputs.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/common/monitor/output_assign.hpp"

#include <wayland-client.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <algorithm>

#define DOCK_DIAG(fmt, ...) do {} while(0)

static std::string layer_outputs_trim(std::string s) {
   
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i) s.erase(0, i);
  return s;
}

static std::string dock_env_output_name() {
   
  const char* e = std::getenv("EH_DOCK_OUTPUT");
  if (!e || !*e) return {};
  std::string s(e);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  DOCK_DIAG("EH_DOCK_OUTPUT=\"%s\"", s.c_str());
  return s;
}

static bool dock_output_name_ieq(std::string_view a, std::string_view b) {
   
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

static std::string dock_requested_output_string(const DockApp& app) {
   
  std::string want = dock_env_output_name();
  if (!want.empty()) return want;
  std::string s = layer_outputs_trim(app.settings.outputName);
  if (s.empty()) return {};
  if (dock_output_name_ieq(s, "auto")) return {};
  if (eh::shell::output_assign_is_all_displays(s)) return {};
  return s;
}

wl_output* dock_pick_layer_output(DockApp& app) {
   
  const std::string want = dock_requested_output_string(app);
  DOCK_DIAG("dock_pick_layer_output: want=\"%s\"", want.c_str());

  // When a specific output is requested by name, match it regardless of ready state.
  // The wl_output proxy is valid as soon as it's bound; xdg-output events are async.
  if (!want.empty()) {
    for (const auto& u : app.outputSlots) {
      if (!u || !u->output || u->output_name.empty()) continue;
      if (u->output_name == want || dock_output_name_ieq(u->output_name, want)) {
        DOCK_DIAG("dock_pick_layer_output: matched by name \"%s\"", u->output_name.c_str());
        return u->output;
      }
    }
    // Fallback: match by output_name from wl_output listener if available
    for (const auto& u : app.outputSlots) {
      if (!u || !u->output) continue;
      if (u->output_name == want || dock_output_name_ieq(u->output_name, want)) {
        DOCK_DIAG("dock_pick_layer_output: matched by name (fallback) \"%s\"", u->output_name.c_str());
        return u->output;
      }
    }
  }

  // No specific output requested or name not found — use position heuristic on ready outputs
  const DockOutputSlot* best = nullptr;
  for (const auto& u : app.outputSlots)
    if (u && u->ready && u->logical_w > 0 && u->logical_h > 0)
      if (!best || u->logical_x < best->logical_x || (u->logical_x == best->logical_x && u->logical_y < best->logical_y))
        best = u.get();

  if (best) {
    DOCK_DIAG("dock_pick_layer_output: auto heuristic picked \"%s\" at %d,%d",
              best->output_name.c_str(), best->logical_x, best->logical_y);
    return best->output;
  }

  // No ready output — return the first output we have
  for (const auto& u : app.outputSlots)
    if (u && u->output) {
      DOCK_DIAG("dock_pick_layer_output: fallback to first output \"%s\"", u->output_name.c_str());
      return u->output;
    }

  DOCK_DIAG("dock_pick_layer_output: no output found");
  return nullptr;
}

std::vector<wl_output*> dock_collect_layer_target_outputs(DockApp& app) {
   
  std::vector<wl_output*> ts;
  const std::string raw = eh::shell::trim_output_assign(app.settings.outputName);
  bool is_all = eh::shell::output_assign_is_all_displays(raw);
  DOCK_DIAG("dock_collect_layer_target_outputs: outputName=\"%s\" raw=\"%s\" is_all=%d n_slots=%zu",
            app.settings.outputName.c_str(), raw.c_str(), is_all ? 1 : 0, app.outputSlots.size());
  for (auto& u : app.outputSlots) {
    if (!u) continue;
    DOCK_DIAG("  slot: output=%p name=\"%s\" ready=%d pos=%d,%d size=%dx%d",
              (void*)u->output, u->output_name.c_str(), u->ready ? 1 : 0,
              u->logical_x, u->logical_y, u->logical_w, u->logical_h);
  }
  if (is_all) {
    for (const auto& u : app.outputSlots) {
      if (!u || !u->output) continue;
      ts.push_back(u->output);
    }
    std::sort(ts.begin(), ts.end(), [&](wl_output* a, wl_output* b) {
      int ax = 0, bx = 0, ay = 0, by = 0;
      for (const auto& u : app.outputSlots) {
        if (u && u->output == a) {
          ax = u->logical_x;
          ay = u->logical_y;
        }
        if (u && u->output == b) {
          bx = u->logical_x;
          by = u->logical_y;
        }
      }
      if (ax != bx) return ax < bx;
      return ay < by;
    });
    DOCK_DIAG("  -> returning %zu targets (all mode)", ts.size());
    return ts;
  }
  wl_output* one = dock_pick_layer_output(app);
  if (one) {
    ts.push_back(one);
  }
  return ts;
}

DockOutputLayer* dock_layer_from_surface(const DockApp& app, wl_surface* s) {
   
  if (!s) return nullptr;
  for (const auto& up : app.dockLayers)
    if (up && up->surface == s) return up.get();
  return nullptr;
}
