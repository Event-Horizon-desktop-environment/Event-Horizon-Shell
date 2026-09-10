#include "desktop_shell/ui/dropdown/dropdown.hpp"

#include <algorithm>
#include <cmath>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include "m3/core/primitives/box.hpp"

namespace eh::ui {

static constexpr float kRadius = 8.0f;
static constexpr float kBorderW = 1.0f;
static constexpr float kChevronSize = 12.0f;

Dropdown::Dropdown() = default;

void Dropdown::setTriggerHovered(bool h) {
   
  forceTrigHover_ = true;
  trigHovered_ = h;
}

void Dropdown::setTriggerPressed(bool p) {
   
  forceTrigPress_ = true;
  trigPressed_ = p;
}

void Dropdown::paintTrigger(cairo_t* cr) const {
   
  if (w_ <= 0.0f || h_ <= 0.0f) return;

  const float alpha = enabled_ ? 1.0f : 0.4f;

  cairo_save(cr);

  auto rounded_rect = [](cairo_t* c, float rx, float ry, float rw, float rh, float r) {
    if (r > rw * 0.5f) r = rw * 0.5f;
    if (r > rh * 0.5f) r = rh * 0.5f;
    cairo_new_path(c);
    cairo_arc(c, rx + r, ry + r, r, M_PI, 1.5 * M_PI);
    cairo_arc(c, rx + rw - r, ry + r, r, 1.5 * M_PI, 2.0 * M_PI);
    cairo_arc(c, rx + rw - r, ry + rh - r, r, 0.0, 0.5 * M_PI);
    cairo_arc(c, rx + r, ry + rh - r, r, 0.5 * M_PI, M_PI);
    cairo_close_path(c);
  };

  const bool isHov = forceTrigHover_ ? trigHovered_ : trigHovered_;
  const bool isPress = forceTrigPress_ ? trigPressed_ : (trigPressed_ || open_);
  const float r = 8.0f;
  const float bw = 1.0f;

  float bg_r = surfaceR_, bg_g = surfaceG_, bg_b = surfaceB_;
  float border_r = outlineR_, border_g = outlineG_, border_b = outlineB_;

  if (!enabled_) {
    bg_r *= 0.6f; bg_g *= 0.6f; bg_b *= 0.6f;
    border_r *= 0.6f; border_g *= 0.6f; border_b *= 0.6f;
  } else if (isPress) {
    const float dk = 0.5f;
    bg_r = surfaceR_ * dk; bg_g = surfaceG_ * dk; bg_b = surfaceB_ * dk;
    border_r = accentR_; border_g = accentG_; border_b = accentB_;
  } else if (isHov) {
    const float lt = 1.15f;
    bg_r = std::min(surfaceR_ * lt, 1.0f); bg_g = std::min(surfaceG_ * lt, 1.0f); bg_b = std::min(surfaceB_ * lt, 1.0f);
    border_r = accentR_; border_g = accentG_; border_b = accentB_;
  }

  // Use m3::Box + glassy for the trigger background to get the consistent inner semi-glassy effect
  {
    m3::Box triggerBg;
    triggerBg.setColor(bg_r, bg_g, bg_b, 1.0f);
    triggerBg.setRadius(r);
    triggerBg.setGeometry(x_, y_, w_, h_);
    triggerBg.setGlassy(true);
    triggerBg.paint(cr);
  }

  rounded_rect(cr, x_, y_, w_, h_, r);
  cairo_set_source_rgba(cr, border_r, border_g, border_b, alpha);
  cairo_set_line_width(cr, bw);
  cairo_stroke(cr);

  const std::string& displayText = (sel_ < options_.size()) ? options_[sel_] : placeholder_;
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, fontSize_);

  float fg_r = textR_, fg_g = textG_, fg_b = textB_;
  if (sel_ >= options_.size()) {
    fg_r = textR_ * 0.7f; fg_g = textG_ * 0.7f; fg_b = textB_ * 0.7f;
  }
  cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, alpha);
  cairo_move_to(cr, x_ + padding_, y_ + h_ * 0.5f + fontSize_ * 0.35f);
  cairo_show_text(cr, displayText.c_str());

  const float cx = x_ + w_ - padding_ - 12.0f;
  const float cy = y_ + h_ * 0.5f;
  cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, alpha * 0.7f);
  cairo_set_line_width(cr, 1.5f);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, cx, cy - 3.0f);
  cairo_line_to(cr, cx + 6.0f, cy + 2.0f);
  cairo_line_to(cr, cx + 12.0f, cy - 3.0f);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void Dropdown::setOptions(std::vector<std::string> items) {
   
  options_ = std::move(items);
  if (sel_ >= options_.size()) sel_ = kNpos;
  hovered_ = kNpos;
}

