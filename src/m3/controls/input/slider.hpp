#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include <cairo/cairo.h>

#include "m3/core/animation.hpp"
#include "m3/core/focus_ring.hpp"
#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Slider — continuous/discrete, active+inactive track, stop indicator,
// value indicator popup, thumb press shrink, centered variant.
class Slider {
public:
  enum class Size { XS, S, M, L, XL };

  Slider() = default;

  void setRange(float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    lo_ = lo; hi_ = hi;
    applyValue(value_);
  }

  void setStep(float step) { step_ = std::max(step, 0.0f); applyValue(value_); }
  void setValue(float val) { animDurationMs_ = 0; applyValue(val); }

  void animateToValue(float val, uint64_t nowMs) {
    if (dragging_ || std::abs(val - value_) < 0.0001f) { applyValue(val); return; }
    animFrom_ = value_;
    animTo_ = snap(val);
    animStartMs_ = nowMs;
    animDurationMs_ = 100;
  }

  void setSize(Size s) { size_ = s; }
  void setEnabled(bool e) { enabled_ = e; if (!e) { hovered_ = false; dragging_ = false; } }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setHovered(bool h) { forceHover_ = true; hovered_ = h; }
  void setPressed(bool p) { forcePressed_ = true; dragging_ = p; }
  void setFocused(bool f) { focused_ = f; }
  void setCentered(bool c) { centered_ = c; }
  void setShowValueLabel(bool s) { showValueLabel_ = s; }
  void setValueLabel(const char* text) { valueLabel_ = text ? text : ""; }

  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }

  void setOnValueChanged(std::function<void(float)> cb) { onValueChanged_ = std::move(cb); }
  void setOnDragStarted(std::function<void()> cb) { onDragStarted_ = std::move(cb); }
  void setOnDragFinished(std::function<void()> cb) { onDragFinished_ = std::move(cb); }

  [[nodiscard]] float value() const noexcept { return value_; }
  [[nodiscard]] float minValue() const noexcept { return lo_; }
  [[nodiscard]] float maxValue() const noexcept { return hi_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool dragging() const noexcept { return dragging_; }
  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }
  [[nodiscard]] bool isAnimating() const noexcept { return animDurationMs_ > 0; }
  [[nodiscard]] float animatedValue(uint64_t nowMs) const;

  bool containsPoint(float px, float py) const noexcept;
  float grabOffset(float px) const noexcept;

  bool handlePointerEnter(float, float);
  bool handlePointerLeave();
  bool handlePointerDown(float px, float py);
  bool handlePointerMove(float px, float);
  bool handlePointerUp(float, float);
  bool handleScroll(float delta);

  void paint(cairo_t* cr) const;
  void paint(cairo_t* cr, uint64_t nowMs);

private:
  void applyValue(float raw);
  [[nodiscard]] float snap(float val) const noexcept;
  [[nodiscard]] float normalized() const noexcept;
  [[nodiscard]] float normalizedCentered() const noexcept;
  [[nodiscard]] float thumbCenter() const noexcept;
  [[nodiscard]] float trackLeft() const noexcept;
  [[nodiscard]] float trackWidth() const noexcept;

  void drawStopIndicator(cairo_t* cr, float tl, float tw, float ty) const;
  void drawValuePopup(cairo_t* cr, float tc, float ty, float tw, float alpha) const;

  // Size constants
  static constexpr float kThumbSize[] = { 16.0f, 16.0f, 20.0f, 24.0f, 24.0f };
  static constexpr float kTrackHeight[] = { 4.0f, 4.0f, 4.0f, 6.0f, 6.0f };

  float lo_ = 0.0f, hi_ = 100.0f, step_ = 1.0f, value_ = 50.0f;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
  Size size_ = Size::M;
  bool enabled_ = true, hovered_ = false, dragging_ = false;
  bool forceHover_ = false, forcePressed_ = false;
  bool centered_ = false;
  bool showValueLabel_ = false;
  bool focused_ = false;
  std::string valueLabel_;
  float grabOffsetX_ = 0;

  uint64_t animStartMs_ = 0;
  float animFrom_ = 0, animTo_ = 0;
  uint64_t animDurationMs_ = 0;

  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;

  StateLayer stateLayer_;
  FocusRing focusRing_;

  std::function<void(float)> onValueChanged_;
  std::function<void()> onDragStarted_;
  std::function<void()> onDragFinished_;
};

