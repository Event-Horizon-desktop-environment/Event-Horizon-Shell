#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include <cairo/cairo.h>

namespace m3 {

// Flex layout container — arranges children in a row or column.
// Children are positioned during paint() by computing their geometry from
// flex properties (spacing, padding, alignment, grow).
class Flex {
public:
  enum class Direction { Horizontal, Vertical };
  enum class Alignment { Start, Center, End, Stretch };

  struct Child {
    void* widget = nullptr;
    enum Type { Box, Label, Glyph, Separator, Spacer, Button, Slider, Toggle, Checkbox, RadioButton, Input, Select, Flex, Chip, Card, Dialog, NavBar, NavRail, NavDrawer, Tabs, FAB, TopAppBar, BottomAppBar, Snackbar, Banner, BottomSheet, SearchBar, DatePicker, TimePicker, Carousel, Custom };
    Type type = Custom;
    float minW = 0.0f, minH = 0.0f;
    float flexGrow = 0.0f;  // 0 = no grow, 1+ = share leftover space
    bool visible = true;

    // Paint function for custom/unknown types
    void (*paintFn)(void*, cairo_t*) = nullptr;
  };

  Flex() = default;
  ~Flex() = default;

  void setDirection(Direction d) { dir_ = d; }
  void setSpacing(float s) { spacing_ = s; }
  void setPadding(float t, float r, float b, float l) { padT_ = t; padR_ = r; padB_ = b; padL_ = l; }
  void setPadding(float all) { padT_ = padR_ = padB_ = padL_ = all; }
  void setMainAlignment(Alignment a) { mainAlign_ = a; }
  void setCrossAlignment(Alignment a) { crossAlign_ = a; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }

  [[nodiscard]] Direction direction() const noexcept { return dir_; }
  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }

  Child& addChild(void* widget, Child::Type type, float minW = 0, float minH = 0,
                  float flexGrow = 0, void (*paintFn)(void*, cairo_t*) = nullptr) {
    return children_.emplace_back(Child{widget, type, minW, minH, flexGrow, true, paintFn});
  }

  void clear() { children_.clear(); }