void Dropdown::setSelectedIndex(std::size_t idx) {
   
  if (idx >= options_.size()) return;
  if (sel_ == idx) return;
  sel_ = idx;
  if (onSelect_ && idx < options_.size()) {
    onSelect_(idx, options_[idx]);
  }
}

void Dropdown::setPlaceholder(std::string_view text) {
   
  placeholder_ = text;
}

void Dropdown::setEnabled(bool enabled) {
   
  enabled_ = enabled;
  if (!enabled_) {
    open_ = false;
    trigHovered_ = false;
    trigPressed_ = false;
  }
}

void Dropdown::setGeometry(float x, float y, float w, float h) {
   
  x_ = x;
  y_ = y;
  w_ = w;
  h_ = h;
}

void Dropdown::setMaxVisible(std::size_t rows) {
   
  maxVisible_ = std::max<std::size_t>(rows, 1);
}

void Dropdown::setAccentColor(float r, float g, float b) {
   
  accentR_ = r; accentG_ = g; accentB_ = b;
}

void Dropdown::setSurfaceColor(float r, float g, float b) {
   
  surfaceR_ = r; surfaceG_ = g; surfaceB_ = b;
}

void Dropdown::setTextColor(float r, float g, float b) {
   
  textR_ = r; textG_ = g; textB_ = b;
}

void Dropdown::setOutlineColor(float r, float g, float b) {
   
  outlineR_ = r; outlineG_ = g; outlineB_ = b;
}

void Dropdown::setOnSelect(std::function<void(std::size_t, std::string_view)> cb) {
   
  onSelect_ = std::move(cb);
}

std::string_view Dropdown::selectedText() const noexcept {
  if (sel_ >= options_.size()) return {};
  return options_[sel_];
}

float Dropdown::menuHeight() const noexcept {
  const std::size_t count = std::min(options_.size(), maxVisible_);
  return static_cast<float>(count) * optH_ + padding_ * 2.0f;
}

bool Dropdown::containsPoint(float px, float py) const noexcept {
  return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
}

bool Dropdown::containsMenuPoint(float px, float py) const noexcept {
  if (!open_) return false;
  const float my = dropTop();
  const float mh = menuHeight();
  return px >= x_ && px < x_ + w_ && py >= my && py < my + mh;
}

float Dropdown::dropTop() const noexcept {
  return y_ + h_ + 4.0f;
}

std::size_t Dropdown::optionAt(float px, float py) const noexcept {
  if (!open_) return kNpos;
  const float my = dropTop() + padding_;
  if (px < x_ || px >= x_ + w_) return kNpos;
  const float relY = py - my;
  if (relY < 0.0f) return kNpos;
  const std::size_t idx = static_cast<std::size_t>(relY / optH_);
  if (idx >= options_.size()) return kNpos;
  return idx;
}

void Dropdown::close() {
   
  open_ = false;
  hovered_ = kNpos;
}

void Dropdown::toggle() {
   
  open_ = !open_;
  if (!open_) hovered_ = kNpos;
}

void Dropdown::selectIndex(std::size_t idx) {
   
  if (idx >= options_.size()) return;
  sel_ = idx;
  open_ = false;
  hovered_ = kNpos;
  if (onSelect_) onSelect_(idx, options_[idx]);
}

bool Dropdown::handlePointerEnter(float /*px*/, float /*py*/) {
   
  if (!enabled_) return false;
  trigHovered_ = true;
  return true;
}

bool Dropdown::handlePointerLeave() {
   
  trigHovered_ = false;
  trigPressed_ = false;
  return true;
}

bool Dropdown::handlePointerDown(float px, float py) {
   
  if (!enabled_) return false;
  if (containsPoint(px, py)) {
    trigPressed_ = true;
    return true;
  }
  if (open_ && containsMenuPoint(px, py)) {
    return true;
  }
  if (open_) {
    close();
    return true;
  }
  return false;
}

