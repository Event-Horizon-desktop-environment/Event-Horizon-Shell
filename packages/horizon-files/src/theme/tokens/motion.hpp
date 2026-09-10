#pragma once

#include <cstdint>

namespace m3 {

// M3 easing curves as cubic-bezier parameters.
struct Easing {
  float x1, y1, x2, y2;
};

inline constexpr Easing kEmphasized            = { 0.2f, 0.0f, 0.0f, 1.0f };
inline constexpr Easing kEmphasizedDecelerate  = { 0.05f, 0.7f, 0.1f, 1.0f };
inline constexpr Easing kEmphasizedAccelerate  = { 0.3f, 0.0f, 0.8f, 0.15f };
inline constexpr Easing kStandard              = { 0.2f, 0.0f, 0.0f, 1.0f };
inline constexpr Easing kStandardDecelerate    = { 0.0f, 0.0f, 0.0f, 1.0f };
inline constexpr Easing kStandardAccelerate    = { 0.3f, 0.0f, 1.0f, 1.0f };
inline constexpr Easing kLinear                = { 0.0f, 0.0f, 1.0f, 1.0f };

// M3 duration classes (milliseconds).
inline constexpr uint64_t kDurationEmphasis    = 500;
inline constexpr uint64_t kDurationLarge       = 400;
inline constexpr uint64_t kDurationMedium      = 300;
inline constexpr uint64_t kDurationSmall       = 200;
inline constexpr uint64_t kDurationExtraSmall  = 100;

// Spring physics (M3 Expressive 2025).
struct SpringParams {
  float stiffness = 400.0f;  // Higher = snappier
  float damping   = 30.0f;   // Lower = more bounce
  float mass      = 1.0f;
};

// Spring presets:
inline constexpr SpringParams kSpringResponsive  = { 500.0f, 35.0f, 1.0f };
inline constexpr SpringParams kSpringNatural     = { 300.0f, 25.0f, 1.0f };
inline constexpr SpringParams kSpringExpressive  = { 200.0f, 15.0f, 1.0f };

// Per-component motion:
//
//   Interaction              | Duration | Easing                 | Spring?
//   -------------------------|----------|------------------------|----------
//   State layer hover        | 100ms    | standard-decelerate    | no
//   State layer press        | 100ms    | standard-decelerate    | no
//   Focus ring               | 100ms    | standard               | no
//   Button ripple            | 300ms    | emphasized             | optional
//   Switch thumb             | 200ms    | emphasized             | responsive
//   Switch track             | 200ms    | emphasized             | responsive
//   Slider thumb             | 100ms    | emphasized             | no
//   Slider value popup       | 100ms    | emphasized-decelerate  | no
//   Checkbox state           | 200ms    | emphasized             | no
//   Radio button state       | 200ms    | emphasized             | no
//   Card hover               | 150ms    | standard               | no
//   Chip enter/exit          | 200ms    | emphasized             | no
//   Dialog open              | 300ms    | emphasized-decelerate  | expressive
//   Dialog close             | 200ms    | emphasized-accelerate  | no
//   Sheet open               | 400ms    | emphasized-decelerate  | natural
//   Sheet close              | 300ms    | emphasized-accelerate  | no
//   Menu open                | 200ms    | emphasized-decelerate  | no
//   Menu close               | 150ms    | emphasized-accelerate  | no
//   FAB expand               | 300ms    | emphasized-decelerate  | expressive
//   Page transition          | 500ms    | emphasized             | natural
//   Palette crossfade        | 400ms    | emphasized             | no

} // namespace m3
