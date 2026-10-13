#include <cairo/cairo.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>

#include "desktop_shell/notifications/types/notifications_notify.hpp"
#include "m3/controls/containers/button.hpp"
#include "services/updater/updater_service.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_updater/settings_tab_updater.hpp"

extern void draw(App& app);

namespace {

constexpr int kBtnH = 28;
constexpr int kBtnW = 150;
constexpr int kTitleY = 36;
constexpr int kDescY = 53;

constexpr int kStatusCardTop = kContentTop;
constexpr int kStatusCardH = 172;
constexpr int kActionCardTop = kStatusCardTop + kStatusCardH + kCardGap;
constexpr int kActionCardH = 196;
constexpr int kNotesCardTop = kActionCardTop + kActionCardH + kCardGap;
constexpr int kNotesCardH = 240;

int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string last_check_label() {
  auto& svc = eh::updater::UpdaterService::instance();
  const auto t = svc.last_check_sec();
  if (t <= 0) return "never";
  std::time_t tt = (std::time_t)t;
  char buf[64] = {};
  std::tm tmv{};
  if (::localtime_r(&tt, &tmv) != nullptr) {
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    return std::string(buf);
  }
  return "recently";
}

void toggle_geom_upd(int cx, int cw, int cardTop, int& ox, int& oy, int& ow, int& oh) {
  constexpr int swW = 52, swH = 26;
  ox = cx + cw - swW - kSpacingXL;
  oy = cardTop + 17;
  ow = swW;
  oh = swH;
}

// Drain service results into App state. Called at the top of paint.
void sync_updater(App& app) {
  auto& svc = eh::updater::UpdaterService::instance();
  svc.drain_wake();

  if (!app.updAutoCheckLoaded) {
    app.updAutoCheck = svc.auto_check();
    app.updAutoCheckLoaded = true;
  }

  app.updBusyChecking = svc.is_checking();
  app.updBusyDownloading = svc.is_downloading();
  app.updBusyInstalling = svc.is_installing();

  if (auto cr = svc.take_check_result()) {
    if (cr->ok) {
      app.updRemoteTag = cr->remoteTag;
      app.updRemoteName = cr->remoteName;
      app.updAssetName = cr->assetName;
      app.updAssetUrl = cr->assetUrl;
      app.updNotes = cr->notes;
      app.updError.clear();
      app.updNewer = cr->newer;
      app.updLocalPath.clear();
      app.updRestartArmed = false;
      if (cr->newer) {
        app.updStatusText = "Update available: " + cr->remoteTag;
        app.updStatusType = "info";
        // Notify once per tag (background + manual checks share this).
        if (svc.last_notified_tag() != cr->remoteTag) {
          svc.set_last_notified_tag(cr->remoteTag);
          std::string body = "Version " + cr->remoteTag + " is available.";
          if (!cr->assetName.empty()) body += " Package: " + cr->assetName;
          eh::notify::info(std::string("Event Horizon"), std::string("Software update available"),
                           std::move(body));
        }
      } else {
        app.updStatusText = "You're up to date (" + eh::updater::current_version_string() + ")";
        app.updStatusType = "success";
      }
      if (cr->assetUrl.empty()) {
        app.updStatusText += " — no installable package in this release";
        if (app.updStatusType == "success") app.updStatusType = "info";
      }
    } else {
      app.updError = cr->error.empty() ? "check failed" : cr->error;
      app.updStatusText = "Check failed: " + app.updError;
      app.updStatusType = "error";
    }
  }
  if (auto dr = svc.take_download_result()) {
    if (dr->ok) {
      app.updLocalPath = dr->path;
      app.updStatusText = "Downloaded " + app.updAssetName + " — ready to install";
      app.updStatusType = "info";
      app.updError.clear();
    } else {
      app.updError = dr->error.empty() ? "download failed" : dr->error;
      app.updStatusText = "Download failed: " + app.updError;
      app.updStatusType = "error";
    }
  }
  if (auto ir = svc.take_install_result()) {
    if (ir->ok) {
      app.updStatusText = "Installed " + app.updRemoteTag + " — restart to apply";
      app.updStatusType = "success";
      app.updError.clear();
    } else {
      app.updError = ir->error.empty() ? "install failed" : ir->error;
      app.updStatusText = "Install failed: " + app.updError;
      app.updStatusType = "error";
    }
  }

  // Periodic background check: every paint is cheap, gate on time + service due.
  const int64_t now = now_ms();
  if (app.updAutoCheck && !app.updBusyChecking && !app.updBusyDownloading &&
      !app.updBusyInstalling && svc.periodic_due(6) && (now - app.updLastPollMs) > 30000) {
    app.updLastPollMs = now;
    svc.check_async();
  }
}

}  // namespace

