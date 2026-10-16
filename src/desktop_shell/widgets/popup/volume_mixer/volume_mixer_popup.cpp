#include "desktop_shell/widgets/popup/volume_mixer/volume_mixer_popup.hpp"

#include "../../../dock/core/dock_app.h"
#include "../../../shared/popup/session/session.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "services/audio/pipewire_service.hpp"
#include "../../../controlcenter/mixer/mixer_stream_icon_resolve.hpp"
#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"
#include "desktop_shell/common/asset/asset_loader.hpp"
#include "desktop_shell/widgets/popup/volume_mixer/volume_mixer_log.hpp"
#include "desktop_shell/common/audio/audio_display_names.hpp"

using eh::shell::audio::shorten_device_name;
using eh::shell::audio::shorten_profile_label;

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include <cairo/cairo.h>

namespace eh::shell::dock::popup::volume_mixer {
namespace {

constexpr double kPad          = 16.0;
constexpr double kCornerRadius  = 20.0;
constexpr double kHeaderH       = 56.0;
constexpr double kSegH          = 32.0;
constexpr double kSegR          = 10.0;
constexpr double kRowH          = 52.0;
constexpr double kMasterH       = 96.0;
constexpr double kRadioR        = 9.0;
constexpr double kRadioInnerR   = 5.0;
constexpr double kTileSz        = 34.0;
constexpr double kTileR         = 10.0;
constexpr double kDevIconSz     = 20.0;
constexpr double kMuteBtnSz     = 28.0;
constexpr double kMuteIconSz    = 18.0;
constexpr double kAppIconSz     = 26.0;
constexpr double kSliderTrackH  = 8.0;
constexpr double kSliderThumbR  = 9.0;
constexpr double kSliderMinW    = 60.0;
constexpr double kNameMaxW      = 150.0;
constexpr double kPctW          = 38.0;
constexpr double kMenuBtnSz     = 28.0;
constexpr double kFooterH       = 40.0;
constexpr double kAppsHeaderH   = 30.0;
constexpr double kDropTitleH    = 26.0;
constexpr double kDropItemH     = 36.0;
constexpr double kDropSubH      = 24.0;  // "PROFILE" subsection header height
constexpr double kScrollW       = 12.0;
constexpr double kScrollStep    = 60.0;

constexpr double kShadOffX  = 2.0;
constexpr double kShadOffY  = 4.0;
constexpr double kShadAlpha = 0.28;

constexpr double kHitPadV = 14.0;
constexpr double kHitPadH = 6.0;

uint64_t now_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

std::string lower_copy(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

struct DeviceInfo {
  std::uint32_t node_id = 0;
  std::string name{};      // display name
  std::string raw_name{};  // node.name for routing matches
  bool is_default = false;
  bool muted = false;
  int volume_pct = 0;
};

struct StreamInfo {
  std::uint32_t node_id = 0;
  int volume_pct = 0;
  bool muted = false;
  std::string app_name{};
  std::string icon_name{};
  std::string app_id{};
  std::string process_binary{};
  std::string node_name{};
  std::string description{};
  std::string routed_raw{};      // counterpart node.name ("" = default)
  std::string routed_display{};  // counterpart display name ("" = default)
};

struct MixerSnapshot {
  std::vector<DeviceInfo> sinks{};
  std::vector<DeviceInfo> sources{};
  std::vector<StreamInfo> out_streams{};
  std::vector<StreamInfo> in_streams{};
};

struct ResolvedName {
  std::string app_name;
  std::string icon_name;
};

// Persistent (across frames) desktop-entry resolution cache. Keyed by the
// stable stream identity, not the recycled node id.
std::unordered_map<std::string, ResolvedName>& resolved_cache() {
  static std::unordered_map<std::string, ResolvedName> s;
  return s;
}

std::string stream_identity(const std::string& app_id, const std::string& bin, const std::string& node,
                            const std::string& desc) {
  return app_id + "|" + bin + "|" + node + "|" + desc;
}

bool is_generic_icon_name(const std::string& n) {
  std::string low = lower_copy(n);
  if (low.empty()) return true;
  if (low == "chromium" || low == "electron") return true;
  if (low == "chromium-browser" || low == "google-chrome" || low == "google-chrome-stable") return true;
  if (low.size() > 8 && low.compare(0, 8, "electron") == 0) return true;
  return false;
}

ResolvedName resolve_stream_display(const std::string& app_name, const std::string& icon_name,
                                    const std::string& app_id, const std::string& bin,
                                    const std::string& node, const std::string& desc) {
  const std::string key = stream_identity(app_id, bin, node, desc);
  auto& cache = resolved_cache();
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  if (cache.size() > 512) cache.clear();

  ResolvedName out;
  out.icon_name = is_generic_icon_name(icon_name) ? std::string{} : icon_name;
  out.app_name = app_name;

  StreamDesktopIds ids;
  ids.process_binary = bin;
  ids.app_id = app_id;
  ids.app_name = app_name;
  ids.node_name = node;
  ids.node_description = desc;
  if (auto dei = resolve_desktop_entry_for_stream(ids)) {
    if (!dei->icon.empty()) out.icon_name = dei->icon;
    if (!dei->name.empty()) out.app_name = dei->name;
  }
  if (out.app_name.empty() || out.app_name == app_name) {
    auto lower = [](const std::string& s) { return lower_copy(s); };
    if (!desc.empty() && !is_generic_runtime_binary(lower(desc))) {
      if (out.app_name.empty()) out.app_name = desc;
    } else if (out.app_name.empty() || is_generic_runtime_binary(lower(out.app_name))) {
      auto candidate = [&](const std::string& s) -> std::string {
        if (s.empty()) return {};
        std::string low = lower(s);
        if (is_generic_runtime_binary(low)) return {};
        // basename + capitalize
        const auto slash = low.find_last_of("/\\");
        if (slash != std::string::npos && slash + 1 < low.size()) low = low.substr(slash + 1);
        // strip common suffixes
        for (const char* suf : {".exe", ".bin", "-bin"}) {
          const std::string sfx = suf;
          if (low.size() > sfx.size() && low.compare(low.size() - sfx.size(), sfx.size(), sfx) == 0)
            low.resize(low.size() - sfx.size());
        }
        if (!low.empty()) low[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(low[0])));
        return low;
      };
      std::string name = candidate(bin);
      if (name.empty()) name = candidate(app_id);
      if (name.empty()) name = candidate(node);
      if (!name.empty()) out.app_name = name;
    }
  }
  if (out.app_name.empty()) out.app_name = "Unknown";
  cache[key] = out;
  return out;
}

struct SnapshotCache {
  MixerSnapshot snap{};
  uint64_t snap_ms = 0;
  std::string link_blob{};
  uint64_t link_ms = 0;
  eh::audio::EngineSettings engine{};
  uint64_t engine_ms = 0;
  eh::audio::PwTopSample top{};
  uint64_t top_ms = 0;
};

SnapshotCache& cache() {
  static SnapshotCache s;
  return s;
}

// Forward: defined beside invalidate_snapshot below; holds engine/`pw-top`
// refreshes while a slider drag is active.
uint64_t& mixer_engine_hold_until_ms();

// Single batched snapshot: one PipeWire snapshot copy + ONE `pw-link -l`
// capture shared by every stream (the old code spawned one subprocess per
// stream per frame). TTL-gated so motion repaints don't re-query.
const MixerSnapshot& cached_snapshot() {
  auto& c = cache();
  const uint64_t now = now_ms();
  if (c.snap_ms != 0 && now - c.snap_ms < 400) {
    t_mixer_last_snap_us = 0;
    t_mixer_last_link_us = 0;
    t_mixer_last_snap_hit = true;
    return c.snap;
  }
  const auto t0 = std::chrono::steady_clock::now();

  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const auto s = pw.snapshot();

  if ((c.link_ms == 0 || now - c.link_ms > 1000) && !(now < mixer_engine_hold_until_ms())) {
    const auto lt0 = std::chrono::steady_clock::now();
    std::string blob;
    if (eh::audio::pw_link_list_capture_stdout(&blob)) {
      c.link_blob = std::move(blob);
      c.link_ms = now;
    }
    t_mixer_last_link_us = mixer_us_between(lt0, std::chrono::steady_clock::now());
  }

  MixerSnapshot out;
  for (const auto& d : s.sinks) {
    DeviceInfo di;
    di.node_id = d.node_id;
    di.raw_name = d.name;
    di.name = d.display_name.empty() ? d.name : d.display_name;
    di.is_default = d.is_default;
    di.muted = d.muted;
    di.volume_pct = d.volume_pct;
    out.sinks.push_back(std::move(di));
  }
  for (const auto& d : s.sources) {
    DeviceInfo di;
    di.node_id = d.node_id;
    di.raw_name = d.name;
    di.name = d.display_name.empty() ? d.name : d.display_name;
    di.is_default = d.is_default;
    di.muted = d.muted;
    di.volume_pct = d.volume_pct;
    out.sources.push_back(std::move(di));
  }
  auto sink_display = [&](const std::string& raw) -> std::string {
    for (const auto& d : out.sinks)
      if (d.raw_name == raw) return d.name;
    return raw;
  };
  auto source_display = [&](const std::string& raw) -> std::string {
    for (const auto& d : out.sources)
      if (d.raw_name == raw) return d.name;
    return raw;
  };
  for (const auto& st : s.output_streams) {
    StreamInfo si;
    si.node_id = st.node_id;
    si.volume_pct = st.volume_pct;
    si.muted = st.muted;
    si.app_id = st.app_id;
    si.process_binary = st.process_binary;
    si.node_name = st.node_name;
    si.description = st.description;
    auto r = resolve_stream_display(st.app_name, st.icon_name, st.app_id, st.process_binary,
                                    st.node_name, st.description);
    si.app_name = r.app_name;
    si.icon_name = r.icon_name;
    si.routed_raw = eh::audio::pw_link_list_playback_sink_for_stream(c.link_blob, st.node_name);
    if (!si.routed_raw.empty()) si.routed_display = sink_display(si.routed_raw);
    out.out_streams.push_back(std::move(si));
  }
  for (const auto& st : s.input_streams) {
    StreamInfo si;
    si.node_id = st.node_id;
    si.volume_pct = st.volume_pct;
    si.muted = st.muted;
    si.app_id = st.app_id;
    si.process_binary = st.process_binary;
    si.node_name = st.node_name;
    si.description = st.description;
    auto r = resolve_stream_display(st.app_name, st.icon_name, st.app_id, st.process_binary,
                                    st.node_name, st.description);
    si.app_name = r.app_name;
    si.icon_name = r.icon_name;
    si.routed_raw = eh::audio::pw_link_list_capture_source_for_stream(c.link_blob, st.node_name);
    if (!si.routed_raw.empty()) si.routed_display = source_display(si.routed_raw);
    out.in_streams.push_back(std::move(si));
  }
  c.snap = std::move(out);
  c.snap_ms = now;
  t_mixer_last_snap_us = mixer_us_between(t0, std::chrono::steady_clock::now());
  t_mixer_last_snap_hit = false;
  return c.snap;
}

const eh::audio::EngineSettings& cached_engine() {
  auto& c = cache();
  const uint64_t now = now_ms();
  // 5s TTL: the footer readout doesn't need 2Hz refresh, and each refresh
  // spawns pw-metadata (see log: footer spikes track these queries).
  // While a slider drag is active the TTL is held indefinitely so a
  // subprocess spawn never lands mid-drag (set/cleared in mixer_motion_at /
  // mixer_release_at); the footer simply shows 5s-stale data for a moment.
  const bool held = now < mixer_engine_hold_until_ms();
  if (c.engine_ms == 0 || ((now - c.engine_ms > 5000) && !held)) {
    c.engine = eh::audio::PipeWireService::instance().query_engine_settings();
    c.engine_ms = now;
  }
  return c.engine;
}

const eh::audio::PwTopSample& cached_top() {
  auto& c = cache();
  const uint64_t now = now_ms();
  // 5s TTL: `pw-top -b -n 2` is a synchronous subprocess spawn off the paint
  // path; the footer DSP/xrun readout is fine at 0.2Hz. Held during drags,
  // same as above.
  const bool held = now < mixer_engine_hold_until_ms();
  if (c.top_ms == 0 || ((now - c.top_ms > 5000) && !held)) {
    c.top = eh::audio::PipeWireService::instance().query_pw_top_batch();
    c.top_ms = now;
  }
  return c.top;
}

void invalidate_snapshot() { cache().snap_ms = 0; }

// Audio card profiles (Bluetooth codecs, ALSA presets, USB DAC modes) via
// `pactl list cards` — two subprocess spawns per refresh, so 10s TTL plus
// the drag hold-off, and warmed once at popup open.
struct CardsCache {
  std::vector<eh::audio::AudioCardProfiles> cards{};
  uint64_t ms = 0;
};

CardsCache& cards_cache() {
  static CardsCache s;
  return s;
}

const std::vector<eh::audio::AudioCardProfiles>& cached_cards() {
  auto& c = cards_cache();
  const uint64_t now = now_ms();
  const bool held = now < mixer_engine_hold_until_ms();
  if (c.ms == 0 || ((now - c.ms > 10000) && !held)) {
    auto bt = eh::audio::PipeWireService::instance().query_audio_cards(true);
    auto rest = eh::audio::PipeWireService::instance().query_audio_cards(false);
    c.cards = std::move(bt);
    c.cards.insert(c.cards.end(), std::make_move_iterator(rest.begin()),
                   std::make_move_iterator(rest.end()));
    c.ms = now;
  }
  return c.cards;
}

void invalidate_cards() { cards_cache().ms = 0; }

// Associate a sink/source node (e.g. "bluez_output.A8_99_DC_57_EF_0D.1") with
// its card ("bluez_card.A8_99_DC_57_EF_0D") via the shared id segment.
const eh::audio::AudioCardProfiles* find_card_for_node(const std::string& node_raw) {
  if (node_raw.empty()) return nullptr;
  for (const auto& card : cached_cards()) {
    const auto dot = card.card_key.find('.');
    const std::string id =
        (dot == std::string::npos) ? card.card_key : card.card_key.substr(dot + 1);
    if (!id.empty() && node_raw.find(id) != std::string::npos) return &card;
  }
  return nullptr;
}

// While a slider drag is active, engine/`pw-top` refreshes are held off so a
// synchronous subprocess spawn never lands mid-drag. Set on every drag
// motion, cleared on release.
uint64_t& mixer_engine_hold_until_ms() {
  static uint64_t s = 0;
  return s;
}

// NOTE: lives in this TU only (single instance). The old header-static ui()
// duplicated state per translation unit, so a tab switch in one TU was
// invisible in another.

struct MixerUiState {
  bool viewOutput = true;
  std::uint32_t routerStream = 0;  // node id with the per-app dropdown open
  bool masterPicker = false;       // default-device picker dropdown open
  double scrollPx = 0.0;           // app-list scroll offset (clamped at use)
  bool dragActive = false;
  enum class DragKind { None, Device, Stream, Scroll } dragKind = DragKind::None;
  std::uint32_t dragId = 0;
  bool dragIsInput = false;
  double dragSx = 0.0;
  double dragSw = 1.0;
  double dragT = 0.0;
  int dragPct = 0;
  double dragStartY = 0.0;      // scrollbar-thumb grab point
  double dragStartScroll = 0.0;  // scrollPx at grab time
  uint64_t lastApplyMs = 0;
};

MixerUiState& ui() {
  static MixerUiState s;
  return s;
}

constexpr double content_w() { return static_cast<double>(kVolumeMixerPopupW) - kPad * 2.0; }
double row_start_x() { return kPad; }
double row_end_x() { return static_cast<double>(kVolumeMixerPopupW) - kPad; }

struct SliderGeom {
  double sx = 0, sy = 0, sw = 0;
};

struct DevRowGeom {
  double y = 0;
  double radioCx = 0;
  double muteX = 0;
  SliderGeom slider{};
};

struct StreamRowGeom {
  double y = 0;
  double muteX = 0;
  double menuX = 0;
  double pctX = 0;
  SliderGeom slider{};
};

struct MixerLayout {
  bool viewOutput = true;
  int devCount = 0;
  int streamCount = 0;
  double segX = 0, segY = 0, segW = 0;
  double outX = 0, outW = 0, inX = 0, inW = 0;
  double headerBottom = 0;
  bool hasMaster = false;
  double masterY = 0;
  double masterSliderX = 0, masterSliderW = 0, masterSliderY = 0;
  double masterMuteX = 0, masterMuteY = 0;
  bool hasMasterMenu = false;  // device-picker chevron (only with 2+ devices)
  double masterMenuX = 0, masterMenuY = 0;
  std::vector<DevRowGeom> devRows{};
  double appsDividerY = 0;
  double streamsTop = 0;
  // Scrollable app viewport (fixed height; rows scroll under it).
  double viewportY = 0, viewportH = 0;
  double contentH = 0;   // streams * kRowH
  double scrollPx = 0;   // clamped offset
  double maxScroll = 0;
  bool scrollable = false;
  double scrollX = 0, scrollW = 0;        // scrollbar track
  double thumbY = 0, thumbH = 0;          // scrollbar thumb
  std::vector<StreamRowGeom> streamRows{};  // y already scroll-adjusted
  // Floating device dropdown (overlay; never shifts layout). Two modes:
  // per-app routing (anchored to the row's swap button) or the master
  // default-device picker (anchored to the master card).
  bool hasDrop = false;
  bool dropIsMaster = false;
  double dropX = 0, dropY = 0, dropW = 0, dropH = 0;
  int dropStreamIdx = -1;
  int dropCount = 0;
  // Profile subsection (master dropdown only): profiles of the default
  // device's card, or 0 when the card is unknown / has a single profile.
  int dropProfileCount = 0;
  double dropProfileTop = 0;
  double footerY = 0;
  double settingsX = 0, settingsW = 0;
  double totalH = 0;
};

double clamp_scroll(double px, double maxScroll) { return std::clamp(px, 0.0, std::max(0.0, maxScroll)); }

// Forward: defined in the hit-testing section below.
const DeviceInfo* master_device(const MixerSnapshot& snap, bool viewOutput);

MixerLayout compute_layout(const MixerSnapshot& snap, bool viewOutput, std::uint32_t routerStream,
                           bool masterPicker, double scrollPx) {
  MixerLayout L;
  L.viewOutput = viewOutput;
  const auto& devices = viewOutput ? snap.sinks : snap.sources;
  const auto& streams = viewOutput ? snap.out_streams : snap.in_streams;
  L.devCount = static_cast<int>(devices.size());
  L.streamCount = static_cast<int>(streams.size());

  const double cx = row_start_x();
  const double cw = content_w();
  const double H = static_cast<double>(kVolumeMixerPopupH);
  L.segX = cx;
  L.segY = 12.0;
  L.segW = cw;
  L.outW = cw * 0.5;
  L.inW = cw - L.outW;
  L.outX = cx;
  L.inX = cx + L.outW;
  L.headerBottom = 12.0 + kHeaderH;

  const double devRowsY = L.headerBottom + 8.0;
  L.hasMaster = !devices.empty();
  double y = devRowsY;
  if (L.hasMaster) {
    L.masterY = y;
    L.masterSliderX = cx + kPad;
    L.masterSliderW = cw - kPad * 2.0 - kPctW - 8.0;
    L.masterSliderY = y + kMasterH - 30.0;
    L.masterMuteX = cx + cw - kPad - kMuteBtnSz;
    L.masterMuteY = y + 12.0;
    // Device-switch chevron: with 2+ devices, or when the default device's
    // card offers switchable profiles (single-BT-headset codec case).
    L.hasMasterMenu = L.devCount > 1;
    if (!L.hasMasterMenu && L.hasMaster) {
      const DeviceInfo* m0 = master_device(snap, viewOutput);
      if (m0) {
        if (const auto* card = find_card_for_node(m0->raw_name))
          if (static_cast<int>(card->profiles.size()) >= 2) L.hasMasterMenu = true;
      }
    }
    L.masterMenuX = L.masterMuteX - 4.0 - kMenuBtnSz;
    L.masterMenuY = L.masterMuteY;
    y += kMasterH + 8.0;
  }
  // Remaining (non-default) devices become compact rows.
  const int extra = std::max(0, L.devCount - 1);
  L.devRows.reserve(static_cast<size_t>(extra));
  for (int i = 0; i < extra; ++i) {
    DevRowGeom r;
    r.y = y + static_cast<double>(i) * kRowH;
    r.radioCx = cx + kPad + kRadioR;
    r.muteX = cx + kPad + kRadioR * 2.0 + 8.0 + kTileSz + 8.0 + kNameMaxW + 8.0;
    const double sx = r.muteX + kMuteBtnSz + 8.0;
    const double sw = std::max(kSliderMinW, row_end_x() - kPad - sx);
    r.slider = {sx, r.y + (kRowH - kSliderTrackH) * 0.5, sw};
  }
  if (extra > 0) y += static_cast<double>(extra) * kRowH + 4.0;

  L.appsDividerY = y + 8.0;
  L.streamsTop = L.appsDividerY + kAppsHeaderH;

  // Fixed footer; the viewport takes whatever is left (min 2 rows).
  L.footerY = H - kPad - kFooterH;
  L.settingsW = 150.0;
  L.settingsX = cx + cw - L.settingsW;
  L.viewportY = L.streamsTop;
  L.viewportH = std::max(2.0 * kRowH, L.footerY - 8.0 - L.streamsTop);

  L.contentH = static_cast<double>(L.streamCount) * kRowH;
  L.maxScroll = std::max(0.0, L.contentH - L.viewportH);
  L.scrollPx = clamp_scroll(scrollPx, L.maxScroll);
  L.scrollable = L.maxScroll > 0.5;
  const double rightGutter = L.scrollable ? kScrollW : 0.0;
  const double rowRight = row_end_x() - kPad - rightGutter;

  L.streamRows.reserve(static_cast<size_t>(L.streamCount));
  for (int i = 0; i < L.streamCount; ++i) {
    StreamRowGeom r;
    r.y = L.streamsTop + static_cast<double>(i) * kRowH - L.scrollPx;
    r.muteX = cx + kPad + kAppIconSz + 8.0 + kNameMaxW + 4.0;
    const double sx = r.muteX + kMuteBtnSz + 8.0;
    r.pctX = rowRight - kPctW - 4.0 - kMenuBtnSz;
    const double sw = std::max(kSliderMinW, r.pctX - 4.0 - sx);
    r.slider = {sx, r.y + (kRowH - kSliderTrackH) * 0.5 - 4.0, sw};
    r.menuX = rowRight - kMenuBtnSz;
    L.streamRows.push_back(r);
  }

  if (L.scrollable) {
    L.scrollX = cx + cw - kScrollW;
    L.scrollW = kScrollW;
    L.thumbH = std::max(24.0, L.viewportH * L.viewportH / std::max(1.0, L.contentH));
    const double travel = std::max(1.0, L.viewportH - L.thumbH);
    L.thumbY = L.viewportY + (L.scrollPx / std::max(1.0, L.maxScroll)) * travel;
  }

  // Floating dropdown: master default-device picker takes precedence over
  // per-app routing (they're mutually exclusive in the click handler).
  L.hasDrop = false;
  L.dropIsMaster = false;
  L.dropStreamIdx = -1;
  const auto& targets = viewOutput ? snap.sinks : snap.sources;
  const int nTargets = static_cast<int>(targets.size());
  L.dropW = cw - 16.0;
  L.dropProfileCount = 0;
  L.dropProfileTop = 0;
  if (masterPicker && L.hasMaster) {
    // Profile subsection: profiles of the current default device's card.
    const DeviceInfo* mdev = master_device(snap, viewOutput);
    const eh::audio::AudioCardProfiles* card =
        mdev ? find_card_for_node(mdev->raw_name) : nullptr;
    const int nProf =
        (card && static_cast<int>(card->profiles.size()) >= 2)
            ? static_cast<int>(card->profiles.size())
            : 0;
    const double ddH = kDropTitleH + static_cast<double>(std::max(1, nTargets)) * kDropItemH +
                       (nProf > 0 ? kDropSubH + static_cast<double>(nProf) * kDropItemH : 0.0) + 10.0;
    const double below = L.masterY + kMasterH + 4.0;
    L.dropX = cx + 8.0;
    // Always hang below the chevron (never flip above): the panel reads as
    // an extension of the button that opened it. Height clamps to the popup.
    L.dropY = below;
    L.dropH = std::max(0.0, std::min(ddH, H - 8.0 - L.dropY));
    L.dropCount = nTargets;
    L.dropProfileCount = nProf;
    L.dropProfileTop =
        L.dropY + kDropTitleH + static_cast<double>(std::max(1, nTargets)) * kDropItemH + kDropSubH;
    L.dropIsMaster = true;
    L.hasDrop = true;
  } else if (routerStream != 0) {
    const double ddH =
        kDropTitleH + static_cast<double>(std::max(1, nTargets)) * kDropItemH + 10.0;
    for (int i = 0; i < L.streamCount; ++i) {
      if (streams[static_cast<size_t>(i)].node_id != routerStream) continue;
      const auto& g = L.streamRows[static_cast<size_t>(i)];
      L.dropX = cx + 8.0;
      const double below = g.y + kRowH + 4.0;
      // Same as the master picker: always below the button, clamped to fit.
      L.dropY = below;
      L.dropH = std::max(0.0, std::min(ddH, H - 8.0 - L.dropY));
      L.dropStreamIdx = i;
      L.dropCount = nTargets;
      L.hasDrop = true;
      break;
    }
  }

  L.totalH = H;
  return L;
}

std::string ellipsize(cairo_t* cr, const std::string& text, double maxW) {
  const auto t0 = std::chrono::steady_clock::now();
  auto done = [&](std::string r) {
    t_mixer_text_us += mixer_us_between(t0, std::chrono::steady_clock::now());
    t_mixer_text_n++;
    return r;
  };
  if (text.empty() || maxW <= 0) return done({});
  cairo_text_extents_t te{};
  cairo_text_extents(cr, text.c_str(), &te);
  if (te.x_advance <= maxW) return done(text);
  // Binary search the longest fitting prefix + ellipsis (log N extents,
  // not the old per-character O(N^2) loop).
  size_t lo = 0, hi = text.size();
  std::string best = text.substr(0, 1) + "...";
  while (lo < hi) {
    size_t mid = (lo + hi + 1) / 2;
    std::string cand = text.substr(0, mid) + "...";
    cairo_text_extents(cr, cand.c_str(), &te);
    if (te.x_advance <= maxW) {
      lo = mid;
      best = std::move(cand);
    } else {
      hi = mid - 1;
    }
  }
  return done(best);
}

const char* volume_glyph(int pct, bool muted) {
  if (muted || pct <= 0) return "volume_off";
  if (pct < 34) return "volume_down";
  return "volume_up";
}

const char* device_glyph(const std::string& name) {  const std::string low = lower_copy(name);
  if (low.find("bluez") != std::string::npos || low.find("bluetooth") != std::string::npos ||
      low.find("headphone") != std::string::npos || low.find("headset") != std::string::npos ||
      low.find("wh-") != std::string::npos || low.find("buds") != std::string::npos)
    return "headphones";
  if (low.find("hdmi") != std::string::npos || low.find("displayport") != std::string::npos) return "tv";
  if (low.find("usb") != std::string::npos) return "usb";
  if (low.find("mic") != std::string::npos || low.find("input") != std::string::npos ||
      low.find("capture") != std::string::npos)
    return "mic";
  return "speaker";
}

void paint_card(cairo_t* cr, double x, double y, double w, double h, double r, double fr, double fg,
                double fb, double fa) {
  eh::shell::shared::rounded_rect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, fr, fg, fb, fa);
  cairo_fill(cr);
}

void paint_hover(cairo_t* cr, double x, double y, double w, double h, double r, double hr, double hg,
                 double hb) {
  eh::shell::shared::rounded_rect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, hr, hg, hb, 0.09);
  cairo_fill(cr);
}

