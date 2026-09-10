#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "m3/core/primitives/box.hpp"
#include "m3/core/primitives/flex.hpp"
#include "m3/core/glyph.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/separator.hpp"
#include "m3/core/primitives/spacer.hpp"
#include "m3/controls/containers/button.hpp"
#include "m3/controls/display/banner.hpp"
#include "m3/controls/navigation/bottom_app_bar.hpp"
#include "m3/controls/display/bottom_sheet.hpp"
#include "m3/controls/containers/card.hpp"
#include "m3/controls/display/carousel.hpp"
#include "m3/controls/input/checkbox.hpp"
#include "m3/controls/input/chip.hpp"
#include "m3/controls/display/dialog.hpp"
#include "m3/controls/containers/fab.hpp"
#include "m3/controls/input/input.hpp"
#include "m3/controls/navigation/nav_bar.hpp"
#include "m3/controls/navigation/nav_drawer.hpp"
#include "m3/controls/navigation/nav_rail.hpp"
#include "m3/controls/navigation/tabs.hpp"
#include "m3/controls/input/radio_button.hpp"
#include "m3/controls/navigation/search_bar.hpp"
#include "m3/controls/input/select.hpp"
#include "m3/controls/input/slider.hpp"
#include "m3/controls/display/snackbar.hpp"
#include "m3/controls/input/toggle.hpp"
#include "m3/controls/navigation/top_app_bar.hpp"
#include "m3/controls/input/date_picker.hpp"
#include "m3/controls/input/time_picker.hpp"

namespace m3 {

// Builder helpers for the basic display controls.

inline Label& label() {
  static Label instance;
  instance = Label{};
  return instance;
}

inline Glyph& glyph() {
  static Glyph instance;
  instance = Glyph{};
  return instance;
}

inline Box& box() {
  static Box instance;
  instance = Box{};
  return instance;
}

inline Separator& separator() {
  static Separator instance;
  instance = Separator{};
  return instance;
}

inline Spacer& spacer() {
  static Spacer instance;
  instance = Spacer{};
  return instance;
}

// Fluent label builder.
struct LabelBuilder {
  Label widget;
  LabelBuilder& text(std::string_view t) { widget.setText(t); return *this; }
  LabelBuilder& font(const FontConfig& fc) { widget.setFontConfig(fc); return *this; }
  LabelBuilder& fontSize(float sp) { widget.setFontSize(sp); return *this; }
  LabelBuilder& color(float r, float g, float b, float a = 1.0f) { widget.setColor(r, g, b, a); return *this; }
  operator Label&() { return widget; }
};

inline LabelBuilder labelBuilder() { return {}; }

// Fluent Flex builder.
class FlexBuilder {
public:
  FlexBuilder& horizontal() { flex_.setDirection(Flex::Direction::Horizontal); return *this; }
  FlexBuilder& vertical() { flex_.setDirection(Flex::Direction::Vertical); return *this; }
  FlexBuilder& spacing(float s) { flex_.setSpacing(s); return *this; }
  FlexBuilder& padding(float all) { flex_.setPadding(all); return *this; }
  FlexBuilder& padding(float t, float r, float b, float l) { flex_.setPadding(t, r, b, l); return *this; }
  FlexBuilder& mainAlign(Flex::Alignment a) { flex_.setMainAlignment(a); return *this; }
  FlexBuilder& crossAlign(Flex::Alignment a) { flex_.setCrossAlignment(a); return *this; }
  FlexBuilder& geometry(float x, float y, float w, float h) { flex_.setGeometry(x, y, w, h); return *this; }

  template <typename T>
  FlexBuilder& add(T& child, float minW = 0, float minH = 0, float flexGrow = 0) {
    Flex::Child::Type t = typeFor<T>();
    flex_.addChild(&child, t, minW, minH, flexGrow);
    return *this;
  }

  operator Flex&() { return flex_; }

private:
  template <typename T>
  static Flex::Child::Type typeFor() {
    if constexpr (std::is_same_v<T, Box>) return Flex::Child::Box;
    else if constexpr (std::is_same_v<T, Label>) return Flex::Child::Label;
    else if constexpr (std::is_same_v<T, Glyph>) return Flex::Child::Glyph;
    else if constexpr (std::is_same_v<T, Separator>) return Flex::Child::Separator;
    else if constexpr (std::is_same_v<T, Spacer>) return Flex::Child::Spacer;
    else if constexpr (std::is_same_v<T, Flex>) return Flex::Child::Flex;
    else if constexpr (std::is_same_v<T, Button>) return Flex::Child::Button;
    else if constexpr (std::is_same_v<T, Slider>) return Flex::Child::Slider;
    else if constexpr (std::is_same_v<T, Toggle>) return Flex::Child::Toggle;
    else if constexpr (std::is_same_v<T, Checkbox>) return Flex::Child::Checkbox;
    else if constexpr (std::is_same_v<T, RadioButton>) return Flex::Child::RadioButton;
    else if constexpr (std::is_same_v<T, Input>) return Flex::Child::Input;
    else if constexpr (std::is_same_v<T, Select>) return Flex::Child::Select;
    else if constexpr (std::is_same_v<T, Chip>) return Flex::Child::Chip;
    else if constexpr (std::is_same_v<T, Card>) return Flex::Child::Card;
    else if constexpr (std::is_same_v<T, Dialog>) return Flex::Child::Dialog;
    else if constexpr (std::is_same_v<T, NavBar>) return Flex::Child::NavBar;
    else if constexpr (std::is_same_v<T, NavRail>) return Flex::Child::NavRail;
    else if constexpr (std::is_same_v<T, NavDrawer>) return Flex::Child::NavDrawer;
    else if constexpr (std::is_same_v<T, Tabs>) return Flex::Child::Tabs;
    else if constexpr (std::is_same_v<T, FAB>) return Flex::Child::FAB;
    else if constexpr (std::is_same_v<T, TopAppBar>) return Flex::Child::TopAppBar;
    else if constexpr (std::is_same_v<T, BottomAppBar>) return Flex::Child::BottomAppBar;
    else if constexpr (std::is_same_v<T, Snackbar>) return Flex::Child::Snackbar;
    else if constexpr (std::is_same_v<T, Banner>) return Flex::Child::Banner;
    else if constexpr (std::is_same_v<T, BottomSheet>) return Flex::Child::BottomSheet;
    else if constexpr (std::is_same_v<T, SearchBar>) return Flex::Child::SearchBar;
    else if constexpr (std::is_same_v<T, DatePicker>) return Flex::Child::DatePicker;
    else if constexpr (std::is_same_v<T, TimePicker>) return Flex::Child::TimePicker;
    else if constexpr (std::is_same_v<T, Carousel>) return Flex::Child::Carousel;
    else return Flex::Child::Custom;
  }

  Flex flex_;
};

inline FlexBuilder flex() { return {}; }

} // namespace m3