int updater_content_bottom(int contentX, int contentW) {
  (void)contentX;
  (void)contentW;
  return kNotesCardTop + kNotesCardH;
}

void paint_updater_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  sync_updater(app);

  cairo_save(cr);
  cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                  static_cast<double>(contentW),
                  static_cast<double>(app.height - kContentTop - kSpacingL));
  cairo_clip(cr);

  const int cx = contentX + 8;
  const int cw = contentW - 16;
  const int kL = cx + kCardPad;
  const int kW = cw - kCardPad * 2;
  const double cardX = (double)cx;
  const double cardW = (double)cw;

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  auto paint_btn = [&](int bx, int by, int bw, int bh, const char* label, bool disabled,
                       bool filled = false) {
    m3::Button btn;
    btn.setMinSize(0, 0);
    btn.setLabel(label);
    btn.setGeometry((float)bx, (float)by, (float)bw, (float)bh);
    btn.setStyle(filled ? m3::Button::Style::Filled : m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    btn.setEnabled(!disabled);
    btn.setAccentColor(a_r, a_g, a_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    btn.setPointer((float)app.pointerX, (float)app.pointerY);
    btn.paint(cr);
  };

  const bool busy = app.updBusyChecking || app.updBusyDownloading || app.updBusyInstalling;

  // ── Card 1: This device ──
  {
    settings_card(app, cr, cardX, (double)kStatusCardTop, cardW, (double)kStatusCardH, glassOv);
    settings_show_text(cr, (double)kL, (double)(kStatusCardTop + kTitleY), "This device", 14.f, 500,
                       t_r, t_g, t_b, 0.93f);
    settings_show_text(cr, (double)kL, (double)(kStatusCardTop + kDescY),
                       "Current version and update preferences", 11.f, 400, t_r, t_g, t_b, 0.46f);
    {
      int tgX, tgY, tgW, tgH;
      toggle_geom_upd(cx, cw, kStatusCardTop, tgX, tgY, tgW, tgH);
      settings_toggle(app, cr, cx, kStatusCardTop + 17, cw, (double)tgY, (double)tgH,
                      app.updAutoCheck, 0.0);
    }
    const std::string cur = "Event Horizon " + eh::updater::current_version_string();
    settings_show_text(cr, (double)kL, (double)(kStatusCardTop + 92), cur.c_str(), 12.f, 600,
                       t_r, t_g, t_b, 0.95f);
    const eh::updater::PkgKind kind = eh::updater::detect_pkg_kind();
    const std::string pkg = std::string("Package: ") + eh::updater::pkg_kind_label(kind);
    settings_show_text(cr, (double)kL, (double)(kStatusCardTop + 114), pkg.c_str(), 11.f, 400,
                       t_r, t_g, t_b, 0.65f);
    const std::string lc = "Last checked: " + last_check_label() + "   •   Auto-check every 6h";
    settings_show_text(cr, (double)kL, (double)(kStatusCardTop + 136), lc.c_str(), 11.f, 400,
                       t_r, t_g, t_b, 0.55f);
    settings_show_text(cr, (double)kL, (double)(kStatusCardTop + 156),
                       "Toggle: automatically check GitHub releases in the background", 10.f, 400,
                       t_r, t_g, t_b, 0.40f);
  }

  // ── Card 2: Software update ──
  {
    settings_card(app, cr, cardX, (double)kActionCardTop, cardW, (double)kActionCardH, glassOv);
    settings_show_text(cr, (double)kL, (double)(kActionCardTop + kTitleY), "Software update", 14.f,
                       500, t_r, t_g, t_b, 0.93f);
    settings_show_text(cr, (double)kL, (double)(kActionCardTop + kDescY),
                       "Checks GitHub releases, installs with pkexec, then restarts", 11.f, 400,
                       t_r, t_g, t_b, 0.46f);

    float sr = t_r, sg = t_g, sb = t_b, sa = 0.90f;
    if (app.updStatusType == "success") {
      sr = 0.55f;
      sg = 0.90f;
      sb = 0.60f;
    } else if (app.updStatusType == "error") {
      sr = 0.95f;
      sg = 0.55f;
      sb = 0.50f;
    }
    settings_show_text(cr, (double)kL, (double)(kActionCardTop + 96), app.updStatusText.c_str(),
                       12.f, 500, sr, sg, sb, sa);
    if (app.updNewer && !app.updRemoteTag.empty()) {
      std::string detail = app.updRemoteTag;
      if (!app.updAssetName.empty()) detail += "  •  " + app.updAssetName;
      if (!app.updLocalPath.empty()) detail += "  •  downloaded";
      settings_show_text(cr, (double)kL, (double)(kActionCardTop + 118), detail.c_str(), 11.f,
                         400, t_r, t_g, t_b, 0.60f);
    }

    const int by = kActionCardTop + kActionCardH - kBtnH - 16;
    const bool canCheck = !busy;
    const bool canDl = !busy && app.updNewer && !app.updAssetUrl.empty() && app.updLocalPath.empty();
    const bool canInstall =
        !busy && app.updNewer && !app.updLocalPath.empty() && app.updStatusType != "success";
    // Install succeeded -> show restart (status text mentions restart).
    const bool installed =
        app.updStatusType == "success" && app.updNewer && !app.updRemoteTag.empty();
    const bool canRestart = !busy && installed;

    int bx = kL + kW - kBtnW;
    if (canRestart) {
      paint_btn(bx, by, kBtnW, kBtnH, app.updRestartArmed ? "Confirm restart" : "Restart shell",
                false, app.updRestartArmed);
      bx -= kBtnW + kSpacingM;
    }
    if (canInstall || (installed && app.updLocalPath.empty())) {
      // After a fresh check the package isn't downloaded yet; the Download
      // button covers that. Once downloaded, offer Install.
      if (!app.updLocalPath.empty())
        paint_btn(bx, by, kBtnW, kBtnH, "Install update", false, true);
      bx -= kBtnW + kSpacingM;
    }
    if (canDl) {
      paint_btn(bx, by, kBtnW, kBtnH, "Download", false, true);
      bx -= kBtnW + kSpacingM;
    }
    paint_btn(bx, by, kBtnW, kBtnH,
              app.updBusyChecking ? "Checking…" : (app.updBusyDownloading ? "Busy…" : "Check now"),
              !canCheck);
    if (app.updBusyDownloading || app.updBusyInstalling) {
      settings_show_text(cr, (double)kL, (double)(by + 8),
                         app.updBusyDownloading ? "Downloading package… (pkexec comes next)"
                                                : "Installing… (auth prompt opened)",
                         11.f, 400, t_r, t_g, t_b, 0.60f);
    }
  }

  // ── Card 3: Release notes ──
  {
    settings_card(app, cr, cardX, (double)kNotesCardTop, cardW, (double)kNotesCardH, glassOv);
    std::string title = "Release notes";
    if (!app.updRemoteTag.empty()) title += " — " + app.updRemoteTag;
    settings_show_text(cr, (double)kL, (double)(kNotesCardTop + kTitleY), title.c_str(), 14.f, 500,
                       t_r, t_g, t_b, 0.93f);
    const std::string body =
        app.updNotes.empty() ? "Check for updates to fetch the latest release notes."
                             : app.updNotes;
    // First 8 lines, hard-truncated (no wrapping helper available).
    std::vector<std::string> lines;
    {
      size_t p = 0;
      while (p < body.size() && lines.size() < 8) {
        size_t nl = body.find('\n', p);
        std::string ln = body.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
        if (ln.size() > 110) ln = ln.substr(0, 110) + "…";
        // Skip markdown header noise.
        if (!ln.empty() && ln[0] != '#') lines.push_back(ln);
        if (nl == std::string::npos) break;
        p = nl + 1;
      }
    }
    int ly = kNotesCardTop + 70;
    for (const auto& ln : lines) {
      if (ln.empty()) {
        ly += 8;
        continue;
      }
      settings_show_text(cr, (double)kL, (double)ly, ln.c_str(), 11.f, 400, t_r, t_g, t_b, 0.70f);
      ly += 20;
    }
  }

  cairo_restore(cr);
}

bool settings_updater_consume_pointer_down(App& app, int contentX, int contentW) {
  auto& svc = eh::updater::UpdaterService::instance();
  const int cx = contentX + 8;
  const int cw = contentW - 16;
  const int kL = cx + kCardPad;
  const int kW = cw - kCardPad * 2;

  auto hit = [&](int bx, int by, int bw, int bh) {
    return app.pointerX >= bx && app.pointerX < bx + bw && app.pointerY >= by &&
           app.pointerY < by + bh;
  };

  // Auto-check toggle.
  {
    int tgX, tgY, tgW, tgH;
    toggle_geom_upd(cx, cw, kStatusCardTop, tgX, tgY, tgW, tgH);
    if (point_in_rect(app.pointerX, app.pointerY, tgX, tgY, tgW, tgH)) {
      app.updAutoCheck = !app.updAutoCheck;
      app.updAutoCheckLoaded = true;
      svc.set_auto_check(app.updAutoCheck);
      app.updLastPollMs = now_ms();  // don't instantly fire after toggling on
      draw(app);
      return true;
    }
  }

  const bool busy = svc.is_checking() || svc.is_downloading() || svc.is_installing();
  const int by = kActionCardTop + kActionCardH - kBtnH - 16;
  const bool installed =
      app.updStatusType == "success" && app.updNewer && !app.updRemoteTag.empty();

  // Recompute button layout in reverse (right-aligned), mirroring paint.
  int bx = kL + kW - kBtnW;
  if (!busy && installed) {
    if (hit(bx, by, kBtnW, kBtnH)) {
      const int64_t now = now_ms();
      if (!app.updRestartArmed || (now - app.updRestartArmMs) > 10000) {
        app.updRestartArmed = true;
        app.updRestartArmMs = now;
        app.updStatusText = "Click again to restart Event Horizon into " + app.updRemoteTag;
        app.updStatusType = "info";
        draw(app);
        return true;
      }
      app.updRestartArmed = false;
      app.updStatusText = "Restarting…";
      app.updStatusType = "info";
      draw(app);
      eh::updater::request_shell_restart();
      return true;
    }
    bx -= kBtnW + kSpacingM;
  }
  const bool canInstall =
      !busy && app.updNewer && !app.updLocalPath.empty() && app.updStatusType != "success";
  if ((canInstall || (installed && app.updLocalPath.empty())) && !app.updLocalPath.empty()) {
    if (hit(bx, by, kBtnW, kBtnH)) {
      app.updStatusText = "Installing… (auth prompt opened)";
      app.updStatusType = "info";
      svc.install_async(app.updLocalPath);
      draw(app);
      return true;
    }
    bx -= kBtnW + kSpacingM;
  } else if (!busy && app.updNewer && app.updLocalPath.empty() && installed) {
    bx -= kBtnW + kSpacingM;  // keep layout in sync when Install is hidden post-restart
  }
  const bool canDl = !busy && app.updNewer && !app.updAssetUrl.empty() && app.updLocalPath.empty();
  if (canDl) {
    if (hit(bx, by, kBtnW, kBtnH)) {
      app.updStatusText = "Downloading " + app.updAssetName + "…";
      app.updStatusType = "info";
      svc.download_async(app.updAssetUrl, app.updAssetName);
      draw(app);
      return true;
    }
    bx -= kBtnW + kSpacingM;
  }
  if (hit(bx, by, kBtnW, kBtnH)) {
    if (busy) return true;
    app.updStatusText = "Checking GitHub releases…";
    app.updStatusType = "idle";
    app.updError.clear();
    app.updRestartArmed = false;
    svc.check_async();
    draw(app);
    return true;
  }
  return false;
}
