#include "desktop_shell/dashboard/dashboard_hit.hpp"

#include "desktop_shell/dock/core/dock_app.h"

namespace eh::shell::dashboard {

DashboardHit dashboard_hit_at(const DockApp& app, double px, double py) {
  DashboardHit out{};
  const DashboardLayout& L = app.dash.layout;
  if (!L.valid) return out;

  for (size_t i = 0; i < L.cards.size(); ++i) {
    const CardRect& c = L.cards[i];
    for (size_t j = 0; j < c.subs.size(); ++j) {
      const SubRect& s = c.subs[j];
      if (px >= s.hitX && px < s.hitX + s.hitW && py >= s.hitY && py < s.hitY + s.hitH) {
        out.valid = true;
        out.card = static_cast<int>(i);
        out.sub = static_cast<int>(j);
        out.kind = c.kind;
        out.role = s.role;
        out.row = s.row;
        out.streamId = s.streamId;
        out.trackX = s.trackX;
        out.trackW = s.trackW;
        return out;
      }
    }
  }

  for (size_t i = 0; i < L.cards.size(); ++i) {
    const CardRect& c = L.cards[i];
    if (px >= c.x && px < c.x + c.w && py >= c.y && py < c.y + c.h) {
      out.valid = true;
      out.card = static_cast<int>(i);
      out.sub = -1;
      out.kind = c.kind;
      out.role = DashCardRole::Body;
      return out;
    }
  }
  return out;
}

}  // namespace eh::shell::dashboard