// Inline implementations.

inline float Slider::animatedValue(uint64_t nowMs) const {
  if (animDurationMs_ == 0) return value_;
  const uint64_t elapsed = nowMs - animStartMs_;
  if (elapsed >= animDurationMs_) return animTo_;
  const float t = static_cast<float>(elapsed) / static_cast<float>(animDurationMs_);
  const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
  return animFrom_ + (animTo_ - animFrom_) * ease;
}

inline float Slider::normalized() const noexcept {
  if (hi_ <= lo_) return 0.0f;
  return std::clamp((value_ - lo_) / (hi_ - lo_), 0.0f, 1.0f);
}

inline float Slider::normalizedCentered() const noexcept {
  if (hi_ <= lo_) return 0.0f;
  const float n = (value_ - lo_) / (hi_ - lo_);
  return n * 2.0f - 1.0f; // -1 to +1
}

inline float Slider::trackLeft() const noexcept {
  const float ts = kThumbSize[static_cast<int>(size_)] * 0.5f;
  return x_ + ts;
}

inline float Slider::trackWidth() const noexcept {
  const float ts = kThumbSize[static_cast<int>(size_)];
  return std::max(0.0f, w_ - ts);
}

inline float Slider::thumbCenter() const noexcept {
  if (centered_) {
    return trackLeft() + (normalizedCentered() * 0.5f + 0.5f) * trackWidth();
  }
  return trackLeft() + normalized() * trackWidth();
}

inline float Slider::snap(float val) const noexcept {
  const float clamped = std::clamp(val, lo_, hi_);
  if (step_ <= 0.0f || hi_ <= lo_) return clamped;
  const float steps = std::round((clamped - lo_) / step_);
  return std::clamp(lo_ + steps * step_, lo_, hi_);
}

inline void Slider::applyValue(float raw) {
  const float snapped = snap(raw);
  if (std::abs(snapped - value_) < 0.0001f) return;
  value_ = snapped;
  if (onValueChanged_) onValueChanged_(value_);
}

inline bool Slider::containsPoint(float px, float py) const noexcept {
  return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
}

inline float Slider::grabOffset(float px) const noexcept {
  return px - thumbCenter();
}

inline bool Slider::handlePointerEnter(float, float) {
  if (!enabled_) return false;
  hovered_ = true;
  return true;
}

inline bool Slider::handlePointerLeave() {
  hovered_ = false;
  return true;
}

inline bool Slider::handlePointerDown(float px, float py) {
  if (!enabled_ || !containsPoint(px, py)) return false;
  animDurationMs_ = 0;
  dragging_ = true;
  grabOffsetX_ = grabOffset(px);
  applyValue(lo_ + ((px - grabOffsetX_ - trackLeft()) / trackWidth()) * (hi_ - lo_));
  if (onDragStarted_) onDragStarted_();
  return true;
}

inline bool Slider::handlePointerMove(float px, float) {
  if (!enabled_ || !dragging_) return false;
  applyValue(lo_ + ((px - grabOffsetX_ - trackLeft()) / trackWidth()) * (hi_ - lo_));
  return true;
}

inline bool Slider::handlePointerUp(float, float) {
  if (!enabled_ || !dragging_) return false;
  dragging_ = false;
  if (onDragFinished_) onDragFinished_();
  return true;
}

inline bool Slider::handleScroll(float delta) {
  if (!enabled_) return false;
  const float adj = step_ > 0.0f ? step_ : (hi_ - lo_) * 0.05f;
  applyValue(value_ - delta * adj);
  return true;
}

inline void Slider::drawStopIndicator(cairo_t* cr, float tl, float tw, float ty) const {
  // Small dot at the end of the active track
  const float dotR = 2.0f;
  cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 0.6f);
  cairo_arc(cr, tl + tw, ty, dotR, 0, 2.0 * M_PI);
  cairo_fill(cr);
}

