#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <cairo/cairo.h>

#include "m3/controls/input/toggle.hpp"
#include "m3/controls/input/slider.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/box.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_m3_layout.hpp"

extern void draw(App& app);
extern void hyprland_commit_cfg(App& app);

namespace m3::detail {

static const char* const kAccelProfileNames[] = {"adaptive", "flat", "custom"};

static const char* const kShareStatesNames[] = {"disable", "enable", "only non-IME"};

static int accelProfileIndex(const std::string& p) {
  for (int i = 0; i < 3; ++i)
    if (p == kAccelProfileNames[i]) return i;
  return 0;
}

struct HyprlandInputM3State {
  Slider repeatDelaySlider;
  Slider repeatRateSlider;
  Slider sensitivitySlider;
  Slider accelProfileSlider;
  Slider followMouseSlider;
  Slider scrollFactorSlider;
  Slider swipeDistSlider;
  Slider scrollButtonSlider;
  Slider rotationSlider;
  Slider followMouseShrinkSlider;
  Slider touchdeviceTransformSlider;
  Slider vkShareStatesSlider;
  Slider tabletTransformSlider;
  Slider eraserButtonModeSlider;
  Slider eraserButtonOverrideSlider;
  Slider pressureRangeMinSlider;
  Slider pressureRangeMaxSlider;
  Toggle numlockByDefault;
  Toggle naturalScroll;
  Toggle tpNaturalScroll;
  Toggle disableWhileTyping;
  Toggle tapToClick;
  Toggle swipeCreateNew;
  Toggle specialFallthrough;
  Toggle forceNoAccel;
  Toggle leftHanded;
  Toggle scrollButtonLock;
  Toggle mouseRefocus;
  Toggle resolveBindsBySym;
  Toggle tpClickfingerBehavior;
  Toggle tpMiddleButtonEmulation;
  Toggle tpTapAndDrag;
  Toggle tpFlipX;
  Toggle tpFlipY;
  Toggle touchdeviceEnabled;
  Toggle vkReleasePressedOnClose;
  Toggle tabletAbsoluteRegionPosition;
  Toggle tabletRelativeInput;
  Toggle tabletLeftHanded;

  int activeSlider_ = -1;

  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

  void syncColours(const App& app) {
    float a_r = accentR_, a_g = accentG_, a_b = accentB_;
    float t_r = textR_, t_g = textG_, t_b = textB_;
    float s_r = surfaceR_, s_g = surfaceG_, s_b = surfaceB_;
    float o_r = outlineR_, o_g = outlineG_, o_b = outlineB_;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    accentR_ = a_r; accentG_ = a_g; accentB_ = a_b;
    textR_ = t_r; textG_ = t_g; textB_ = t_b;
    surfaceR_ = s_r; surfaceG_ = s_g; surfaceB_ = s_b;
    outlineR_ = o_r; outlineG_ = o_g; outlineB_ = o_b;
  }