void paint_slider(cairo_t* cr, double x, double y, double w, double t, bool hov, double dimR,
                  double dimG, double dimB, double accR, double accG, double accB, double fillR,
                  double fillG, double fillB) {
  const auto t0 = std::chrono::steady_clock::now();
  const double tC = std::clamp(t, 0.0, 1.0);
  const double fillW = w * tC;
  const double midY = y + kSliderTrackH * 0.5;
  const double thumbCx = std::clamp(x + w * tC, x + kSliderThumbR, x + w - kSliderThumbR);
  // 100% detent: the track spans 0..150%, with an overdrive zone past the mark.
  const double markX = x + w * (100.0 / 150.0);

  eh::shell::shared::rounded_rect(cr, x, y, w, kSliderTrackH, kSliderTrackH * 0.5);
  cairo_set_source_rgba(cr, dimR, dimG, dimB, hov ? 0.65 : 0.55);
  cairo_fill(cr);

  if (fillW > 1.0) {
    const double accW = std::min(fillW, markX - x);
    if (accW > 1.0) {
      eh::shell::shared::rounded_rect(cr, x, y, accW, kSliderTrackH, kSliderTrackH * 0.5);
      cairo_set_source_rgba(cr, accR, accG, accB, 0.90);
      cairo_fill(cr);
    }
    if (fillW > markX - x + 1.0) {
      cairo_rectangle(cr, markX, y, fillW - (markX - x), kSliderTrackH);
      cairo_set_source_rgba(cr, 1.0, 0.55, 0.15, 0.92);
      cairo_fill(cr);
    }
  }

  // 100% tick.
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.55);
  cairo_set_line_width(cr, 1.5);
  cairo_move_to(cr, markX, y - 3.0);
  cairo_line_to(cr, markX, y + kSliderTrackH + 3.0);
  cairo_stroke(cr);

  cairo_new_path(cr);
  cairo_arc(cr, thumbCx, midY, hov ? kSliderThumbR + 1.0 : kSliderThumbR, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, std::min(1.0, fillR), std::min(1.0, fillG), std::min(1.0, fillB), 0.98);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, accR, accG, accB, 0.45);
  cairo_set_line_width(cr, 1.5);
  cairo_stroke(cr);
  t_mixer_slider_us += mixer_us_between(t0, std::chrono::steady_clock::now());
  t_mixer_slider_n++;
}