inline void Slider::drawValuePopup(cairo_t* cr, float tc, float ty, float tw, float alpha) const {
  (void)tw;
  if (!showValueLabel_ || valueLabel_.empty()) return;

  // Fast path: the popup used to rasterize its text with cairo_text_path +
  // a 2.2px stroke + fill EVERY frame (~20-45us during drags, when the value
  // changes each frame). Glyph shapes for a 10px bold Sans value string come
  // from a tiny alphabet (digits, '.', 'x', "nits"), so pre-render each
  // distinct glyph ONCE (white fill + dark outline baked in with the same ops
  // as the old path) and blit per frame. Same for the rounded-rect background
  // (keyed by its computed width) and the arrow (shape-constant, tinted via
  // mask). Geometry, advances and colors match the old path exactly (toy font
  // has no kerning/ligatures, so per-glyph advances sum to the whole-string
  // advance). One known delta: adjacent glyphs' 2.2px outlines overlap, and
  // separate per-glyph strokes double-darken those ~1px seam columns versus
  // the old single combined stroke (up to ~127 channel delta in seam pixels,
  // invisible at 1x, measurable under magnification).
  struct PEntry {
    cairo_surface_t* surf = nullptr;
    int sw = 0, sh = 0;
    float adv = 0;  // user-unit x advance
    float xb = 0, yb = 0;  // bearing box top-left, user units from baseline origin
  };
  static std::unordered_map<uint64_t, PEntry> s_glyphs;
  static std::unordered_map<uint64_t, PEntry> s_bgs;
  static cairo_surface_t* s_arrow = nullptr;
  static int s_arrowSw = 0, s_arrowSh = 0, s_arrowDsQ = 0;

  cairo_matrix_t ctm;
  cairo_get_matrix(cr, &ctm);
  double ds = std::hypot(ctm.xx, ctm.xy);
  if (!(ds > 0.0) || !std::isfinite(ds)) ds = 1.0;
  const int dsQ = static_cast<int>(std::lround(ds * 128.0));
  const int alphaQ = std::clamp(static_cast<int>(std::lround(alpha * 255.0f)), 0, 255);

  auto clear_glyphs = []() {
    for (auto& kv : s_glyphs)
      if (kv.second.surf) cairo_surface_destroy(kv.second.surf);
    s_glyphs.clear();
  };
  auto clear_bgs = []() {
    for (auto& kv : s_bgs)
      if (kv.second.surf) cairo_surface_destroy(kv.second.surf);
    s_bgs.clear();
  };

  // Decode one UTF-8 code point; advances *p past it (U+FFFD on bad input).
  auto utf8_next = [](const char*& p) -> uint32_t {
    const unsigned char c0 = static_cast<unsigned char>(*p);
    if (c0 < 0x80) {
      if (c0 == 0) return 0;
      ++p;
      return c0;
    }
    if ((c0 & 0xE0) == 0xC0) {
      const unsigned char c1 = static_cast<unsigned char>(p[1]);
      if ((c1 & 0xC0) != 0x80) { ++p; return 0xFFFD; }
      p += 2;
      return (static_cast<uint32_t>(c0 & 0x1F) << 6) | (c1 & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0) {
      const unsigned char c1 = static_cast<unsigned char>(p[1]);
      const unsigned char c2 = static_cast<unsigned char>(p[2]);
      if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) { ++p; return 0xFFFD; }
      p += 3;
      return (static_cast<uint32_t>(c0 & 0x0F) << 12) | (static_cast<uint32_t>(c1 & 0x3F) << 6) |
             (c2 & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0) {
      const unsigned char c1 = static_cast<unsigned char>(p[1]);
      const unsigned char c2 = static_cast<unsigned char>(p[2]);
      const unsigned char c3 = static_cast<unsigned char>(p[3]);
      if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
        ++p;
        return 0xFFFD;
      }
      p += 4;
      return (static_cast<uint32_t>(c0 & 0x07) << 18) | (static_cast<uint32_t>(c1 & 0x3F) << 12) |
             (static_cast<uint32_t>(c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    ++p;
    return 0xFFFD;
  };

  // Total advance from cached (or freshly measured) per-glyph advances.
  float totalAdv = 0.0f;
  {
    const char* p = valueLabel_.c_str();
    while (*p) {
      const char* start = p;
      const uint32_t cp = utf8_next(p);
      if (cp == 0) break;
      const uint64_t gkey =
          (static_cast<uint64_t>(cp) << 24) | (static_cast<uint64_t>(dsQ & 0xFFFF) << 8) |
          static_cast<uint64_t>(alphaQ & 0xFF);
      auto git = s_glyphs.find(gkey);
      if (git != s_glyphs.end() && git->second.surf) {
        totalAdv += git->second.adv;
        continue;
      }
      // Cold glyph: measure with the same toy-font calls the old path used.
      char buf[8] = {0, 0, 0, 0, 0, 0, 0, 0};
      size_t bl = 0;
      for (const char* q = start; q < p && bl < sizeof(buf) - 1; ++q) buf[bl++] = *q;
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 10);
      cairo_text_extents_t te{};
      cairo_text_extents(cr, buf, &te);
      totalAdv += static_cast<float>(te.x_advance);
      // Render white fill + dark outline once (the old per-frame ops).
      if (s_glyphs.size() >= 256) clear_glyphs();
      const int pad = 3;
      const int gsw = std::max(1, static_cast<int>(std::ceil((te.width + 2 * pad) * ds)));
      const int gsh = std::max(1, static_cast<int>(std::ceil((te.height + 2 * pad) * ds)));
      PEntry e;
      e.surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gsw, gsh);
      e.sw = gsw;
      e.sh = gsh;
      e.adv = static_cast<float>(te.x_advance);
      e.xb = static_cast<float>(te.x_bearing);
      e.yb = static_cast<float>(te.y_bearing);
      cairo_t* tmp = cairo_create(e.surf);
      cairo_set_operator(tmp, CAIRO_OPERATOR_CLEAR);
      cairo_paint(tmp);
      cairo_set_operator(tmp, CAIRO_OPERATOR_OVER);
      cairo_scale(tmp, ds, ds);
      cairo_translate(tmp, pad - te.x_bearing, pad - te.y_bearing);
      cairo_select_font_face(tmp, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(tmp, 10);
      cairo_new_path(tmp);
      cairo_move_to(tmp, 0, 0);
      cairo_text_path(tmp, buf);
      cairo_set_line_width(tmp, 2.2f);
      cairo_set_line_join(tmp, CAIRO_LINE_JOIN_ROUND);
      cairo_set_line_cap(tmp, CAIRO_LINE_CAP_ROUND);
      cairo_set_source_rgba(tmp, 0.0f, 0.0f, 0.0f, alpha * 0.9f);
      cairo_stroke_preserve(tmp);
      cairo_set_source_rgba(tmp, 1.0f, 1.0f, 1.0f, alpha);
      cairo_fill(tmp);
      cairo_destroy(tmp);
      s_glyphs.emplace(gkey, e);
    }
  }

  // Same popup geometry the old code computed (te.x_advance == totalAdv: no kerning in toy API).
  const float popupW = std::clamp(totalAdv + 18.0f, 44.0f, 180.0f);
  const float popupH = 26.0f;
  const float popupR = 9.0f;

  float popupX = tc - popupW * 0.5f;
  if (popupW <= w_) {
    popupX = std::clamp(popupX, x_, x_ + w_ - popupW);
  } else {
    popupX = x_;
  }
  const float thumbR = kThumbSize[static_cast<int>(size_)] * 0.5f;
  const float popupY = ty - popupH - 6.0f - thumbR;

  // Background (accent rounded rect), keyed by exact width + colors + scale.
  {
    const int pwQ = static_cast<int>(std::lround(popupW * 64.0f));
    const int acR = std::clamp(static_cast<int>(std::lround(accentR_ * 255.0f)), 0, 255);
    const int acG = std::clamp(static_cast<int>(std::lround(accentG_ * 255.0f)), 0, 255);
    const int acB = std::clamp(static_cast<int>(std::lround(accentB_ * 255.0f)), 0, 255);
    const uint64_t bkey = (static_cast<uint64_t>(pwQ) << 40) |
                          (static_cast<uint64_t>(acR) << 32) | (static_cast<uint64_t>(acG) << 24) |
                          (static_cast<uint64_t>(acB) << 16) |
                          (static_cast<uint64_t>(alphaQ) << 8) | static_cast<uint64_t>(dsQ & 0xFF);
    auto bit = s_bgs.find(bkey);
    if (bit == s_bgs.end() || !bit->second.surf) {
      if (s_bgs.size() >= 32) clear_bgs();
      const int bsw = std::max(1, static_cast<int>(std::ceil(popupW * ds)));
      const int bsh = std::max(1, static_cast<int>(std::ceil(popupH * ds)));
      PEntry e;
      e.surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bsw, bsh);
      e.sw = bsw;
      e.sh = bsh;
      cairo_t* tmp = cairo_create(e.surf);
      cairo_set_operator(tmp, CAIRO_OPERATOR_CLEAR);
      cairo_paint(tmp);
      cairo_set_operator(tmp, CAIRO_OPERATOR_OVER);
      cairo_scale(tmp, ds, ds);
      cairo_new_path(tmp);
      cairo_arc(tmp, popupR, popupR, popupR, M_PI, 1.5 * M_PI);
      cairo_arc(tmp, popupW - popupR, popupR, popupR, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(tmp, popupW - popupR, popupH - popupR, popupR, 0.0, 0.5 * M_PI);
      cairo_arc(tmp, popupR, popupH - popupR, popupR, 0.5 * M_PI, M_PI);
      cairo_close_path(tmp);
      cairo_set_source_rgba(tmp, accentR_, accentG_, accentB_, alpha * 0.96f);
      cairo_fill(tmp);
      cairo_destroy(tmp);
      s_bgs.emplace(bkey, e);
      bit = s_bgs.find(bkey);
    }
    if (bit != s_bgs.end() && bit->second.surf) {
      cairo_save(cr);
      cairo_translate(cr, popupX, popupY);
      cairo_scale(cr, 1.0 / ds, 1.0 / ds);
      cairo_set_source_surface(cr, bit->second.surf, 0, 0);
      cairo_rectangle(cr, 0, 0, bit->second.sw, bit->second.sh);
      cairo_fill(cr);
      cairo_restore(cr);
    }
  }

  // Arrow (shape-constant white triangle, tinted via mask so accent stays out of the key).
  {
    const int asw = std::max(1, static_cast<int>(std::ceil(10.0 * ds)));
    const int ash = std::max(1, static_cast<int>(std::ceil(7.0 * ds)));
    if (!s_arrow || s_arrowDsQ != dsQ || s_arrowSw != asw || s_arrowSh != ash) {
      if (s_arrow) cairo_surface_destroy(s_arrow);
      s_arrow = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, asw, ash);
      s_arrowSw = asw;
      s_arrowSh = ash;
      s_arrowDsQ = dsQ;
      cairo_t* tmp = cairo_create(s_arrow);
      cairo_set_operator(tmp, CAIRO_OPERATOR_CLEAR);
      cairo_paint(tmp);
      cairo_set_operator(tmp, CAIRO_OPERATOR_OVER);
      cairo_scale(tmp, ds, ds);
      cairo_move_to(tmp, 0, 0);
      cairo_line_to(tmp, 10.0, 0);
      cairo_line_to(tmp, 5.0, 7.0);
      cairo_close_path(tmp);
      cairo_set_source_rgba(tmp, 1, 1, 1, 1);
      cairo_fill(tmp);
      cairo_destroy(tmp);
    }
    cairo_save(cr);
    cairo_translate(cr, tc - 5.0, popupY + popupH);
    cairo_scale(cr, 1.0 / ds, 1.0 / ds);
    cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha * 0.96f);
    cairo_mask_surface(cr, s_arrow, 0, 0);
    cairo_restore(cr);
  }

  // Text: blit cached glyphs along the baseline (same positions as text_path layout).
  {
    const float textX = popupX + (popupW - totalAdv) * 0.5f;
    const float textY = popupY + popupH * 0.5f + 4.0f;
    float cur = textX;
    const char* p = valueLabel_.c_str();
    while (*p) {
      const uint32_t cp = utf8_next(p);
      if (cp == 0) break;
      const uint64_t gkey =
          (static_cast<uint64_t>(cp) << 24) | (static_cast<uint64_t>(dsQ & 0xFFFF) << 8) |
          static_cast<uint64_t>(alphaQ & 0xFF);
      auto git = s_glyphs.find(gkey);
      if (git == s_glyphs.end() || !git->second.surf) break;  // rendered above; be safe
      const PEntry& e = git->second;
      cairo_save(cr);
      cairo_translate(cr, cur + e.xb - 3.0, textY + e.yb - 3.0);
      cairo_scale(cr, 1.0 / ds, 1.0 / ds);
      cairo_set_source_surface(cr, e.surf, 0, 0);
      cairo_rectangle(cr, 0, 0, e.sw, e.sh);
      cairo_fill(cr);
      cairo_restore(cr);
      cur += e.adv;
    }
  }
}