  void applyColours() {
    auto apply = [&](auto& w) {
      w.setAccentColor(accentR_, accentG_, accentB_);
      w.setSurfaceColor(surfaceR_, surfaceG_, surfaceB_);
      w.setTextColor(textR_, textG_, textB_);
    };
    apply(repeatDelaySlider); apply(repeatRateSlider); apply(sensitivitySlider);
    apply(accelProfileSlider); apply(followMouseSlider); apply(scrollFactorSlider);
    apply(swipeDistSlider); apply(scrollButtonSlider); apply(rotationSlider);
    apply(followMouseShrinkSlider);
    apply(touchdeviceTransformSlider); apply(vkShareStatesSlider);
    apply(tabletTransformSlider); apply(eraserButtonModeSlider);
    apply(eraserButtonOverrideSlider); apply(pressureRangeMinSlider);
    apply(pressureRangeMaxSlider);
    apply(numlockByDefault); apply(naturalScroll); apply(tpNaturalScroll);
    apply(disableWhileTyping); apply(tapToClick); apply(swipeCreateNew);
    apply(specialFallthrough);
    apply(forceNoAccel); apply(leftHanded); apply(scrollButtonLock);
    apply(mouseRefocus); apply(resolveBindsBySym);
    apply(tpClickfingerBehavior); apply(tpMiddleButtonEmulation);
    apply(tpTapAndDrag); apply(tpFlipX); apply(tpFlipY);
    apply(touchdeviceEnabled); apply(vkReleasePressedOnClose);
    apply(tabletAbsoluteRegionPosition); apply(tabletRelativeInput);
    apply(tabletLeftHanded);
    numlockByDefault.setOutlineColor(outlineR_, outlineG_, outlineB_);
    naturalScroll.setOutlineColor(outlineR_, outlineG_, outlineB_);
    tpNaturalScroll.setOutlineColor(outlineR_, outlineG_, outlineB_);
    disableWhileTyping.setOutlineColor(outlineR_, outlineG_, outlineB_);
    tapToClick.setOutlineColor(outlineR_, outlineG_, outlineB_);
    swipeCreateNew.setOutlineColor(outlineR_, outlineG_, outlineB_);
    specialFallthrough.setOutlineColor(outlineR_, outlineG_, outlineB_);
    forceNoAccel.setOutlineColor(outlineR_, outlineG_, outlineB_);
    leftHanded.setOutlineColor(outlineR_, outlineG_, outlineB_);
    scrollButtonLock.setOutlineColor(outlineR_, outlineG_, outlineB_);
    mouseRefocus.setOutlineColor(outlineR_, outlineG_, outlineB_);
    resolveBindsBySym.setOutlineColor(outlineR_, outlineG_, outlineB_);
    tpClickfingerBehavior.setOutlineColor(outlineR_, outlineG_, outlineB_);
    tpMiddleButtonEmulation.setOutlineColor(outlineR_, outlineG_, outlineB_);
    tpTapAndDrag.setOutlineColor(outlineR_, outlineG_, outlineB_);
    tpFlipX.setOutlineColor(outlineR_, outlineG_, outlineB_);
    tpFlipY.setOutlineColor(outlineR_, outlineG_, outlineB_);
    touchdeviceEnabled.setOutlineColor(outlineR_, outlineG_, outlineB_);
    vkReleasePressedOnClose.setOutlineColor(outlineR_, outlineG_, outlineB_);
    tabletAbsoluteRegionPosition.setOutlineColor(outlineR_, outlineG_, outlineB_);
    tabletRelativeInput.setOutlineColor(outlineR_, outlineG_, outlineB_);
    tabletLeftHanded.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(repeatDelaySlider); setH(repeatRateSlider); setH(sensitivitySlider);
    setH(accelProfileSlider); setH(followMouseSlider); setH(scrollFactorSlider);
    setH(swipeDistSlider); setH(scrollButtonSlider); setH(rotationSlider);
    setH(followMouseShrinkSlider);
    setH(touchdeviceTransformSlider); setH(vkShareStatesSlider);
    setH(tabletTransformSlider); setH(eraserButtonModeSlider);
    setH(eraserButtonOverrideSlider); setH(pressureRangeMinSlider);
    setH(pressureRangeMaxSlider);
    setH(numlockByDefault); setH(naturalScroll); setH(tpNaturalScroll);
    setH(disableWhileTyping); setH(tapToClick); setH(swipeCreateNew);
    setH(specialFallthrough);
    setH(forceNoAccel); setH(leftHanded); setH(scrollButtonLock);
    setH(mouseRefocus); setH(resolveBindsBySym);
    setH(tpClickfingerBehavior); setH(tpMiddleButtonEmulation);
    setH(tpTapAndDrag); setH(tpFlipX); setH(tpFlipY);
    setH(touchdeviceEnabled); setH(vkReleasePressedOnClose);
    setH(tabletAbsoluteRegionPosition); setH(tabletRelativeInput);
    setH(tabletLeftHanded);
  }