// Per-app device-switch glyph: bundled Eva/Lucide-style `UI/swap.svg`
// (bidirectional exchange arrows = "switch output/input"). Loaded once and
// tinted via mask to the theme color; falls back to the Material Symbols
// "swap_horiz" ligature when the asset is unavailable.
cairo_surface_t* swap_icon_surface() {
  static cairo_surface_t* s = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    s = eh::shell::asset::load_asset_svg("UI", "swap.svg", 64);
  }
  if (s && cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) return nullptr;
  return s;
}

void paint_icon_tile(cairo_t* cr, double x, double y, const char* glyph, bool active, double accR,
                     double accG, double accB, double txtR, double txtG, double txtB) {
  eh::shell::shared::rounded_rect(cr, x, y, kTileSz, kTileSz, kTileR);
  if (active)
    cairo_set_source_rgba(cr, accR, accG, accB, 0.20);
  else
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
  cairo_fill(cr);
  eh::shell::draw_material_glyph(cr, x + kTileSz * 0.5, y + kTileSz * 0.5, kDevIconSz, glyph,
                                 active ? accR : txtR, active ? accG : txtG, active ? accB : txtB,
                                 active ? 0.95 : 0.70);
}

}  // namespace

int volume_mixer_popup_content_height() {
  // Fixed height: the app list scrolls inside its viewport instead of growing
  // the popup, so this never changes after open.
  return kVolumeMixerPopupH;
}

void volume_mixer_notify_popup_open() {
  eh::audio::PipeWireService::instance().start();
  auto& u = ui();
  u.dragActive = false;
  u.dragKind = MixerUiState::DragKind::None;
  u.dragId = 0;
  u.scrollPx = 0.0;
  u.routerStream = 0;
  u.masterPicker = false;
  invalidate_snapshot();
  (void)cached_cards();  // warm pactl card list off the first frame (10s TTL)
  const MixerSnapshot& snap = cached_snapshot();
  const double snap_ms = t_mixer_last_snap_us / 1000.0;
  if (MixerLog::instance().enabled()) {
    const auto& devs = u.viewOutput ? snap.sinks : snap.sources;
    const auto& streams = u.viewOutput ? snap.out_streams : snap.in_streams;
    MixerLog::instance().report_open(static_cast<int>(devs.size()), static_cast<int>(streams.size()),
                                     snap_ms);
  }
}