inline void Slider::paint(cairo_t* cr) const {
  const_cast<Slider*>(this)->paint(cr, 0);
}

inline void Slider::paint(cairo_t* cr, uint64_t nowMs) {
  if (w_ <= 0 || h_ <= 0) return;

  const bool isHov = forceHover_ ? hovered_ : (hovered_ || dragging_);
  const bool isDrag = forcePressed_ ? dragging_ : dragging_;
  const float pv = animatedValue(nowMs);

  const int si = static_cast<int>(size_);
  const float thumbSize = kThumbSize[si];
  const float trackH = kTrackHeight[si];
  const float tl = trackLeft();
  const float tw = trackWidth();
  const float ty = y_ + h_ * 0.5f;
  const float alpha = enabled_ ? 1.0f : 0.38f;

  float tc;
  if (centered_) {
    const float n = (pv - lo_) / (hi_ - lo_);
    tc = tl + (n * 2.0f - 1.0f) * tw * 0.5f + tw * 0.5f;
  } else {
    tc = tl + ((pv - lo_) / (hi_ - lo_)) * tw;
  }

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  // Inactive track
  const float inactAlpha = 0.35f;
  cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, alpha * inactAlpha);
  cairo_set_line_width(cr, trackH);
  cairo_move_to(cr, tl, ty);
  cairo_line_to(cr, tl + tw, ty);
  cairo_stroke(cr);

  // Active track
  cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha);
  cairo_set_line_width(cr, trackH);
  cairo_move_to(cr, tl, ty);
  cairo_line_to(cr, tc, ty);
  cairo_stroke(cr);

  // Stop indicator at end of track
  if (enabled_) drawStopIndicator(cr, tl, tw, ty);

  // Thumb
  const float ts = isDrag ? thumbSize * 1.1f : thumbSize;
  const float thumbR = ts * 0.5f;

  // State layer on thumb
  stateLayer_.setColor(accentR_, accentG_, accentB_);
  stateLayer_.setRadius(thumbR + 8.0f);
  stateLayer_.setGeometry(tc - thumbR - 8.0f, ty - thumbR - 8.0f, ts + 16.0f, ts + 16.0f);
  stateLayer_.setHovered(isHov);
  stateLayer_.setPressed(isDrag);
  stateLayer_.tick(nowMs);
  stateLayer_.paint(cr, nowMs);

  // Thumb circle
  cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha);
  cairo_arc(cr, tc, ty, thumbR, 0, 2.0 * M_PI);
  cairo_fill(cr);

  // Value popup
  if (showValueLabel_ && isDrag) drawValuePopup(cr, tc, ty, tw, alpha);

  // Focus ring
  focusRing_.setFocused(focused_);
  focusRing_.setColor(accentR_, accentG_, accentB_);
  focusRing_.setRadius(thumbR + 6);
  focusRing_.paint(cr, tc - thumbR - 6, ty - thumbR - 6, (thumbR + 6) * 2, (thumbR + 6) * 2);
}

} // namespace m3