bool Dropdown::handlePointerMove(float px, float py) {
   
  if (!enabled_ || !open_) return false;
  hovered_ = optionAt(px, py);
  return hovered_ != kNpos || containsMenuPoint(px, py);
}

bool Dropdown::handlePointerUp(float px, float py) {
   
  if (!enabled_) return false;
  trigPressed_ = false;
  if (containsPoint(px, py)) {
    toggle();
    return true;
  }
  if (open_) {
    const std::size_t idx = optionAt(px, py);
    if (idx != kNpos) {
      selectIndex(idx);
    } else {
      close();
    }
    return true;
  }
  return false;
}

void Dropdown::paint(cairo_t* cr) const {
   
  if (w_ <= 0.0f || h_ <= 0.0f) return;

  const float alpha = enabled_ ? 1.0f : 0.4f;

  cairo_save(cr);

  auto rounded_rect = [](cairo_t* c, float rx, float ry, float rw, float rh, float r) {
    if (r > rw * 0.5f) r = rw * 0.5f;
    if (r > rh * 0.5f) r = rh * 0.5f;
    cairo_new_path(c);
    cairo_arc(c, rx + r, ry + r, r, M_PI, 1.5 * M_PI);
    cairo_arc(c, rx + rw - r, ry + r, r, 1.5 * M_PI, 2.0 * M_PI);
    cairo_arc(c, rx + rw - r, ry + rh - r, r, 0.0, 0.5 * M_PI);
    cairo_arc(c, rx + r, ry + rh - r, r, 0.5 * M_PI, M_PI);
    cairo_close_path(c);
  };

  // Trigger background.
  float bg_r = surfaceR_, bg_g = surfaceG_, bg_b = surfaceB_;
  float border_r = outlineR_, border_g = outlineG_, border_b = outlineB_;

  if (!enabled_) {
    bg_r *= 0.6f; bg_g *= 0.6f; bg_b *= 0.6f;
    border_r *= 0.6f; border_g *= 0.6f; border_b *= 0.6f;
  } else if (trigPressed_ || open_) {
    bg_r = accentR_; bg_g = accentG_; bg_b = accentB_; bg_r *= 0.12f; bg_g *= 0.12f; bg_b *= 0.12f;
    border_r = accentR_; border_g = accentG_; border_b = accentB_;
  } else if (trigHovered_) {
    bg_r = accentR_; bg_g = accentG_; bg_b = accentB_; bg_r *= 0.08f; bg_g *= 0.08f; bg_b *= 0.08f;
    border_r = accentR_; border_g = accentG_; border_b = accentB_;
  }

  // Use m3::Box + glassy for the trigger background to get the consistent inner semi-glassy effect
  {
    m3::Box triggerBg;
    triggerBg.setColor(bg_r, bg_g, bg_b, 1.0f);
    triggerBg.setRadius(kRadius);
    triggerBg.setGeometry(x_, y_, w_, h_);
    triggerBg.setGlassy(true);
    triggerBg.paint(cr);
  }

  rounded_rect(cr, x_, y_, w_, h_, kRadius);
  cairo_set_source_rgba(cr, border_r, border_g, border_b, alpha);
  cairo_set_line_width(cr, kBorderW);
  cairo_stroke(cr);

  // Trigger label.
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_from_string("Inter");
  pango_font_description_set_size(desc, fontSize_ * PANGO_SCALE);
  pango_layout_set_font_description(layout, desc);

  const std::string& displayText = (sel_ < options_.size()) ? options_[sel_] : placeholder_;
  pango_layout_set_text(layout, displayText.data(), static_cast<int>(displayText.size()));
  pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

  int th = 0;
  pango_layout_get_pixel_size(layout, nullptr, &th);

  const float lx = x_ + padding_;
  const float ly = y_ + std::round((h_ - static_cast<float>(th)) * 0.5f);

  float fg_r = textR_, fg_g = textG_, fg_b = textB_;
  if (sel_ >= options_.size()) {
    fg_r = textR_ * 0.7f; fg_g = textG_ * 0.7f; fg_b = textB_ * 0.7f;
  }

  cairo_move_to(cr, lx, ly);
  cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, alpha);
  pango_cairo_show_layout(cr, layout);

  pango_font_description_free(desc);

  // Chevron.
  const float cx = x_ + w_ - padding_ - kChevronSize;
  const float cy = y_ + h_ * 0.5f;
  const float ch = kChevronSize * 0.55f;
  const float cw2 = kChevronSize * 0.5f;

  cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, alpha);
  cairo_set_line_width(cr, 1.5f);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  if (open_) {
    cairo_move_to(cr, cx, cy + ch * 0.3f);
    cairo_line_to(cr, cx + cw2, cy - ch * 0.3f);
    cairo_line_to(cr, cx + kChevronSize, cy + ch * 0.3f);
  } else {
    cairo_move_to(cr, cx, cy - ch * 0.3f);
    cairo_line_to(cr, cx + cw2, cy + ch * 0.3f);
    cairo_line_to(cr, cx + kChevronSize, cy - ch * 0.3f);
  }
  cairo_stroke(cr);

  g_object_unref(layout);

  // Dropdown menu (when open).
  if (!open_) {
    cairo_restore(cr);
    return;
  }

  const float my = dropTop();
  const float mh = menuHeight();

  // Shadow
  for (int i = 3; i >= 0; --i) {
    const float s = 1.0f + static_cast<float>(i) * 2.0f;
    cairo_set_source_rgba(cr, 0.0f, 0.0f, 0.0f, 0.06f - static_cast<float>(i) * 0.015f);
    rounded_rect(cr, x_ + s, my + s, w_ - s * 2.0f, mh - s * 2.0f, kRadius);
    cairo_fill(cr);
  }

  // Menu background
  rounded_rect(cr, x_, my, w_, mh, kRadius);
  cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
  cairo_fill(cr);

  rounded_rect(cr, x_, my, w_, mh, kRadius);
  cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.5f);
  cairo_set_line_width(cr, kBorderW);
  cairo_stroke(cr);

  // Options
  const float optStartY = my + padding_;
  const std::size_t visibleCount = std::min(options_.size(), maxVisible_);

  for (std::size_t i = 0; i < visibleCount; ++i) {
    const float oy = optStartY + static_cast<float>(i) * optH_;
    const bool isHovered = (i == hovered_);
    const bool isSelected = (i == sel_);

    // Option background
    if (isHovered) {
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 0.12f);
      rounded_rect(cr, x_ + 4.0f, oy, w_ - 8.0f, optH_, 6.0f);
      cairo_fill(cr);
    }

    // Option text
    auto* ol = pango_cairo_create_layout(cr);
    auto* od = pango_font_description_from_string("Inter");
    pango_font_description_set_size(od, fontSize_ * PANGO_SCALE);
    pango_layout_set_font_description(ol, od);
    pango_layout_set_text(ol, options_[i].data(), static_cast<int>(options_[i].size()));
    pango_layout_set_ellipsize(ol, PANGO_ELLIPSIZE_END);

    int otw = 0, oth = 0;
    pango_layout_get_pixel_size(ol, &otw, &oth);

    const float olx = x_ + padding_;
    const float oly = oy + std::round((optH_ - static_cast<float>(oth)) * 0.5f);

    float ofg_r = textR_, ofg_g = textG_, ofg_b = textB_;
    if (isHovered) {
      ofg_r = accentR_; ofg_g = accentG_; ofg_b = accentB_;
    }

    cairo_move_to(cr, olx, oly);
    cairo_set_source_rgba(cr, ofg_r, ofg_g, ofg_b, alpha);
    pango_cairo_show_layout(cr, ol);

    pango_font_description_free(od);
    g_object_unref(ol);

    // Checkmark on selected
    if (isSelected) {
      const float ckx = x_ + w_ - padding_ - 14.0f;
      const float cky = oy + optH_ * 0.5f;
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha);
      cairo_set_line_width(cr, 2.0f);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      cairo_move_to(cr, ckx - 6.0f, cky);
      cairo_line_to(cr, ckx - 2.0f, cky + 4.0f);
      cairo_line_to(cr, ckx + 5.0f, cky - 4.0f);
      cairo_stroke(cr);
    }
  }

  cairo_restore(cr);
}

}
