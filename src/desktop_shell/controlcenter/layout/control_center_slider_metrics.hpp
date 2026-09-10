#pragma once

namespace eh::shell::cc_slider {

constexpr double kAudioTrackXPad = 18.0;
constexpr double kAudioTrackH = 8.0;
constexpr double kAudioCardH = 92.0;
constexpr double kAudioTitleBaseline = 30.0;
constexpr double kAudioTrackYFromCardTop =
    kAudioTitleBaseline + (kAudioCardH - kAudioTitleBaseline - kAudioTrackH) / 2.0;
constexpr double kAudioHitPadV = 15.0;
constexpr double kAudioHitPadH = 6.0;
constexpr double kAudioThumbR = 9.0;

constexpr double kMixerTrackH = 8.0;
constexpr double kMixerHitPadV = 15.0;
constexpr double kMixerHitPadH = 4.0;

constexpr double kMixerHeaderH = 36.0;
constexpr double kMixerSectionH = 28.0;
constexpr double kMixerRowH = 46.0;
constexpr double kMixerMoreH = 26.0;
constexpr double kMixerIconLeft = 32.0;   // kPillPad + 14
constexpr double kMixerNameLeft = 58.0;   // kPillPad + 40
constexpr double kMixerIconY = 15.0;
constexpr double kMixerNameBaseline = 27.0;
constexpr double kMixerSliderCY = 23.0;

// Legacy — kept for taskbar compatibility
constexpr double kMixerRowTrackYOffset = 18.0;
constexpr double kMixerExpTrackLeftPad = 34.0;
constexpr double kMixerExpTrackYFromRow = 18.0;
constexpr double kMixerExpTrackRightReserve = 46.0;

}
