#pragma once

// Shared value types for the top-edge dashboard.
//
// The dashboard follows the repo-wide hit-testing rule (Docs/hit-testing.md):
// layout geometry is computed once during paint, stored in DashboardState::
// layout, and every input handler resolves through those stored rects. Input
// code never recomputes geometry on its own.

#include "configuration/shell_config.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/buffer/shm_buffer.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct DockApp;

namespace eh::shell::dashboard {

enum class DashCardKind : std::uint8_t {
  None = 0,
  Clock,
  Calendar,
  Weather,
  Media,
  Volume,
  Mic,
  Network,
  Bluetooth,
  Mixer,
  System,
};

// Interactive affordances inside a card. Sub-rects are pushed in paint order
// and hit-tested in that same order, so an earlier sub wins an overlap (the
// audio Mute button overlaps the left edge of the slider hit box).
enum class DashCardRole : std::uint8_t {
  Body = 0,
  Toggle,        // network / bluetooth header (expand-collapse)
  Row,           // expanded network AP row or bluetooth device row
  Mute,          // audio card icon button
  Slider,        // volume / mic / mixer track
  MediaPrev,
  MediaPlayPause,
  MediaNext,
  CalPrev,
  CalNext,
  CalToday,  // jump back to the current month
};

// One interactive (or purely informational) rect inside a card. `paint*` is
// exactly what paint drew; `hit*` is the accepted input box — equal unless the
// affordance is padded for touch-friendliness.
struct SubRect {
  DashCardRole role = DashCardRole::Body;
  int row = -1;
  int streamId = -1;
  double paintX = 0.0, paintY = 0.0, paintW = 0.0, paintH = 0.0;
  double hitX = 0.0, hitY = 0.0, hitW = 0.0, hitH = 0.0;
  double trackX = 0.0, trackW = 0.0;  // slider drag geometry (surface coords)
};

struct CardRect {
  std::string id{};
  DashCardKind kind = DashCardKind::None;
  int span = 1;
  double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
  std::vector<SubRect> subs{};
};

// Written by every paint, read by every input handler.
struct DashboardLayout {
  double surfaceW = 0.0;
  double surfaceH = 0.0;
  double panelX = 0.0, panelY = 0.0, panelW = 0.0, panelH = 0.0;
  std::vector<CardRect> cards{};
  bool valid = false;
};

struct DashboardHit {
  bool valid = false;
  int card = -1;
  int sub = -1;
  DashCardKind kind = DashCardKind::None;
  DashCardRole role = DashCardRole::Body;
  int row = -1;
  int streamId = -1;
  double trackX = 0.0, trackW = 0.0;
};

enum class DashDragKind : std::uint8_t { None = 0, Volume, InputVolume, Mixer, Seek };

struct DashboardState {
  // --- trigger strip surface (permanent while enabled) ---
  // Static strip region set at creation, never updated. Enter toggles:
  // closed -> open (reveal), open -> close (dismiss).
  wl_surface* trigSurface = nullptr;
  zwlr_layer_surface_v1* trigLayer = nullptr;
  eh::wayland::ShmBuffer trigShm{};
  int trigW = 0;

  // --- panel surface (created on open, destroyed on close) ---
  // Static full-surface region set at creation, never updated: region-only
  // updates are not reliably applied by the compositor, so the region must
  // be right from birth (and death removes all input at once).
  wl_surface* panelSurface = nullptr;
  zwlr_layer_surface_v1* panelLayer = nullptr;
  eh::wayland::ShmBuffer panelShm{};
  int panelW = 0, panelH = 0;
  bool panelConfigured = false;
  int panelLastRequestedH = 0;  // set_size loop guard (one configure per resize)
  int panelEnsureFailures = 0;

  // --- reveal ---
  bool open = false;
  float revealT = 0.f;  // 0 = hidden above the screen edge, 1 = fully shown
  std::uint32_t animId = 0;

  // --- drawing ---
  bool dirty = false;
  bool needsDraw = false;

  DashboardLayout layout{};

  // --- input ---
  DashboardHit hover{};
  DashboardHit press{};
  bool dragging = false;
  bool pendingLeave = false;  // pointer left mid-drag; cleared on release (stays open)
  bool kbdFocus = false;

  // --- card-local UI state ---
  bool netExpanded = false;
  bool btExpanded = false;
  std::string wifiError{};
  std::uint64_t wifiErrorUntilMs = 0;
  int calYear = 0, calMonth = 0;  // 0 = seed from the current date

  // --- slider drag ---
  DashDragKind dragKind = DashDragKind::None;
  int dragStreamId = -1;
  double dragTrackX = 0.0, dragTrackW = 1.0;
  double dragVisualT = -1.0;
  std::uint64_t dragLastApplyMs = 0;
  int dragLastPct = -1;
  std::int64_t dragSeekDurationUs = 0;  // captured at press for media seek

  // --- timer signatures ---
  std::string clockSig{}, weatherSig{}, mediaSig{};
  std::string audioSig{}, netSig{}, btSig{}, mixerSig{}, sysSig{};

  // --- system monitor cache (own /proc sampling) ---
  std::uint64_t sysSampleMs = 0;
  double sysCpuPct = 0.0;
  double sysMemMb = 0.0;
  double sysTempC = 0.0;
  double sysMemTotalMb = 0.0;
  unsigned long long sysPrevIdle = 0, sysPrevTotal = 0;
  bool sysPrimed = false;

  // --- cached config ---
  eh::config::DashboardConfig cfg{};
  bool cfgValid = false;
};

}  // namespace eh::shell::dashboard