  void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, int contentTop) {
    syncColours(app);
    applyColours();
    updateHover(app);

    auto& cfg = app.hyprlandConfig;
    HyprlandCardLayout lay;
    lay.init(contentX, contentW, contentTop);

    auto paintSliderCard = [&](Slider& sl, int val, int lo, int hi, const char* label,
                               const char* const* value_labels = nullptr) {
      hyprland_paint_slider_card(app, cr, lay, glassOv, sl, val, lo, hi, label,
                                 textR_, textG_, textB_, nullptr, value_labels);
    };
    auto paintToggleCard = [&](Toggle& tg, bool val, const char* label, const char* desc = nullptr) {
      hyprland_paint_toggle_card(app, cr, lay, glassOv, tg, val, label,
                                 accentR_, accentG_, accentB_, textR_, textG_, textB_, desc);
    };

    // Keyboard
    lay.beginGroup(app, cr, glassOv, "Keyboard", "Repeat, rotation, and key handling", 3, 3,
                   textR_, textG_, textB_);
    paintToggleCard(numlockByDefault, cfg.input.numlock_by_default, "Numlock by default", "Enable numlock at startup");
    paintToggleCard(resolveBindsBySym, cfg.input.resolve_binds_by_sym, "Resolve binds by sym", "Resolve keybinds by keysym");
    paintToggleCard(forceNoAccel, cfg.input.force_no_accel, "Force no accel", "Disable mouse acceleration");
    paintSliderCard(repeatDelaySlider, cfg.input.repeat_delay, 100, 1000, "Repeat delay");
    paintSliderCard(repeatRateSlider, cfg.input.repeat_rate, 10, 100, "Repeat rate");
    paintSliderCard(rotationSlider, cfg.input.rotation, 0, 3, "Input rotation");
    lay.endGroup();

    // Mouse
    lay.beginGroup(app, cr, glassOv, "Mouse", "Pointer speed, acceleration, and behavior", 4, 5,
                   textR_, textG_, textB_);
    paintToggleCard(naturalScroll, cfg.input.natural_scroll, "Natural scroll", "Invert mouse scroll direction");
    paintToggleCard(leftHanded, cfg.input.left_handed, "Left handed", "Swap mouse buttons for left-handed use");
    paintToggleCard(mouseRefocus, cfg.input.mouse_refocus, "Mouse refocus", "Refocus window on mouse move");
    paintToggleCard(scrollButtonLock, cfg.input.scroll_button_lock, "Scroll button lock", "Do not need to hold the scroll button");
    paintSliderCard(sensitivitySlider, 50 + static_cast<int>(cfg.input.sensitivity * 50.0), 0, 100, "Sensitivity");
    paintSliderCard(accelProfileSlider, accelProfileIndex(cfg.input.accel_profile), 0, 2, "Accel profile", kAccelProfileNames);
    paintSliderCard(followMouseSlider, cfg.input.follow_mouse, 0, 3, "Follow mouse");
    paintSliderCard(scrollButtonSlider, cfg.input.scroll_button, 0, 300, "Scroll button");
    paintSliderCard(followMouseShrinkSlider, cfg.input.follow_mouse_shrink, 0, 10, "Follow mouse shrink");
    lay.endGroup();

    // Touchpad
    lay.beginGroup(app, cr, glassOv, "Touchpad", "Scroll, tap, and gesture behavior", 8, 1,
                   textR_, textG_, textB_);
    paintToggleCard(tpNaturalScroll, cfg.input.touchpad.natural_scroll, "Natural scroll", "Invert touchpad scroll direction");
    paintToggleCard(disableWhileTyping, cfg.input.touchpad.disable_while_typing, "Disable while typing", "Pause touchpad while typing");
    paintToggleCard(tapToClick, cfg.input.touchpad.tap_to_click, "Tap to click", "Enable tap-to-click on touchpad");
    paintToggleCard(tpClickfingerBehavior, cfg.input.touchpad.clickfinger_behavior, "Clickfinger behavior", "Use finger count for button mapping");
    paintToggleCard(tpMiddleButtonEmulation, cfg.input.touchpad.middle_button_emulation, "Emulate middle click with two-finger tap");
    paintToggleCard(tpTapAndDrag, cfg.input.touchpad.tap_and_drag, "Tap and drag", "Enable tap-and-drag gesture");
    paintToggleCard(tpFlipX, cfg.input.touchpad.flip_x, "Flip X", "Invert touchpad X axis");
    paintToggleCard(tpFlipY, cfg.input.touchpad.flip_y, "Flip Y", "Invert touchpad Y axis");
    paintSliderCard(scrollFactorSlider, static_cast<int>(cfg.input.touchpad.scroll_factor * 100), 10, 200, "Scroll factor");
    lay.endGroup();

    // Gestures
    lay.beginGroup(app, cr, glassOv, "Gestures", "Workspace swipe and gesture controls", 1, 1,
                   textR_, textG_, textB_);
    paintToggleCard(swipeCreateNew, cfg.gestures.workspace_swipe_create_new, "Swipe create new", "Create new workspace on swipe");
    paintSliderCard(swipeDistSlider, cfg.gestures.workspace_swipe_distance, 100, 1000, "Swipe distance");
    lay.endGroup();

    // Window Behavior
    lay.beginGroup(app, cr, glassOv, "Window Behavior", "Focus and keybind fallthrough behavior", 1, 0,
                   textR_, textG_, textB_);
    paintToggleCard(specialFallthrough, cfg.input.special_fallthrough, "Special fallthrough", "Let keybinds reach the fallthrough window");
    lay.endGroup();

    // Touch Device
    lay.beginGroup(app, cr, glassOv, "Touch Device", "Touchscreen input mapping", 1, 1,
                   textR_, textG_, textB_);
    paintToggleCard(touchdeviceEnabled, cfg.input.touchdevice.enabled, "Enabled", "Enable input for touch devices");
    paintSliderCard(touchdeviceTransformSlider, cfg.input.touchdevice.transform, 0, 6, "Transform");
    lay.endGroup();

    // Virtual Keyboard
    lay.beginGroup(app, cr, glassOv, "Virtual Keyboard", "Virtual keyboard state sharing", 1, 1,
                   textR_, textG_, textB_);
    paintToggleCard(vkReleasePressedOnClose, cfg.input.virtualkeyboard.release_pressed_on_close, "Release keys on close", "Release pressed keys when virtual keyboard closes");
    paintSliderCard(vkShareStatesSlider, cfg.input.virtualkeyboard.share_states, 0, 2, "Share states", kShareStatesNames);
    lay.endGroup();

    // Tablet
    lay.beginGroup(app, cr, glassOv, "Tablet", "Tablet mapping and behavior", 3, 1,
                   textR_, textG_, textB_);
    paintToggleCard(tabletAbsoluteRegionPosition, cfg.input.tablet.absolute_region_position, "Absolute region position", "Treat region position as absolute in monitor layout");
    paintToggleCard(tabletRelativeInput, cfg.input.tablet.relative_input, "Relative input", "Use relative input mode");
    paintToggleCard(tabletLeftHanded, cfg.input.tablet.left_handed, "Left handed", "Rotate tablet by 180 degrees");
    paintSliderCard(tabletTransformSlider, cfg.input.tablet.transform, 0, 6, "Transform");
    lay.endGroup();

    // Tablet Tool
    lay.beginGroup(app, cr, glassOv, "Tablet Tool", "Stylus eraser and pressure range", 0, 4,
                   textR_, textG_, textB_);
    paintSliderCard(eraserButtonModeSlider, cfg.input.tablettool.eraser_button_mode, 0, 6, "Eraser button mode");
    paintSliderCard(eraserButtonOverrideSlider, cfg.input.tablettool.eraser_button_override, 0, 300, "Eraser button override");
    paintSliderCard(pressureRangeMinSlider, static_cast<int>(cfg.input.tablettool.pressure_range_min * 100), -100, 100, "Pressure range min");
    paintSliderCard(pressureRangeMaxSlider, static_cast<int>(cfg.input.tablettool.pressure_range_max * 100), -100, 100, "Pressure range max");
    lay.endGroup();

    app.hyprlandContentBottom = lay.groupY + kSpacingL;
  }

