#include "ux/settings/settings_tab_taskflip/settings_tab_taskflip.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/settings_serialize.hpp"

#include "m3/controls/input/toggle.hpp"

#include <string>

namespace {

m3::Toggle tgGallery;
m3::Toggle tgMru;
m3::Toggle tgCaption;
m3::Toggle tgCount;
m3::Toggle tgIcons;
bool tgInit = false;

struct Row {
  m3::Toggle* tg;
  const char* title;
  const char* sub;
  bool* flag;
};

void ensure_init(App& app) {
  if (tgInit) return;
  tgInit = true;
  tgGallery.setOnToggle([&](bool v) {
    app.settings.taskflipGalleryStyle = v;
    save_settings(app.settings);
  });
  tgMru.setOnToggle([&](bool v) {
    app.settings.taskflipMruOrder = v;
    save_settings(app.settings);
  });
  tgCaption.setOnToggle([&](bool v) {
    app.settings.taskflipShowCaption = v;
    save_settings(app.settings);
  });
  tgCount.setOnToggle([&](bool v) {
    app.settings.taskflipShowCount = v;
    save_settings(app.settings);
  });
  tgIcons.setOnToggle([&](bool v) {
    app.settings.taskflipShowIcons = v;
    save_settings(app.settings);
  });
}

}

void taskflip_tab_sync(App& app, int contentX, int contentW) {
  ensure_init(app);
  (void)contentX;
  (void)contentW;
  tgGallery.setOn(app.settings.taskflipGalleryStyle);
  tgMru.setOn(app.settings.taskflipMruOrder);
  tgCaption.setOn(app.settings.taskflipShowCaption);
  tgCount.setOn(app.settings.taskflipShowCount);
  tgIcons.setOn(app.settings.taskflipShowIcons);
}

void paint_taskflip_tab(App& app, cairo_t* cr, int contentX, int contentW) {
  ensure_init(app);
  taskflip_tab_sync(app, contentX, contentW);
  const double cardX = (double)contentX + 16.0;
  const double cardW = (double)contentW - 32.0;
  double y = 24.0;
  const Row rows[] = {
      {&tgGallery, "Gallery style", "Depth strip layout. Off uses the boxed rail.", &app.settings.taskflipGalleryStyle},
      {&tgMru, "Recent order", "List windows by recency instead of workspace order.", &app.settings.taskflipMruOrder},
      {&tgCaption, "Captions", "Show window titles under the selection.", &app.settings.taskflipShowCaption},
      {&tgCount, "Counts", "Show position and total above the selection.", &app.settings.taskflipShowCount},
      {&tgIcons, "Icons", "Show application icons. Off uses text only.", &app.settings.taskflipShowIcons},
  };
  for (const auto& r : rows) {
    const double rowH = 64.0;
    cairo_set_source_rgba(cr, 1, 1, 1, 0.04);
    cairo_rectangle(cr, cardX, y, cardW, rowH);
    cairo_fill(cr);
    settings_show_text(cr, cardX + 14.0, y + 22.0, r.title, 14.f, 500, 1, 1, 1, 0.93f);
    settings_show_text(cr, cardX + 14.0, y + 40.0, r.sub, 11.f, 400, 1, 1, 1, 0.6f);
    const float tgW = 48.0f, tgH = 28.0f;
    r.tg->setSize(m3::Toggle::Size::L);
    r.tg->setGeometry((float)(cardX + cardW - 14.0 - tgW), (float)(y + (rowH - tgH) * 0.5), tgW, tgH);
    r.tg->setOn(*r.flag);
    r.tg->paint(cr);
    y += rowH + 8.0;
  }
}

bool taskflip_tab_handle_pointer_down(App& app, float px, float py, int contentX, int contentW) {
  ensure_init(app);
  taskflip_tab_sync(app, contentX, contentW);
  m3::Toggle* tgs[] = {&tgGallery, &tgMru, &tgCaption, &tgCount, &tgIcons};
  for (auto* tg : tgs) {
    if (tg->containsPoint(px, py)) {
      tg->handlePointerDown(px, py);
      return true;
    }
  }
  return false;
}