namespace {
enum class Hit {
  None,
  OutSeg,
  InSeg,
  MasterMute,
  MasterSlider,
  MasterPicker,  // master-card chevron: default output/input device picker
  DevRadio,
  DevMute,
  DevSlider,
  StreamMute,
  StreamSlider,
  StreamMenu,
  DropItem,   // dropdown device row: select default / route stream
  DropProfile,  // dropdown profile row: apply card profile to default device
  DropSwallow,  // dropdown chrome (titles/padding): absorb, do nothing
  ScrollThumb,
  ScrollTrack,
  FooterSettings,
};

struct HitResult {
  Hit hit = Hit::None;
  int index = -1;
  double t = 0.0;
};

HitResult hit_test(const MixerLayout& L, const MixerSnapshot& snap, double x, double y) {
  HitResult r;
  const double cx = row_start_x();
  const double cw = content_w();
  if (x < 0 || x >= static_cast<double>(kVolumeMixerPopupW) || y < 0) return r;

  if (y >= L.segY && y < L.segY + kSegH) {
    if (x >= L.outX && x < L.outX + L.outW) { r.hit = Hit::OutSeg; return r; }
    if (x >= L.inX && x < L.inX + L.inW) {
      if (!snap.sources.empty() || !ui().viewOutput) { r.hit = Hit::InSeg; return r; }
    }
  }
  if (L.hasMaster) {
    if (L.hasMasterMenu && x >= L.masterMenuX && x < L.masterMenuX + kMenuBtnSz &&
        y >= L.masterMenuY && y < L.masterMenuY + kMenuBtnSz) {
      r.hit = Hit::MasterPicker;
      return r;
    }
    if (x >= L.masterMuteX && x < L.masterMuteX + kMuteBtnSz && y >= L.masterMuteY &&
        y < L.masterMuteY + kMuteBtnSz) {
      r.hit = Hit::MasterMute;
      return r;
    }
    if (x >= L.masterSliderX - kHitPadH && x <= L.masterSliderX + L.masterSliderW + kHitPadH &&
        y >= L.masterSliderY - kHitPadV && y <= L.masterSliderY + kSliderTrackH + kHitPadV) {
      r.hit = Hit::MasterSlider;
      r.t = std::clamp((x - L.masterSliderX) / std::max(1.0, L.masterSliderW), 0.0, 1.0);
      return r;
    }
  }
  // Floating dropdown overlay: test before rows/devices since it floats above
  // them. Items route, everything else absorbs the click.
  if (L.hasDrop && x >= L.dropX && x < L.dropX + L.dropW && y >= L.dropY && y < L.dropY + L.dropH) {
    const int idx = static_cast<int>((y - L.dropY - kDropTitleH) / kDropItemH);
    if (idx >= 0 && idx < L.dropCount) { r.hit = Hit::DropItem; r.index = idx; return r; }
    if (L.dropProfileCount > 0) {
      const int pidx = static_cast<int>((y - L.dropProfileTop) / kDropItemH);
      if (pidx >= 0 && pidx < L.dropProfileCount &&
          L.dropProfileTop + static_cast<double>(pidx) * kDropItemH + kDropItemH <=
              L.dropY + L.dropH + 1.0) {
        r.hit = Hit::DropProfile;
        r.index = pidx;
        return r;
      }
    }
    r.hit = Hit::DropSwallow;
    return r;
  }
  for (size_t i = 0; i < L.devRows.size(); ++i) {
    const auto& dr = L.devRows[i];
    if (y < dr.y || y >= dr.y + kRowH) continue;
    if (x >= dr.muteX && x < dr.muteX + kMuteBtnSz) { r.hit = Hit::DevMute; r.index = (int)i; return r; }
    if (x >= dr.slider.sx - kHitPadH && x <= dr.slider.sx + dr.slider.sw + kHitPadH &&
        y >= dr.slider.sy - kHitPadV && y <= dr.slider.sy + kSliderTrackH + kHitPadV) {
      r.hit = Hit::DevSlider;
      r.index = (int)i;
      r.t = std::clamp((x - dr.slider.sx) / std::max(1.0, dr.slider.sw), 0.0, 1.0);
      return r;
    }
    if (x >= cx && x <= cx + cw) { r.hit = Hit::DevRadio; r.index = (int)i; return r; }
  }
  for (size_t i = 0; i < L.streamRows.size(); ++i) {
    const auto& sr = L.streamRows[i];
    if (y < sr.y || y >= sr.y + kRowH) continue;
    // Scrolled-out rows are not interactive (they're clipped in paint).
    if (sr.y + kRowH <= L.viewportY || sr.y >= L.viewportY + L.viewportH) continue;
    if (x >= sr.muteX && x < sr.muteX + kMuteBtnSz) { r.hit = Hit::StreamMute; r.index = (int)i; return r; }
    if (x >= sr.menuX && x < sr.menuX + kMenuBtnSz) { r.hit = Hit::StreamMenu; r.index = (int)i; return r; }
    if (x >= sr.slider.sx - kHitPadH && x <= sr.slider.sx + sr.slider.sw + kHitPadH &&
        y >= sr.slider.sy - kHitPadV && y <= sr.slider.sy + kSliderTrackH + kHitPadV) {
      r.hit = Hit::StreamSlider;
      r.index = (int)i;
      r.t = std::clamp((x - sr.slider.sx) / std::max(1.0, sr.slider.sw), 0.0, 1.0);
      return r;
    }
  }
  // Scrollbar (only when the list overflows the viewport).
  if (L.scrollable && x >= L.scrollX && x < L.scrollX + L.scrollW && y >= L.viewportY &&
      y < L.viewportY + L.viewportH) {
    if (y >= L.thumbY && y < L.thumbY + L.thumbH) { r.hit = Hit::ScrollThumb; return r; }
    r.hit = Hit::ScrollTrack;
    r.t = (y < L.thumbY) ? -1.0 : 1.0;  // above thumb: page up, else page down
    return r;
  }
  if (y >= L.footerY && y < L.footerY + kFooterH && x >= L.settingsX && x < L.settingsX + L.settingsW) {
    r.hit = Hit::FooterSettings;
    return r;
  }
  return r;
}

const DeviceInfo* master_device(const MixerSnapshot& snap, bool viewOutput) {
  const auto& devs = viewOutput ? snap.sinks : snap.sources;
  if (devs.empty()) return nullptr;
  for (const auto& d : devs)
    if (d.is_default) return &d;
  return &devs.front();
}

void apply_device_volume(std::uint32_t id, double t) {
  eh::audio::PipeWireService::instance().set_node_volume(id, std::clamp(t, 0.0, 1.0));
  invalidate_snapshot();
}

void begin_drag(MixerUiState::DragKind kind, std::uint32_t id, bool isInput, double sx, double sw, double t) {
  auto& u = ui();
  u.dragActive = true;
  u.dragKind = kind;
  u.dragId = id;
  u.dragIsInput = isInput;
  u.dragSx = sx;
  u.dragSw = std::max(1.0, sw);
  u.dragT = std::clamp(t, 0.0, 1.0);
  // Track fraction maps to 0..150% volume (100% detent + overdrive zone).
  u.dragPct = std::clamp(static_cast<int>(std::lround(u.dragT * 150.0)), 0, 150);
  u.lastApplyMs = 0;
}

// Resize helper: re-create at the expansion-aware height (same pattern as the
// control-center resize: creation placement is always correct, live set_size
// would keep the wrong edge on this compositor).
void mixer_resize_popup(DockApp& app, uint32_t serial) {
  const int wantH = volume_mixer_popup_content_height();
  if (wantH == app.popupH) return;
  if (wantH < app.popupH) return;  // shrinking settles via next open; avoids flicker
  popup_open_volume_mixer(app, app.popupAnchorX, serial);
}

}  // namespace

// Coordinate-only core shared by Dock/Panel/Taskbar. Dock wrappers below add
// outside-close + resize; paint lives in the .tpp thin adapters' target here.