  // Lay out children and paint them.
  void paint(cairo_t* cr) const {
    if (children_.empty()) return;

    const float contentX = x_ + padL_;
    const float contentY = y_ + padT_;
    const float contentW = std::max(0.0f, w_ - padL_ - padR_);
    const float contentH = std::max(0.0f, h_ - padT_ - padB_);

    if (dir_ == Direction::Horizontal) {
      layoutHorizontal(cr, contentX, contentY, contentW, contentH);
    } else {
      layoutVertical(cr, contentX, contentY, contentW, contentH);
    }
  }

private:
  void paintChild(void* widget, Child::Type type, cairo_t* cr,
                  float cx, float cy, float cw, float ch,
                  void (*customPaint)(void*, cairo_t*)) const {
    // Set geometry on each child before painting
    switch (type) {
      case Child::Box: {
        auto* b = static_cast<class Box*>(widget);
        b->setGeometry(cx, cy, cw, ch);
        b->paint(cr);
        break;
      }
      case Child::Label: {
        auto* l = static_cast<class Label*>(widget);
        l->setGeometry(cx, cy, cw, ch);
        l->paint(cr);
        break;
      }
      case Child::Glyph: {
        auto* g = static_cast<class Glyph*>(widget);
        g->setGeometry(cx, cy, cw, ch);
        g->paint(cr);
        break;
      }
      case Child::Separator: {
        auto* s = static_cast<class Separator*>(widget);
        s->setGeometry(cx, cy, cw, ch);
        s->paint(cr);
        break;
      }
      case Child::Spacer: {
        auto* s = static_cast<class Spacer*>(widget);
        s->setGeometry(cx, cy, cw, ch);
        break;
      }
      case Child::Button: {
        auto* b = static_cast<class Button*>(widget);
        b->setGeometry(cx, cy, cw, ch);
        b->paint(cr);
        break;
      }
      case Child::Slider: {
        auto* s = static_cast<class Slider*>(widget);
        s->setGeometry(cx, cy, cw, ch);
        s->paint(cr);
        break;
      }
      case Child::Toggle: {
        auto* t = static_cast<class Toggle*>(widget);
        t->setGeometry(cx, cy, cw, ch);
        t->paint(cr);
        break;
      }
      case Child::Checkbox: {
        auto* c = static_cast<class Checkbox*>(widget);
        c->setGeometry(cx, cy, cw, ch);
        c->paint(cr);
        break;
      }
      case Child::RadioButton: {
        auto* r = static_cast<class RadioButton*>(widget);
        r->setGeometry(cx, cy, cw, ch);
        r->paint(cr);
        break;
      }
      case Child::Input: {
        auto* i = static_cast<class Input*>(widget);
        i->setGeometry(cx, cy, cw, ch);
        i->paint(cr);
        break;
      }
      case Child::Select: {
        auto* s = static_cast<class Select*>(widget);
        s->setGeometry(cx, cy, cw, ch);
        s->paint(cr);
        break;
      }
      case Child::Flex: {
        auto* f = static_cast<Flex*>(widget);
        f->setGeometry(cx, cy, cw, ch);
        f->paint(cr);
        break;
      }
      case Child::Chip: {
        auto* c = static_cast<class Chip*>(widget);
        c->setGeometry(cx, cy, cw, ch);
        c->paint(cr);
        break;
      }
      case Child::Card: {
        auto* c = static_cast<class Card*>(widget);
        c->setGeometry(cx, cy, cw, ch);
        c->paint(cr);
        break;
      }
      case Child::Dialog: {
        auto* d = static_cast<class Dialog*>(widget);
        d->setGeometry(cx, cy, cw, ch);
        d->paint(cr);
        break;
      }
      case Child::NavBar: {
        auto* n = static_cast<class NavBar*>(widget);
        n->setGeometry(cx, cy, cw, ch);
        n->paint(cr);
        break;
      }
      case Child::NavRail: {
        auto* n = static_cast<class NavRail*>(widget);
        n->setGeometry(cx, cy, cw, ch);
        n->paint(cr);
        break;
      }
      case Child::NavDrawer: {
        auto* n = static_cast<class NavDrawer*>(widget);
        n->setGeometry(cx, cy, cw, ch);
        n->paint(cr);
        break;
      }
      case Child::Tabs: {
        auto* t = static_cast<class Tabs*>(widget);
        t->setGeometry(cx, cy, cw, ch);
        t->paint(cr);
        break;
      }
      case Child::FAB: {
        auto* f = static_cast<class FAB*>(widget);
        f->setGeometry(cx, cy, cw, ch);
        f->paint(cr);
        break;
      }
      case Child::TopAppBar: {
        auto* t = static_cast<class TopAppBar*>(widget);
        t->setGeometry(cx, cy, cw, ch);
        t->paint(cr);
        break;
      }
      case Child::BottomAppBar: {
        auto* b = static_cast<class BottomAppBar*>(widget);
        b->setGeometry(cx, cy, cw, ch);
        b->paint(cr);
        break;
      }
      case Child::Snackbar: {
        auto* s = static_cast<class Snackbar*>(widget);
        s->setGeometry(cx, cy, cw, ch);
        s->paint(cr);
        break;
      }
      case Child::Banner: {
        auto* b = static_cast<class Banner*>(widget);
        b->setGeometry(cx, cy, cw, ch);
        b->paint(cr);
        break;
      }
      case Child::BottomSheet: {
        auto* b = static_cast<class BottomSheet*>(widget);
        b->setGeometry(cx, cy, cw, ch);
        b->paint(cr);
        break;
      }
      case Child::SearchBar: {
        auto* s = static_cast<class SearchBar*>(widget);
        s->setGeometry(cx, cy, cw, ch);
        s->paint(cr);
        break;
      }
      case Child::DatePicker: {
        auto* d = static_cast<class DatePicker*>(widget);
        d->setGeometry(cx, cy, cw, ch);
        d->paint(cr);
        break;
      }
      case Child::TimePicker: {
        auto* t = static_cast<class TimePicker*>(widget);
        t->setGeometry(cx, cy, cw, ch);
        t->paint(cr);
        break;
      }
      case Child::Carousel: {
        auto* c = static_cast<class Carousel*>(widget);
        c->setGeometry(cx, cy, cw, ch);
        c->paint(cr);
        break;
      }
      case Child::Custom:
        if (customPaint) customPaint(widget, cr);
        break;
      default:
        break;
    }
  }

