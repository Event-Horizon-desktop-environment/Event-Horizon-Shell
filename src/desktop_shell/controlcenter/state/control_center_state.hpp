#pragma once

#include <cstdint>
#include <string>

namespace eh::shell::dock::control_center {

enum class CcModal : uint8_t {
  None = 0,
  Network,
  Bluetooth,
  AudioOutput,
  AudioInput,
  Mixer,
};

// Backward compatibility alias
using ControlCenterActiveModal = CcModal;

enum class CcHoverTarget : uint8_t {
  None = 0,
  NetworkGrid,
  NetworkCard,
  NetworkRow,
  BluetoothGrid,
  BluetoothCard,
  BluetoothRow,
  AudioOutputGrid,
  AudioInputGrid,
  OutputAudioSlider,
  OutputAudioMute,
  InputAudioSlider,
  InputAudioMute,
  OutputDeviceRow,
  InputDeviceRow,
  MixerCard,
  MixerSlider,
  MediaPrev,
  MediaPlayPause,
  MediaNext,
  MediaCard,
  WeatherCard,
  // PearCenter compact layout targets.
  SettingsRow,      // SectionButtons LongButton[2] -> open System Settings
  DndCard,          // DndButton -> toggle Do Not Disturb
  DeviceLinkCard,   // Device-link quick toggle -> run configured device command
  NightColorCard,   // Night Color quick toggle -> toggle night light
  ColorSchemeCard,  // Theme quick toggle (icon only) -> swap light/dark scheme
  CameraCard,       // Camera quick toggle -> screenshot region selection
  CmdCard1,         // CommandRun 1 -> spawn custom shell command
  CmdCard2,         // CommandRun 2 -> spawn custom shell command
  VolumeCard,       // Volume slider card (mute on icon, drag value)
  InputCard,        // Input slider card (mute on icon, drag value)
  BrightnessCard,   // Brightness slider card
  NetworksBack,     // SectionNetworks header back arrow
  WifiToggle,       // SectionNetworks Wi-Fi enable checkbox
};

struct ControlCenterState {
  ControlCenterState() = default;

  // Active modal overlay
  CcModal activeModal = CcModal::None;

  // Audio output drag state
  bool audioDragActive = false;
  double audioDragVisualT = -1.0;
  int audioLastLoggedPct = -1;
  int audioDragUiPct = -1;
  int audioLastAppliedPct = -1;
  uint64_t audioLastApplyMs = 0;
  uint64_t audioIgnoreStateUntilMs = 0;

  // Audio input drag state
  bool inputDragActive = false;
  double inputDragVisualT = -1.0;
  int inputLastLoggedPct = -1;
  int inputDragUiPct = -1;
  int inputLastAppliedPct = -1;
  uint64_t inputLastApplyMs = 0;
  uint64_t inputIgnoreStateUntilMs = 0;

  // PearCenter brightness drag state (kept separate so the input-audio row
  // can keep using inputDragActive).
  bool pearBriDragActive = false;
  double pearBriDragT = -1.0;

  // Mixer drag state
  bool mixerDragActive = false;
  double mixerDragVisualT = -1.0;
  int mixerDragStreamId = -1;
  bool mixerDragIsInput = false;
  int mixerLastLoggedPct = -1;
  int mixerDragUiPct = -1;
  int mixerLastAppliedPct = -1;
  uint64_t mixerLastApplyMs = 0;
  uint64_t mixerIgnoreStateUntilMs = 0;
  double mixerDragSx = 0.0;
  double mixerDragSw = 1.0;

  // Output devices
  std::string outputDevicesPendingSink{};
  uint64_t outputDevicesIgnoreUntilMs = 0;

  // Input devices
  std::string inputDevicesPendingSource{};
  uint64_t inputDevicesIgnoreUntilMs = 0;

  // Network / Wi-Fi
  bool wifiPasswordPrompt = false;
  std::string wifiPendingSsid{};
  std::string wifiPassword{};
  std::string wifiLastError{};
  uint64_t wifiIgnoreUntilMs = 0;

  // Bluetooth
  bool btScanning = false;
  std::string btLastError{};
  uint64_t btIgnoreUntilMs = 0;

  // Expansion states
  bool mixerExpanded = false;
  bool networkExpanded = false;
  bool bluetoothExpanded = false;
  bool outputDevicesExpanded = false;
  bool inputDevicesExpanded = false;
  bool weatherExpanded = false;

  // PearCenter compact layout: SectionNetworks full-overlay page (covers wrapper, z=999).
  // When true, paint draws the networks page instead of Section A+B and hit/
  // dispatch route to NetworksBack/WifiToggle/NetworkRow.
  bool networksOverlay = false;

  // Animation states for network
  uint64_t netAnimStartMs = 0;
  bool netAnimFromExpanded = false;
  bool netAnimToExpanded = false;

  // Animation states for bluetooth
  uint64_t btAnimStartMs = 0;
  bool btAnimFromExpanded = false;
  bool btAnimToExpanded = false;

  // Animation states for weather
  uint64_t weatherAnimStartMs = 0;
  bool weatherAnimFromExpanded = false;
  bool weatherAnimToExpanded = false;

  // Hover tracking
  CcHoverTarget hoverTarget = CcHoverTarget::None;
  int hoverRowIdx = -1;
  int hoverStreamId = -1;

  // Last settle-triggered resize (debounces post-animation/streaming growth).
  uint64_t ccLastSettleResizeMs = 0;

  // Benchmarks
  uint64_t openBenchStartMs = 0;
  bool openBenchLoggedFirstConfigure = false;
  bool openBenchLoggedFirstPaint = false;
};
}

namespace eh::shell::controlcenter {
using namespace eh::shell::dock::control_center;
}