namespace detail {

const char* hit_name(Hit h) {
  switch (h) {
    case Hit::None: return "none";
    case Hit::OutSeg: return "out-seg";
    case Hit::InSeg: return "in-seg";
    case Hit::MasterMute: return "master-mute";
    case Hit::MasterSlider: return "master-slider";
    case Hit::MasterPicker: return "master-picker";
    case Hit::DevRadio: return "dev-radio";
    case Hit::DevMute: return "dev-mute";
    case Hit::DevSlider: return "dev-slider";
    case Hit::StreamMute: return "stream-mute";
    case Hit::StreamSlider: return "stream-slider";
    case Hit::StreamMenu: return "stream-menu";
    case Hit::DropItem: return "drop-item";
    case Hit::DropProfile: return "drop-profile";
    case Hit::DropSwallow: return "drop-swallow";
    case Hit::ScrollThumb: return "scroll-thumb";
    case Hit::ScrollTrack: return "scroll-track";
    case Hit::FooterSettings: return "footer-settings";
  }
  return "?";
}

// Reports slow input handling (PipeWire round-trips show up here).
struct ClickTimer {
  const char* action = "none";
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
  ~ClickTimer() {
    if (!MixerLog::instance().enabled()) return;
    const double ms = mixer_us_between(t0, std::chrono::steady_clock::now()) / 1000.0;
    MixerLog::instance().report_click(action, ms);
  }
};

bool mixer_click_at(double x, double y, double popupH) {
  ClickTimer ck;
  const double W = static_cast<double>(kVolumeMixerPopupW);
  if (x < 0 || x >= W || y < 0 || y >= popupH) return false;
  auto& pw = eh::audio::PipeWireService::instance();
  pw.start();
  const MixerSnapshot& snap = cached_snapshot();
  MixerLayout L = compute_layout(snap, ui().viewOutput, ui().routerStream, ui().masterPicker, ui().scrollPx);
  const HitResult h = hit_test(L, snap, x, y);
  ck.action = hit_name(h.hit);
  const auto& devices = ui().viewOutput ? snap.sinks : snap.sources;
  const auto& streams = ui().viewOutput ? snap.out_streams : snap.in_streams;

  // An open dropdown captures: items act, its own chrome absorbs, any other
  // click dismisses it (and does nothing else, so the dismiss click can't
  // accidentally retune a slider underneath).
  const bool dropOpen = ui().routerStream != 0 || ui().masterPicker;
  if (dropOpen && h.hit != Hit::StreamMenu && h.hit != Hit::MasterPicker) {
    if (h.hit == Hit::DropProfile && ui().masterPicker) {
      if (const DeviceInfo* m = master_device(snap, ui().viewOutput)) {
        if (const auto* card = find_card_for_node(m->raw_name)) {
          if (h.index >= 0 && static_cast<size_t>(h.index) < card->profiles.size()) {
            const auto& opt = card->profiles[static_cast<size_t>(h.index)];
            eh::audio::PipeWireService::instance().apply_card_profile(
                card->card_index, card->card_key, opt.key, opt.description);
            invalidate_cards();
            invalidate_snapshot();
          }
        }
      }
      ui().routerStream = 0;
      ui().masterPicker = false;
      return false;
    }
    if (h.hit == Hit::DropItem) {
      const auto& targets = ui().viewOutput ? snap.sinks : snap.sources;
      if (h.index >= 0 && static_cast<size_t>(h.index) < targets.size()) {
        if (ui().masterPicker) {
          if (ui().viewOutput)
            pw.set_default_sink(targets[static_cast<size_t>(h.index)].node_id);
          else
            pw.set_default_source(targets[static_cast<size_t>(h.index)].node_id);
        } else if (ui().routerStream != 0) {
          if (ui().viewOutput)
            pw.route_playback_stream_to_sink(ui().routerStream,
                                             targets[static_cast<size_t>(h.index)].node_id);
          else
            pw.route_capture_stream_to_source(ui().routerStream,
                                              targets[static_cast<size_t>(h.index)].node_id);
        }
        invalidate_snapshot();
      }
      ui().routerStream = 0;
      ui().masterPicker = false;
      return false;
    }
    if (h.hit != Hit::DropSwallow) {
      ui().routerStream = 0;
      ui().masterPicker = false;
    }
    return false;
  }

  switch (h.hit) {
    case Hit::OutSeg:
      if (!ui().viewOutput) {
        ui().viewOutput = true;
        ui().routerStream = 0;
        ui().masterPicker = false;
        ui().scrollPx = 0.0;
        return true;
      }
      return false;
    case Hit::InSeg:
      if (ui().viewOutput && !snap.sources.empty()) {
        ui().viewOutput = false;
        ui().routerStream = 0;
        ui().masterPicker = false;
        ui().scrollPx = 0.0;
        return true;
      }
      return false;
    case Hit::MasterMute: {
      if (const DeviceInfo* m = master_device(snap, ui().viewOutput)) pw.set_node_mute(m->node_id, !m->muted);
      invalidate_snapshot();
      return false;
    }
    case Hit::MasterSlider: {
      if (const DeviceInfo* m = master_device(snap, ui().viewOutput)) {
        apply_device_volume(m->node_id, h.t * 1.5);
        begin_drag(MixerUiState::DragKind::Device, m->node_id, !ui().viewOutput, L.masterSliderX,
                   L.masterSliderW, h.t);
      }
      return false;
    }
    case Hit::MasterPicker: {
      ui().masterPicker = !ui().masterPicker;
      ui().routerStream = 0;
      return false;
    }
    case Hit::DevRadio: {
      const size_t di = static_cast<size_t>(h.index + 1);  // row 0 is the master card
      if (di < devices.size()) {
        if (ui().viewOutput)
          pw.set_default_sink(devices[di].node_id);
        else
          pw.set_default_source(devices[di].node_id);
        invalidate_snapshot();
      }
      return false;
    }
    case Hit::DevMute: {
      const size_t di = static_cast<size_t>(h.index + 1);
      if (di < devices.size()) {
        pw.set_node_mute(devices[di].node_id, !devices[di].muted);
        invalidate_snapshot();
      }
      return false;
    }
    case Hit::DevSlider: {
      const size_t di = static_cast<size_t>(h.index + 1);
      if (di < devices.size() && h.index >= 0 &&
          static_cast<size_t>(h.index) < L.devRows.size()) {
        apply_device_volume(devices[di].node_id, h.t * 1.5);
        const auto& g = L.devRows[static_cast<size_t>(h.index)];
        begin_drag(MixerUiState::DragKind::Device, devices[di].node_id, !ui().viewOutput,
                   g.slider.sx, g.slider.sw, h.t);
      }
      return false;
    }
    case Hit::StreamMute: {
      if (h.index >= 0 && static_cast<size_t>(h.index) < streams.size()) {
        const auto& st = streams[static_cast<size_t>(h.index)];
        pw.set_node_mute(st.node_id, !st.muted);
        invalidate_snapshot();
      }
      return false;
    }
    case Hit::StreamSlider: {
      if (h.index >= 0 && static_cast<size_t>(h.index) < streams.size() &&
          static_cast<size_t>(h.index) < L.streamRows.size()) {
        const auto& st = streams[static_cast<size_t>(h.index)];
        pw.set_node_volume(st.node_id, h.t * 1.5);
        invalidate_snapshot();
        const auto& g = L.streamRows[static_cast<size_t>(h.index)];
        begin_drag(MixerUiState::DragKind::Stream, st.node_id, !ui().viewOutput, g.slider.sx,
                   g.slider.sw, h.t);
      }
      return false;
    }
    case Hit::StreamMenu: {
      if (h.index >= 0 && static_cast<size_t>(h.index) < streams.size()) {
        const auto& st = streams[static_cast<size_t>(h.index)];
        ui().routerStream = (ui().routerStream == st.node_id) ? 0 : st.node_id;
        ui().masterPicker = false;
      }
      return false;
    }
    case Hit::DropItem:
    case Hit::DropProfile:
    case Hit::DropSwallow:
      return false;  // handled by the capture above; kept for exhaustiveness
    case Hit::ScrollThumb: {
      auto& u = ui();
      u.dragActive = true;
      u.dragKind = MixerUiState::DragKind::Scroll;
      u.dragId = 0;
      u.dragStartY = y;
      u.dragStartScroll = L.scrollPx;
      u.routerStream = 0;
      u.masterPicker = false;
      return false;
    }
    case Hit::ScrollTrack: {
      auto& u = ui();
      u.scrollPx = clamp_scroll(L.scrollPx + h.t * L.viewportH, L.maxScroll);
      u.routerStream = 0;
      u.masterPicker = false;
      return false;
    }
    case Hit::FooterSettings:
      eh::settings::request_launch_settings();
      return false;
    case Hit::None:
      return false;
  }
  return false;
}

void mixer_motion_at(double x, double y) {
  auto& u = ui();
  if (!u.dragActive) return;
  mixer_engine_hold_until_ms() = now_ms() + 400;
  if (u.dragKind == MixerUiState::DragKind::Scroll) {
    const MixerSnapshot& snap = cached_snapshot();
    MixerLayout L = compute_layout(snap, u.viewOutput, u.routerStream, u.masterPicker, u.scrollPx);
    const double ratio = L.contentH / std::max(1.0, L.viewportH);
    u.scrollPx = clamp_scroll(u.dragStartScroll + (y - u.dragStartY) * ratio, L.maxScroll);
    return;
  }
  const double t = std::clamp((x - u.dragSx) / u.dragSw, 0.0, 1.0);
  u.dragT = t;
  u.dragPct = std::clamp(static_cast<int>(std::lround(t * 150.0)), 0, 150);
  const uint64_t now = now_ms();
  if (u.lastApplyMs == 0 || now - u.lastApplyMs >= 16) {
    const auto pw0 = std::chrono::steady_clock::now();
    eh::audio::PipeWireService::instance().set_node_volume(u.dragId, t * 1.5);
    MixerLog::instance().note_motion_pw(
        mixer_us_between(pw0, std::chrono::steady_clock::now()));
    u.lastApplyMs = now;
  }
}

void mixer_release_at(double x) {
  auto& u = ui();
  if (!u.dragActive) return;
  mixer_engine_hold_until_ms() = 0;
  if (u.dragKind == MixerUiState::DragKind::Scroll) {
    u.dragActive = false;
    u.dragKind = MixerUiState::DragKind::None;
    return;
  }
  const double t = std::clamp((x - u.dragSx) / u.dragSw, 0.0, 1.0);
  eh::audio::PipeWireService::instance().set_node_volume(u.dragId, t * 1.5);
  u.dragActive = false;
  u.dragKind = MixerUiState::DragKind::None;
  u.dragId = 0;
  invalidate_snapshot();
}

bool mixer_axis_at(double x, double y, double deltaPx) {
  const MixerSnapshot& snap = cached_snapshot();
  MixerLayout L = compute_layout(snap, ui().viewOutput, ui().routerStream, ui().masterPicker, ui().scrollPx);
  HitResult h = hit_test(L, snap, x, y);
  auto& pw = eh::audio::PipeWireService::instance();
  const auto& devices = ui().viewOutput ? snap.sinks : snap.sources;
  const auto& streams = ui().viewOutput ? snap.out_streams : snap.in_streams;
  // Scrolling the app list: anywhere in the viewport that isn't a slider.
  // Any scroll dismisses an open dropdown.
  const bool inViewport = x >= row_start_x() && x < row_start_x() + content_w() && y >= L.viewportY &&
                          y < L.viewportY + L.viewportH;
  if (h.hit != Hit::MasterSlider && h.hit != Hit::MasterMute && h.hit != Hit::DevSlider &&
      h.hit != Hit::DevMute && h.hit != Hit::StreamSlider && h.hit != Hit::StreamMute) {
    if (inViewport && L.scrollable) {
      auto& u = ui();
      u.scrollPx = clamp_scroll(L.scrollPx + (deltaPx > 0.0 ? kScrollStep : -kScrollStep), L.maxScroll);
      u.routerStream = 0;
      return true;
    }
    if (h.hit == Hit::DropItem || h.hit == Hit::DropSwallow || h.hit == Hit::ScrollThumb ||
        h.hit == Hit::ScrollTrack)
      return true;
    return false;
  }
  const int dPct = static_cast<int>(std::lround(-deltaPx / 20.0 * 5.0));
  if (dPct == 0) return true;
  auto bump = [&](int cur) { return std::clamp(cur + dPct, 0, 150) / 100.0; };
  switch (h.hit) {
    case Hit::MasterSlider:
    case Hit::MasterMute:
      if (const DeviceInfo* m = master_device(snap, ui().viewOutput)) {
        pw.set_node_volume(m->node_id, bump(m->volume_pct));
        invalidate_snapshot();
      }
      return true;
    case Hit::DevSlider:
    case Hit::DevMute: {
      const size_t di = static_cast<size_t>(h.index + 1);
      if (di < devices.size()) {
        pw.set_node_volume(devices[di].node_id, bump(devices[di].volume_pct));
        invalidate_snapshot();
      }
      return true;
    }
    case Hit::StreamSlider:
    case Hit::StreamMute:
      if (h.index >= 0 && static_cast<size_t>(h.index) < streams.size()) {
        pw.set_node_volume(streams[static_cast<size_t>(h.index)].node_id,
                           bump(streams[static_cast<size_t>(h.index)].volume_pct));
        invalidate_snapshot();
      }
      return true;
    default:
      return false;
  }
}

const cairo_surface_t* stream_icon_surface(eh::icons::IconCache& icons, const StreamInfo& st) {
  const auto t0 = std::chrono::steady_clock::now();
  eh::shell::mixer_icon::StreamIconIds ids{};
  ids.icon_name = st.icon_name;
  ids.app_id = st.app_id;
  ids.process_binary = st.process_binary;
  ids.app_name = st.app_name;
  const cairo_surface_t* out = nullptr;
  if (const auto* ic = eh::shell::mixer_icon::resolve_mixer_stream_theme_icon(icons, ids))
    if (ic->surface) out = ic->surface;
  t_mixer_icon_us += mixer_us_between(t0, std::chrono::steady_clock::now());
  t_mixer_icon_n++;
  return out;
}

void paint_stream_app_icon(cairo_t* cr, eh::icons::IconCache* icons, const StreamInfo& st, double cx,
                           double cy, double size) {  if (icons) {
    if (const auto* surf = stream_icon_surface(*icons, st)) {
      auto* msurf = const_cast<cairo_surface_t*>(surf);
      const int iw = cairo_image_surface_get_width(msurf);
      const int ih = cairo_image_surface_get_height(msurf);
      if (iw > 0 && ih > 0) {
        const double sc = std::min(size / static_cast<double>(iw), size / static_cast<double>(ih));
        const double dw = static_cast<double>(iw) * sc;
        const double dh = static_cast<double>(ih) * sc;
        cairo_save(cr);
        cairo_translate(cr, cx - dw * 0.5, cy - dh * 0.5);
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, msurf, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        return;
      }
    }
  }
  eh::shell::draw_material_glyph(cr, cx, cy, size * 0.6, "apps", 0.78, 0.78, 0.78, 0.78);
}

// Device-switch button glyph: tinted swap.svg silhouette, ligature fallback.
void paint_swap_glyph(cairo_t* cr, double cx, double cy, double size, double r, double g, double b,
                      double a) {
  if (cairo_surface_t* s = swap_icon_surface()) {
    const int iw = cairo_image_surface_get_width(s);
    const int ih = cairo_image_surface_get_height(s);
    if (iw > 0 && ih > 0) {
      const double k = size / static_cast<double>(std::max(iw, ih));
      cairo_save(cr);
      cairo_set_source_rgba(cr, r, g, b, a);
      cairo_translate(cr, cx - iw * k * 0.5, cy - ih * k * 0.5);
      cairo_scale(cr, k, k);
      cairo_mask_surface(cr, s, 0, 0);
      cairo_restore(cr);
      return;
    }
  }
  eh::shell::draw_material_glyph(cr, cx, cy, size, "swap_horiz", r, g, b, a);
}

void mixer_paint(cairo_t* cr, const eh::config::ShellConfig& sc, const MixerView& v) {
  using perf_clock = std::chrono::steady_clock;
  t_mixer_icon_us = 0;
  t_mixer_icon_n = 0;
  t_mixer_text_us = 0;
  t_mixer_text_n = 0;
  t_mixer_slider_us = 0;
  t_mixer_slider_n = 0;
  const auto pf0 = perf_clock::now();
  const MixerSnapshot& snap = cached_snapshot();
  const auto pf1 = perf_clock::now();
  const MixerUiState& u = ui();
  const auto& devices = u.viewOutput ? snap.sinks : snap.sources;
  const auto& streams = u.viewOutput ? snap.out_streams : snap.in_streams;
  const MixerLayout L = compute_layout(snap, u.viewOutput, u.routerStream, u.masterPicker, u.scrollPx);
  const auto pf2 = perf_clock::now();

  const double W = static_cast<double>(kVolumeMixerPopupW);
  const double H = static_cast<double>(v.popupH);
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);