  void layoutHorizontal(cairo_t* cr, float cx, float cy, float cw, float ch) const {
    // Measure fixed-size children, compute total flex grow
    float fixedSize = 0.0f;
    float totalGrow = 0.0f;
    int visibleCount = 0;

    for (const auto& child : children_) {
      if (!child.visible) continue;
      fixedSize += child.minW;
      totalGrow += child.flexGrow;
      ++visibleCount;
    }
    fixedSize += std::max(0, visibleCount - 1) * spacing_;

    const float leftover = std::max(0.0f, cw - fixedSize);
    const float growUnit = totalGrow > 0 ? leftover / totalGrow : 0.0f;

    // Compute cross-axis sizes
    float crossSize = ch;
    for (const auto& child : children_) {
      if (!child.visible) continue;
      if (crossAlign_ == Alignment::Stretch) {
        crossSize = ch;
      } else {
        crossSize = std::min(ch, child.minH > 0 ? child.minH : ch);
      }
    }

    // Distribute based on alignment
    float totalContentSize = fixedSize;
    if (totalGrow > 0) {
      for (const auto& child : children_) {
        if (!child.visible) continue;
        totalContentSize += child.flexGrow * growUnit;
      }
    }

    float startX = cx;
    if (mainAlign_ == Alignment::Center) {
      startX = cx + (cw - totalContentSize) * 0.5f;
    } else if (mainAlign_ == Alignment::End) {
      startX = cx + cw - totalContentSize;
    }

    float curX = startX;
    for (const auto& child : children_) {
      if (!child.visible) continue;

      const float childW = child.minW + child.flexGrow * growUnit;
      const float childH = crossSize;

      float childY = cy;
      if (crossAlign_ == Alignment::Center) {
        childY = cy + (ch - childH) * 0.5f;
      } else if (crossAlign_ == Alignment::End) {
        childY = cy + ch - childH;
      }

      paintChild(child.widget, child.type, cr, curX, childY, childW, childH, child.paintFn);
      curX += childW + spacing_;
    }
  }

  void layoutVertical(cairo_t* cr, float cx, float cy, float cw, float ch) const {
    float fixedSize = 0.0f;
    float totalGrow = 0.0f;
    int visibleCount = 0;

    for (const auto& child : children_) {
      if (!child.visible) continue;
      fixedSize += child.minH;
      totalGrow += child.flexGrow;
      ++visibleCount;
    }
    fixedSize += std::max(0, visibleCount - 1) * spacing_;

    const float leftover = std::max(0.0f, ch - fixedSize);
    const float growUnit = totalGrow > 0 ? leftover / totalGrow : 0.0f;

    float totalContentSize = fixedSize;
    if (totalGrow > 0) {
      for (const auto& child : children_) {
        if (!child.visible) continue;
        totalContentSize += child.flexGrow * growUnit;
      }
    }

    float startY = cy;
    if (mainAlign_ == Alignment::Center) {
      startY = cy + (ch - totalContentSize) * 0.5f;
    } else if (mainAlign_ == Alignment::End) {
      startY = cy + ch - totalContentSize;
    }

    float curY = startY;
    for (const auto& child : children_) {
      if (!child.visible) continue;

      const float childH = child.minH + child.flexGrow * growUnit;
      const float childW = cw;

      float childX = cx;
      if (crossAlign_ == Alignment::Center) {
        childX = cx + (cw - childW) * 0.5f;
      } else if (crossAlign_ == Alignment::End) {
        childX = cx + cw - childW;
      }

      paintChild(child.widget, child.type, cr, childX, curY, childW, childH, child.paintFn);
      curY += childH + spacing_;
    }
  }

  Direction dir_ = Direction::Horizontal;
  Alignment mainAlign_ = Alignment::Start;
  Alignment crossAlign_ = Alignment::Start;
  float spacing_ = 0.0f;
  float padT_ = 0, padR_ = 0, padB_ = 0, padL_ = 0;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
  std::vector<Child> children_;
};

} // namespace m3