  bool handlePointerDown(App&, float px, float py) {
    activeSlider_ = -1;

    auto tryToggle = [&](Toggle* tg) -> bool {
      if (tg->containsPoint(px, py)) { tg->handlePointerDown(px, py); return true; }
      return false;
    };

    if (tryToggle(&numlockByDefault)) return true;
    if (tryToggle(&naturalScroll)) return true;
    if (tryToggle(&tpNaturalScroll)) return true;
    if (tryToggle(&disableWhileTyping)) return true;
    if (tryToggle(&tapToClick)) return true;
    if (tryToggle(&swipeCreateNew)) return true;
    if (tryToggle(&specialFallthrough)) return true;
    if (tryToggle(&forceNoAccel)) return true;
    if (tryToggle(&leftHanded)) return true;
    if (tryToggle(&scrollButtonLock)) return true;
    if (tryToggle(&mouseRefocus)) return true;
    if (tryToggle(&resolveBindsBySym)) return true;
    if (tryToggle(&tpClickfingerBehavior)) return true;
    if (tryToggle(&tpMiddleButtonEmulation)) return true;
    if (tryToggle(&tpTapAndDrag)) return true;
    if (tryToggle(&tpFlipX)) return true;
    if (tryToggle(&tpFlipY)) return true;
    if (tryToggle(&touchdeviceEnabled)) return true;
    if (tryToggle(&vkReleasePressedOnClose)) return true;
    if (tryToggle(&tabletAbsoluteRegionPosition)) return true;
    if (tryToggle(&tabletRelativeInput)) return true;
    if (tryToggle(&tabletLeftHanded)) return true;

    Slider* sliders[] = {&repeatDelaySlider, &repeatRateSlider, &sensitivitySlider,
                         &accelProfileSlider, &followMouseSlider, &scrollFactorSlider,
                         &swipeDistSlider, &scrollButtonSlider, &rotationSlider,
                         &followMouseShrinkSlider,
                         &touchdeviceTransformSlider, &vkShareStatesSlider,
                         &tabletTransformSlider, &eraserButtonModeSlider,
                         &eraserButtonOverrideSlider, &pressureRangeMinSlider,
                         &pressureRangeMaxSlider};
    for (int i = 0; i < 17; ++i) {
      if (sliders[i]->containsPoint(px, py)) {
        sliders[i]->handlePointerDown(px, py);
        activeSlider_ = i;
        return true;
      }
    }

    return false;
  }