  cairo_save(cr);
  eh::shell::shared::rounded_rect(cr, kShadOffX, kShadOffY, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, 0, 0, 0, kShadAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  eh::shell::shared::rounded_rect(cr, 0, 0, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, mc.dockFillR * 0.30, mc.dockFillG * 0.30, mc.dockFillB * 0.30,
                        0.92 * sc.appearance.overlayOpacityWidgetCard);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
  const auto pf3 = perf_clock::now();

  const double cx = row_start_x();
  const double cw = content_w();
  const int devExtra = static_cast<int>(L.devRows.size());
  const int streamCount = static_cast<int>(streams.size());

  auto hovering = [&](double x0, double y0, double x1, double y1) {
    return v.onPopup && v.px >= x0 && v.px < x1 && v.py >= y0 && v.py < y1;
  };

  // ---- segmented Output | Input control
  {
    eh::shell::shared::rounded_rect(cr, L.segX, L.segY, L.segW, kSegH, kSegR);
    cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.25);
    cairo_fill(cr);
    const bool outSel = u.viewOutput;
    const bool hasSources = !snap.sources.empty();
    // selected segment
    const double selX = outSel ? L.outX : L.inX;
    const double selW = outSel ? L.outW : L.inW;
    eh::shell::shared::rounded_rect(cr, selX + 2.0, L.segY + 2.0, selW - 4.0, kSegH - 4.0, kSegR - 2.0);
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.22);
    cairo_fill(cr);
    if (hovering(L.outX, L.segY, L.outX + L.outW, L.segY + kSegH))
      paint_hover(cr, L.outX + 2.0, L.segY + 2.0, L.outW - 4.0, kSegH - 4.0, kSegR - 2.0, mc.accentR,
                  mc.accentG, mc.accentB);
    if (hovering(L.inX, L.segY, L.inX + L.inW, L.segY + kSegH))
      paint_hover(cr, L.inX + 2.0, L.segY + 2.0, L.inW - 4.0, kSegH - 4.0, kSegR - 2.0, mc.accentR,
                  mc.accentG, mc.accentB);

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    auto seg_label = [&](double sx, const char* glyph, const std::string& label, bool sel,
                         bool dimmed) {
      const double a = dimmed ? 0.35 : (sel ? 0.95 : 0.65);
      eh::shell::draw_material_glyph(cr, sx + 20.0, L.segY + kSegH * 0.5, 15.0, glyph,
                                     sel ? mc.accentR : mc.textR, sel ? mc.accentG : mc.textG,
                                     sel ? mc.accentB : mc.textB, a);
      cairo_set_source_rgba(cr, sel ? mc.accentR : mc.textR, sel ? mc.accentG : mc.textG,
                            sel ? mc.accentB : mc.textB, a);
      cairo_move_to(cr, sx + 40.0, L.segY + kSegH * 0.5 + 5.0);
      cairo_show_text(cr, label.c_str());
    };
    seg_label(L.outX, "volume_up", "Output · " + std::to_string(snap.sinks.size()), outSel,
              false);
    seg_label(L.inX, "mic", "Input · " + std::to_string(snap.sources.size()), !outSel,
              !hasSources);
  }

  // divider under header
  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, cx, L.headerBottom);
  cairo_line_to(cr, cx + cw, L.headerBottom);
  cairo_stroke(cr);
  const auto pf4 = perf_clock::now();

  // ---- master (default device) card
  if (const DeviceInfo* m = master_device(snap, u.viewOutput)) {
    const bool mHov = hovering(cx, L.masterY, cx + cw, L.masterY + kMasterH);
    if (mHov) paint_hover(cr, cx, L.masterY, cw, kMasterH, 12.0, mc.accentR, mc.accentG, mc.accentB);
    paint_card(cr, cx, L.masterY, cw, kMasterH, 12.0, mc.accentR, mc.accentG, mc.accentB, 0.08);

    paint_icon_tile(cr, cx + kPad, L.masterY + 12.0, device_glyph(m->raw_name), true, mc.accentR,
                    mc.accentG, mc.accentB, mc.textR, mc.textG, mc.textB);

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13.5);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.92);
    cairo_move_to(cr, cx + kPad + kTileSz + 10.0, L.masterY + 28.0);
    cairo_show_text(cr, ellipsize(cr, m->name, cw - kPad * 2.0 - kTileSz - 10.0 - kMuteBtnSz - 12.0).c_str());

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.0);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.55);
    char sub[64];
    std::snprintf(sub, sizeof(sub), "Default · %d%%", m->volume_pct);
    cairo_move_to(cr, cx + kPad + kTileSz + 10.0, L.masterY + 44.0);
    cairo_show_text(cr, sub);

    const bool muteHov = hovering(L.masterMuteX, L.masterMuteY, L.masterMuteX + kMuteBtnSz,
                                 L.masterMuteY + kMuteBtnSz);
    if (muteHov)
      paint_hover(cr, L.masterMuteX, L.masterMuteY, kMuteBtnSz, kMuteBtnSz, kMuteBtnSz * 0.5,
                  mc.accentR, mc.accentG, mc.accentB);
    eh::shell::draw_material_glyph(cr, L.masterMuteX + kMuteBtnSz * 0.5, L.masterMuteY + kMuteBtnSz * 0.5,
                                   kMuteIconSz, volume_glyph(m->volume_pct, m->muted), mc.textR,
                                   mc.textG, mc.textB, (muteHov || m->muted) ? 0.95 : 0.78);

    if (L.hasMasterMenu) {
      const bool menuHov = hovering(L.masterMenuX, L.masterMenuY, L.masterMenuX + kMenuBtnSz,
                                   L.masterMenuY + kMenuBtnSz);
      const bool menuOpen = u.masterPicker;
      if (menuHov || menuOpen)
        paint_hover(cr, L.masterMenuX, L.masterMenuY, kMenuBtnSz, kMenuBtnSz, kMenuBtnSz * 0.5,
                    mc.accentR, mc.accentG, mc.accentB);
      eh::shell::shared::rounded_rect(cr, L.masterMenuX, L.masterMenuY, kMenuBtnSz, kMenuBtnSz,
                                      kMenuBtnSz * 0.5);
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, menuOpen ? 0.16 : 0.07);
      cairo_fill(cr);
      eh::shell::draw_material_glyph(cr, L.masterMenuX + kMenuBtnSz * 0.5,
                                     L.masterMenuY + kMenuBtnSz * 0.5, 18.0, "expand_more",
                                     menuOpen ? mc.accentR : mc.textR, menuOpen ? mc.accentG : mc.textG,
                                     menuOpen ? mc.accentB : mc.textB, 0.85);
    }

    double masterT = std::clamp(static_cast<double>(m->volume_pct) / 150.0, 0.0, 1.0);
    if (u.dragActive && u.dragKind == MixerUiState::DragKind::Device && u.dragId == m->node_id)
      masterT = u.dragT;
    const bool sHov = hovering(L.masterSliderX - kHitPadH, L.masterSliderY - kHitPadV,
                              L.masterSliderX + L.masterSliderW + kHitPadH,
                              L.masterSliderY + kSliderTrackH + kHitPadV);
    paint_slider(cr, L.masterSliderX, L.masterSliderY, L.masterSliderW, masterT, sHov || u.dragActive,
                 mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, mc.accentR, mc.accentG, mc.accentB,
                 mc.dockFillR + 0.35, mc.dockFillG + 0.35, mc.dockFillB + 0.35);

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12.0);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.80);
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%d%%",
                  (u.dragActive && u.dragKind == MixerUiState::DragKind::Device && u.dragId == m->node_id)
                      ? u.dragPct
                      : m->volume_pct);
    cairo_move_to(cr, L.masterSliderX + L.masterSliderW + 8.0, L.masterSliderY + 11.0);
    cairo_show_text(cr, pct);
  }
  const auto pf5 = perf_clock::now();

  // ---- secondary devices
  for (int i = 0; i < devExtra; ++i) {
    const auto& dev = devices[static_cast<size_t>(i + 1)];
    const auto& g = L.devRows[static_cast<size_t>(i)];
    const bool rowHov = hovering(cx, g.y, cx + cw, g.y + kRowH);
    if (rowHov) paint_hover(cr, cx + 4.0, g.y + 3.0, cw - 8.0, kRowH - 6.0, 8.0, mc.accentR, mc.accentG, mc.accentB);

    cairo_new_path(cr);
    cairo_arc(cr, g.radioCx, g.y + kRowH * 0.5, kRadioR, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.45);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    paint_icon_tile(cr, cx + kPad + kRadioR * 2.0 + 8.0, g.y + (kRowH - kTileSz) * 0.5,
                    device_glyph(dev.raw_name), false, mc.accentR, mc.accentG, mc.accentB, mc.textR,
                    mc.textG, mc.textB);

    const double nameX = cx + kPad + kRadioR * 2.0 + 8.0 + kTileSz + 8.0;
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.68);
    cairo_move_to(cr, nameX, g.y + kRowH * 0.5 + 5.0);
    cairo_show_text(cr, ellipsize(cr, dev.name, kNameMaxW).c_str());

    const bool muteHov = hovering(g.muteX, g.y + (kRowH - kMuteBtnSz) * 0.5, g.muteX + kMuteBtnSz,
                                 g.y + (kRowH + kMuteBtnSz) * 0.5);
    if (muteHov)
      paint_hover(cr, g.muteX, g.y + (kRowH - kMuteBtnSz) * 0.5, kMuteBtnSz, kMuteBtnSz,
                  kMuteBtnSz * 0.5, mc.accentR, mc.accentG, mc.accentB);
    eh::shell::draw_material_glyph(cr, g.muteX + kMuteBtnSz * 0.5, g.y + kRowH * 0.5, kMuteIconSz,
                                   volume_glyph(dev.volume_pct, dev.muted), mc.textR, mc.textG,
                                   mc.textB, (muteHov || dev.muted) ? 0.95 : 0.78);
    double t = std::clamp(static_cast<double>(dev.volume_pct) / 150.0, 0.0, 1.0);
    if (u.dragActive && u.dragKind == MixerUiState::DragKind::Device && u.dragId == dev.node_id)
      t = u.dragT;
    const bool sHov = hovering(g.slider.sx - kHitPadH, g.slider.sy - kHitPadV,
                              g.slider.sx + g.slider.sw + kHitPadH, g.slider.sy + kSliderTrackH + kHitPadV);
    paint_slider(cr, g.slider.sx, g.slider.sy, g.slider.sw, t, sHov, mc.drawerDimR, mc.drawerDimG,
                 mc.drawerDimB, mc.accentR, mc.accentG, mc.accentB, mc.dockFillR + 0.35,
                 mc.dockFillG + 0.35, mc.dockFillB + 0.35);
  }
  if (devices.empty()) {
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.5);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.55);
    cairo_move_to(cr, cx + kPad, L.headerBottom + 34.0);
    cairo_show_text(cr, u.viewOutput ? "No output devices found." : "No input devices found.");
  }
  const auto pf6 = perf_clock::now();

  // ---- apps divider + header
  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.18);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, cx, L.appsDividerY);
  cairo_line_to(cr, cx + cw, L.appsDividerY);
  cairo_stroke(cr);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 10.5);
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.50);
  char appsLabel[48];
  std::snprintf(appsLabel, sizeof(appsLabel), "%s · %d",
                u.viewOutput ? "APPS PLAYING AUDIO" : "APPS RECORDING", streamCount);
  cairo_move_to(cr, cx + 4.0, L.appsDividerY + 20.0);
  cairo_show_text(cr, appsLabel);

  // ---- app rows (clipped to the scroll viewport; culled when outside)
  cairo_save(cr);
  cairo_rectangle(cr, cx, L.viewportY, cw, L.viewportH);
  cairo_clip(cr);
  for (int i = 0; i < streamCount; ++i) {
    const auto& st = streams[static_cast<size_t>(i)];
    const auto& g = L.streamRows[static_cast<size_t>(i)];
    if (g.y + kRowH <= L.viewportY || g.y >= L.viewportY + L.viewportH) continue;
    const bool rowHov = hovering(cx, g.y, cx + cw, g.y + kRowH);
    if (rowHov) paint_hover(cr, cx + 2.0, g.y + 3.0, cw - 4.0, kRowH - 6.0, 8.0, mc.accentR, mc.accentG, mc.accentB);

    paint_stream_app_icon(cr, v.icons, st, cx + kPad + kAppIconSz * 0.5, g.y + kRowH * 0.5 - 2.0,
                          kAppIconSz);

    const double nameX = cx + kPad + kAppIconSz + 8.0;
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.5);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.87);
    cairo_move_to(cr, nameX, g.y + kRowH * 0.5 - 2.0);
    cairo_show_text(cr, ellipsize(cr, st.app_name, kNameMaxW).c_str());

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.5);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.45);
    cairo_move_to(cr, nameX, g.y + kRowH * 0.5 + 13.0);
    const std::string sub = st.routed_display.empty() ? "Default output" : st.routed_display;
    cairo_show_text(cr, ellipsize(cr, sub, kNameMaxW).c_str());

    const bool smuteHov =
        hovering(g.muteX, g.y + (kRowH - kMuteBtnSz) * 0.5, g.muteX + kMuteBtnSz, g.y + (kRowH + kMuteBtnSz) * 0.5);
    if (smuteHov)
      paint_hover(cr, g.muteX, g.y + (kRowH - kMuteBtnSz) * 0.5, kMuteBtnSz, kMuteBtnSz,
                  kMuteBtnSz * 0.5, mc.accentR, mc.accentG, mc.accentB);
    eh::shell::draw_material_glyph(cr, g.muteX + kMuteBtnSz * 0.5, g.y + kRowH * 0.5 - 2.0,
                                   kMuteIconSz, volume_glyph(st.volume_pct, st.muted), mc.textR,
                                   mc.textG, mc.textB, (smuteHov || st.muted) ? 0.95 : 0.78);

    double t = std::clamp(static_cast<double>(st.volume_pct) / 150.0, 0.0, 1.0);
    if (u.dragActive && u.dragKind == MixerUiState::DragKind::Stream && u.dragId == st.node_id)
      t = u.dragT;
    const bool sHov = hovering(g.slider.sx - kHitPadH, g.slider.sy - kHitPadV,
                              g.slider.sx + g.slider.sw + kHitPadH, g.slider.sy + kSliderTrackH + kHitPadV);
    paint_slider(cr, g.slider.sx, g.slider.sy, g.slider.sw, t, sHov, mc.drawerDimR, mc.drawerDimG,
                 mc.drawerDimB, mc.accentR, mc.accentG, mc.accentB, mc.dockFillR + 0.35,
                 mc.dockFillG + 0.35, mc.dockFillB + 0.35);

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.5);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.70);
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%d%%",
                  (u.dragActive && u.dragKind == MixerUiState::DragKind::Stream && u.dragId == st.node_id)
                      ? u.dragPct
                      : st.volume_pct);
    cairo_move_to(cr, g.pctX, g.y + kRowH * 0.5 + 2.0);
    cairo_show_text(cr, pct);

    const bool menuOpen = (u.routerStream == st.node_id);
    const bool menuHov = hovering(g.menuX, g.y + (kRowH - kMenuBtnSz) * 0.5, g.menuX + kMenuBtnSz,
                                 g.y + (kRowH + kMenuBtnSz) * 0.5);
    if (menuHov || menuOpen)
      paint_hover(cr, g.menuX, g.y + (kRowH - kMenuBtnSz) * 0.5, kMenuBtnSz, kMenuBtnSz,
                  kMenuBtnSz * 0.5, mc.accentR, mc.accentG, mc.accentB);
    eh::shell::shared::rounded_rect(cr, g.menuX, g.y + (kRowH - kMenuBtnSz) * 0.5, kMenuBtnSz,
                                    kMenuBtnSz, kMenuBtnSz * 0.5);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, menuOpen ? 0.16 : 0.07);
    cairo_fill(cr);
    paint_swap_glyph(cr, g.menuX + kMenuBtnSz * 0.5, g.y + kRowH * 0.5 - 2.0, 17.0,
                     menuOpen ? mc.accentR : mc.textR, menuOpen ? mc.accentG : mc.textG,
                     menuOpen ? mc.accentB : mc.textB, 0.85);
  }
  if (streamCount == 0) {
    eh::shell::draw_material_glyph(cr, cx + cw * 0.5, L.viewportY + L.viewportH * 0.5 - 12.0, 22.0,
                                   "music_note", mc.textR, mc.textG, mc.textB, 0.30);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.45);
    const char* msg = u.viewOutput ? "Nothing is playing right now." : "Nothing is recording right now.";
    cairo_text_extents_t te{};
    cairo_text_extents(cr, msg, &te);
    cairo_move_to(cr, cx + (cw - te.x_advance) * 0.5, L.viewportY + L.viewportH * 0.5 + 14.0);
    cairo_show_text(cr, msg);
  }
  cairo_restore(cr);

  // Scrollbar (only when the list overflows).
  if (L.scrollable) {
    const double trackX = L.scrollX + (L.scrollW - 6.0) * 0.5;
    eh::shell::shared::rounded_rect(cr, trackX, L.viewportY + 4.0, 6.0, L.viewportH - 8.0, 3.0);
    cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.35);
    cairo_fill(cr);
    const bool thumbHov =
        hovering(L.scrollX, L.thumbY, L.scrollX + L.scrollW, L.thumbY + L.thumbH);
    const bool thumbDrag = u.dragActive && u.dragKind == MixerUiState::DragKind::Scroll;
    eh::shell::shared::rounded_rect(cr, trackX - 1.0, L.thumbY, 8.0, L.thumbH, 4.0);
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, (thumbHov || thumbDrag) ? 0.85 : 0.55);
    cairo_fill(cr);
  }
  const auto pf7 = perf_clock::now();

  // ---- engine footer (live sound-engine status)
  {
    cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.14);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, cx, L.footerY);
    cairo_line_to(cr, cx + cw, L.footerY);
    cairo_stroke(cr);

    const auto& es = cached_engine();
    const auto& top = cached_top();
    char eng[160];
    if (es.available)
      std::snprintf(eng, sizeof(eng), "%d Hz · q%d", es.clock_rate_hz,
                    es.clock_force_quantum != 0 ? es.clock_force_quantum : es.clock_quantum);
    else
      std::snprintf(eng, sizeof(eng), "engine offline");
    char dsp[160];
    if (top.available)
      std::snprintf(dsp, sizeof(dsp), "DSP %.0f%% · %ld xruns", top.dsp_load_pct, top.xruns_total);
    else
      std::snprintf(dsp, sizeof(dsp), "no load data");
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.5);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.50);
    cairo_move_to(cr, cx + 4.0, L.footerY + 17.0);
    cairo_show_text(cr, eng);
    cairo_move_to(cr, cx + 4.0, L.footerY + 31.0);
    cairo_show_text(cr, dsp);

    const bool setHov =
        hovering(L.settingsX, L.footerY + (kFooterH - 28.0) * 0.5, L.settingsX + L.settingsW, L.footerY + (kFooterH + 28.0) * 0.5);
    if (setHov) paint_hover(cr, L.settingsX, L.footerY + (kFooterH - 28.0) * 0.5, L.settingsW, 28.0, 8.0, mc.accentR, mc.accentG, mc.accentB);
    eh::shell::shared::rounded_rect(cr, L.settingsX, L.footerY + (kFooterH - 28.0) * 0.5, L.settingsW,
                                    28.0, 8.0);
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, setHov ? 0.24 : 0.16);
    cairo_fill(cr);
    eh::shell::draw_material_glyph(cr, L.settingsX + 16.0, L.footerY + kFooterH * 0.5, 14.0,
                                   "settings", mc.accentR, mc.accentG, mc.accentB, 0.90);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.5);
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.92);
    cairo_move_to(cr, L.settingsX + 32.0, L.footerY + kFooterH * 0.5 + 4.5);
    cairo_show_text(cr, "Sound settings");
  }
  const auto pf8 = perf_clock::now();

  // ---- floating device dropdown (overlay, painted last so it floats above
  // rows and footer). Master mode picks the default output/input device;
  // stream mode routes one app. Opening it never shifts layout.
  if (L.hasDrop && (L.dropIsMaster || (L.dropStreamIdx >= 0 && L.dropStreamIdx < streamCount))) {
    const std::string routed =
        (!L.dropIsMaster && L.dropStreamIdx >= 0) ? streams[static_cast<size_t>(L.dropStreamIdx)].routed_raw
                                                 : std::string{};
    const auto& targets = u.viewOutput ? snap.sinks : snap.sources;
    cairo_save(cr);
    eh::shell::shared::rounded_rect(cr, L.dropX + 2.0, L.dropY + 3.0, L.dropW, L.dropH, 10.0);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_fill(cr);
    cairo_restore(cr);
    eh::shell::shared::rounded_rect(cr, L.dropX, L.dropY, L.dropW, L.dropH, 10.0);
    cairo_set_source_rgba(cr, mc.panelFillR, mc.panelFillG, mc.panelFillB, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.35);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10.5);
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.62);
    cairo_move_to(cr, L.dropX + 12.0, L.dropY + 18.0);
    cairo_show_text(cr, L.dropIsMaster ? (u.viewOutput ? "OUTPUT DEVICE" : "INPUT DEVICE")
                                       : (u.viewOutput ? "PLAY ON" : "RECORD FROM"));

    for (int si = 0; si < L.dropCount; ++si) {
      const auto& tgt = targets[static_cast<size_t>(si)];
      const double itemY = L.dropY + kDropTitleH + static_cast<double>(si) * kDropItemH;
      if (itemY + kDropItemH > L.dropY + L.dropH) break;  // clamped: never paint past the box
      const bool isChecked =
          (!routed.empty() && routed == tgt.raw_name) || (routed.empty() && tgt.is_default);
      if (isChecked) {
        eh::shell::shared::rounded_rect(cr, L.dropX + 4.0, itemY + 2.0, L.dropW - 8.0,
                                        kDropItemH - 4.0, 6.0);
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.14);
        cairo_fill(cr);
      }
      const bool itemHov = hovering(L.dropX + 4.0, itemY, L.dropX + L.dropW - 4.0, itemY + kDropItemH);
      if (itemHov) paint_hover(cr, L.dropX + 4.0, itemY + 2.0, L.dropW - 8.0, kDropItemH - 4.0, 6.0, mc.accentR, mc.accentG, mc.accentB);

      cairo_new_path(cr);
      cairo_arc(cr, L.dropX + 24.0, itemY + kDropItemH * 0.5, 8.0, 0, 2 * M_PI);
      cairo_set_source_rgba(cr, isChecked ? mc.accentR : mc.outlineR, isChecked ? mc.accentG : mc.outlineG,
                            isChecked ? mc.accentB : mc.outlineB, isChecked ? 0.95 : 0.62);
      cairo_set_line_width(cr, isChecked ? 2.0 : 1.5);
      cairo_stroke(cr);
      if (isChecked) {
        cairo_new_path(cr);
        cairo_arc(cr, L.dropX + 24.0, itemY + kDropItemH * 0.5, 4.5, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.95);
        cairo_fill(cr);
      }
      eh::shell::draw_material_glyph(cr, L.dropX + 42.0, itemY + kDropItemH * 0.5, 16.0,
                                     device_glyph(tgt.raw_name), isChecked ? mc.accentR : mc.textR,
                                     isChecked ? mc.accentG : mc.textG, isChecked ? mc.accentB : mc.textB,
                                     isChecked ? 0.87 : 0.60);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL,
                             isChecked ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12.0);
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, isChecked ? 0.87 : 0.60);
      cairo_move_to(cr, L.dropX + 58.0, itemY + kDropItemH * 0.5 + 4.5);
      cairo_show_text(cr, ellipsize(cr, shorten_device_name(tgt.name), L.dropW - 58.0 - 30.0).c_str());
      if (tgt.is_default)
        eh::shell::draw_material_glyph(cr, L.dropX + L.dropW - 20.0, itemY + kDropItemH * 0.5, 12.0,
                                       "star", mc.textR, mc.textG, mc.textB, 0.50);
    }

    // Card profile subsection (master dropdown only): codec/preset switcher
    // for the default device's card, e.g. Bluetooth A2DP codecs.
    if (L.dropIsMaster && L.dropProfileCount > 0) {
      if (const DeviceInfo* mdev = master_device(snap, u.viewOutput)) {
        if (const auto* card = find_card_for_node(mdev->raw_name)) {
          cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
          cairo_set_font_size(cr, 10.5);
          cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.62);
          cairo_move_to(cr, L.dropX + 12.0, L.dropProfileTop - 6.0);
          cairo_show_text(cr, "PROFILE");
          for (int pi = 0; pi < L.dropProfileCount; ++pi) {
            if (static_cast<size_t>(pi) >= card->profiles.size()) break;
            const auto& opt = card->profiles[static_cast<size_t>(pi)];
            const double itemY = L.dropProfileTop + static_cast<double>(pi) * kDropItemH;
            if (itemY + kDropItemH > L.dropY + L.dropH) break;
            const bool isActive = opt.key == card->active_profile_key;
            if (isActive) {
              eh::shell::shared::rounded_rect(cr, L.dropX + 4.0, itemY + 2.0, L.dropW - 8.0,
                                              kDropItemH - 4.0, 6.0);
              cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.14);
              cairo_fill(cr);
            }
            const bool itemHov =
                hovering(L.dropX + 4.0, itemY, L.dropX + L.dropW - 4.0, itemY + kDropItemH);
            if (itemHov)
              paint_hover(cr, L.dropX + 4.0, itemY + 2.0, L.dropW - 8.0, kDropItemH - 4.0, 6.0,
                          mc.accentR, mc.accentG, mc.accentB);
            cairo_new_path(cr);
            cairo_arc(cr, L.dropX + 24.0, itemY + kDropItemH * 0.5, 8.0, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, isActive ? mc.accentR : mc.outlineR,
                                  isActive ? mc.accentG : mc.outlineG,
                                  isActive ? mc.accentB : mc.outlineB, isActive ? 0.95 : 0.62);
            cairo_set_line_width(cr, isActive ? 2.0 : 1.5);
            cairo_stroke(cr);
            if (isActive) {
              cairo_new_path(cr);
              cairo_arc(cr, L.dropX + 24.0, itemY + kDropItemH * 0.5, 4.5, 0, 2 * M_PI);
              cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.95);
              cairo_fill(cr);
            }
            eh::shell::draw_material_glyph(cr, L.dropX + 42.0, itemY + kDropItemH * 0.5, 16.0,
                                           "tune", isActive ? mc.accentR : mc.textR,
                                           isActive ? mc.accentG : mc.textG,
                                           isActive ? mc.accentB : mc.textB, isActive ? 0.87 : 0.60);
            cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL,
                                   isActive ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 12.0);
            cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, isActive ? 0.87 : 0.60);
            cairo_move_to(cr, L.dropX + 58.0, itemY + kDropItemH * 0.5 + 4.5);
            const std::string label = opt.description.empty() ? opt.key : opt.description;
            cairo_show_text(cr, ellipsize(cr, shorten_profile_label(opt.key, label), L.dropW - 58.0 - 12.0).c_str());
          }
        }
      }
    }
  }
  const auto pf9 = perf_clock::now();
  if (MixerLog::instance().enabled()) {
    const double total = mixer_us_between(pf0, pf9) / 1000.0;
    MixerLog::instance().report_paint(
        total, t_mixer_last_snap_us / 1000.0, t_mixer_last_snap_hit, t_mixer_last_link_us / 1000.0,
        mixer_us_between(pf1, pf2) / 1000.0, mixer_us_between(pf2, pf3) / 1000.0,
        mixer_us_between(pf3, pf4) / 1000.0, mixer_us_between(pf4, pf5) / 1000.0,
        mixer_us_between(pf5, pf6) / 1000.0, mixer_us_between(pf6, pf7) / 1000.0,
        mixer_us_between(pf7, pf8) / 1000.0, mixer_us_between(pf8, pf9) / 1000.0,
        static_cast<int>(devices.size()), streamCount);
  }
}

}  // namespace detail

void dock_volume_mixer_popup_handle_click(DockApp& app, double x, double y, uint32_t serial) {
  const double W = static_cast<double>(kVolumeMixerPopupW);
  const double H = static_cast<double>(app.popupH);
  if (x < 0 || x >= W || y < 0 || y >= H) {
    popup_close(app);
    return;
  }
  const bool heightChanged = detail::mixer_click_at(x, y, H);
  if (heightChanged) mixer_resize_popup(app, serial);
}

void dock_volume_mixer_popup_handle_motion(DockApp& app) {
  if (app.pointerSurface != app.popupSurface) return;
  detail::mixer_motion_at(app.pointerX, app.pointerY);
}

void dock_volume_mixer_popup_handle_release(DockApp& app) {
  detail::mixer_release_at(app.pointerX);
}

bool dock_volume_mixer_popup_handle_axis(DockApp& app, double deltaPx) {
  if (!app.popupOpen) return false;
  if (app.pointerSurface != app.popupSurface) return false;
  return detail::mixer_axis_at(app.pointerX, app.pointerY, deltaPx);
}

}  // namespace eh::shell::dock::popup::volume_mixer