  bool handlePointerUp(App& app, float px, float py) {
    bool handled = false;

    auto endToggle = [&](Toggle* tg, bool* setting) {
      if (!tg->pressed()) return;
      tg->handlePointerUp(px, py);
      *setting = !*setting;
      handled = true;
    };

    endToggle(&numlockByDefault, &app.hyprlandConfig.input.numlock_by_default);
    endToggle(&naturalScroll, &app.hyprlandConfig.input.natural_scroll);
    endToggle(&tpNaturalScroll, &app.hyprlandConfig.input.touchpad.natural_scroll);
    endToggle(&disableWhileTyping, &app.hyprlandConfig.input.touchpad.disable_while_typing);
    endToggle(&tapToClick, &app.hyprlandConfig.input.touchpad.tap_to_click);
    endToggle(&swipeCreateNew, &app.hyprlandConfig.gestures.workspace_swipe_create_new);
    endToggle(&specialFallthrough, &app.hyprlandConfig.input.special_fallthrough);
    endToggle(&forceNoAccel, &app.hyprlandConfig.input.force_no_accel);
    endToggle(&leftHanded, &app.hyprlandConfig.input.left_handed);
    endToggle(&scrollButtonLock, &app.hyprlandConfig.input.scroll_button_lock);
    endToggle(&mouseRefocus, &app.hyprlandConfig.input.mouse_refocus);
    endToggle(&resolveBindsBySym, &app.hyprlandConfig.input.resolve_binds_by_sym);
    endToggle(&tpClickfingerBehavior, &app.hyprlandConfig.input.touchpad.clickfinger_behavior);
    endToggle(&tpMiddleButtonEmulation, &app.hyprlandConfig.input.touchpad.middle_button_emulation);
    endToggle(&tpTapAndDrag, &app.hyprlandConfig.input.touchpad.tap_and_drag);
    endToggle(&tpFlipX, &app.hyprlandConfig.input.touchpad.flip_x);
    endToggle(&tpFlipY, &app.hyprlandConfig.input.touchpad.flip_y);
    endToggle(&touchdeviceEnabled, &app.hyprlandConfig.input.touchdevice.enabled);
    endToggle(&vkReleasePressedOnClose, &app.hyprlandConfig.input.virtualkeyboard.release_pressed_on_close);
    endToggle(&tabletAbsoluteRegionPosition, &app.hyprlandConfig.input.tablet.absolute_region_position);
    endToggle(&tabletRelativeInput, &app.hyprlandConfig.input.tablet.relative_input);
    endToggle(&tabletLeftHanded, &app.hyprlandConfig.input.tablet.left_handed);

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&repeatDelaySlider, &repeatRateSlider, &sensitivitySlider,
                           &accelProfileSlider, &followMouseSlider, &scrollFactorSlider,
                           &swipeDistSlider, &scrollButtonSlider, &rotationSlider,
                           &followMouseShrinkSlider,
                           &touchdeviceTransformSlider, &vkShareStatesSlider,
                           &tabletTransformSlider, &eraserButtonModeSlider,
                           &eraserButtonOverrideSlider, &pressureRangeMinSlider,
                           &pressureRangeMaxSlider};
      if (activeSlider_ < 17) {
        sliders[activeSlider_]->handlePointerUp(px, py);
      }
      activeSlider_ = -1;
      handled = true;
    }

    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&repeatDelaySlider, &repeatRateSlider, &sensitivitySlider,
                           &accelProfileSlider, &followMouseSlider, &scrollFactorSlider,
                           &swipeDistSlider, &scrollButtonSlider, &rotationSlider,
                           &followMouseShrinkSlider,
                           &touchdeviceTransformSlider, &vkShareStatesSlider,
                           &tabletTransformSlider, &eraserButtonModeSlider,
                           &eraserButtonOverrideSlider, &pressureRangeMinSlider,
                           &pressureRangeMaxSlider};
      if (activeSlider_ < 17) {
        sliders[activeSlider_]->handlePointerMove(px, py);
      }
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(repeatDelaySlider); reset(repeatRateSlider); reset(sensitivitySlider);
    reset(accelProfileSlider); reset(followMouseSlider); reset(scrollFactorSlider);
    reset(swipeDistSlider); reset(scrollButtonSlider); reset(rotationSlider);
    reset(followMouseShrinkSlider);
    reset(touchdeviceTransformSlider); reset(vkShareStatesSlider);
    reset(tabletTransformSlider); reset(eraserButtonModeSlider);
    reset(eraserButtonOverrideSlider); reset(pressureRangeMinSlider);
    reset(pressureRangeMaxSlider);
    reset(numlockByDefault); reset(naturalScroll); reset(tpNaturalScroll);
    reset(disableWhileTyping); reset(tapToClick); reset(swipeCreateNew);
    reset(specialFallthrough);
    reset(forceNoAccel); reset(leftHanded); reset(scrollButtonLock);
    reset(mouseRefocus); reset(resolveBindsBySym);
    reset(tpClickfingerBehavior); reset(tpMiddleButtonEmulation);
    reset(tpTapAndDrag); reset(tpFlipX); reset(tpFlipY);
    reset(touchdeviceEnabled); reset(vkReleasePressedOnClose);
    reset(tabletAbsoluteRegionPosition); reset(tabletRelativeInput);
    reset(tabletLeftHanded);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0 || activeSlider_ >= 17) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&repeatDelaySlider, &repeatRateSlider, &sensitivitySlider,
                                 &accelProfileSlider, &followMouseSlider, &scrollFactorSlider,
                                 &swipeDistSlider, &scrollButtonSlider, &rotationSlider,
                                 &followMouseShrinkSlider,
                                 &touchdeviceTransformSlider, &vkShareStatesSlider,
                                 &tabletTransformSlider, &eraserButtonModeSlider,
                                 &eraserButtonOverrideSlider, &pressureRangeMinSlider,
                                 &pressureRangeMaxSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.hyprlandConfig.input.repeat_delay = static_cast<int>(v); break;
      case 1: app.hyprlandConfig.input.repeat_rate = static_cast<int>(v); break;
      case 2: app.hyprlandConfig.input.sensitivity = (v / 50.0) - 1.0; break;
      case 3: {
        int idx = static_cast<int>(v);
        if (idx < 0) idx = 0;
        if (idx > 2) idx = 2;
        app.hyprlandConfig.input.accel_profile = kAccelProfileNames[idx];
        break;
      }
      case 4: app.hyprlandConfig.input.follow_mouse = static_cast<int>(v); break;
      case 5: app.hyprlandConfig.input.touchpad.scroll_factor = v / 100.0; break;
      case 6: app.hyprlandConfig.gestures.workspace_swipe_distance = static_cast<int>(v); break;
      case 7: app.hyprlandConfig.input.scroll_button = static_cast<int>(v); break;
      case 8: app.hyprlandConfig.input.rotation = static_cast<int>(v); break;
      case 9: app.hyprlandConfig.input.follow_mouse_shrink = static_cast<int>(v); break;
      case 10: app.hyprlandConfig.input.touchdevice.transform = static_cast<int>(v); break;
      case 11: app.hyprlandConfig.input.virtualkeyboard.share_states = static_cast<int>(v); break;
      case 12: app.hyprlandConfig.input.tablet.transform = static_cast<int>(v); break;
      case 13: app.hyprlandConfig.input.tablettool.eraser_button_mode = static_cast<int>(v); break;
      case 14: app.hyprlandConfig.input.tablettool.eraser_button_override = static_cast<int>(v); break;
      case 15: app.hyprlandConfig.input.tablettool.pressure_range_min = v / 100.0; break;
      case 16: app.hyprlandConfig.input.tablettool.pressure_range_max = v / 100.0; break;
    }
  }
};

inline HyprlandInputM3State& hyprlandInputM3() {
  static HyprlandInputM3State s;
  return s;
}

} // namespace m3::detail
