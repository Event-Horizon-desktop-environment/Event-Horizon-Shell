#include "desktop_shell/switcher/taskflip_host.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "wl/surface/layer_surface.hpp"

#include <algorithm>
#include <cmath>

namespace eh::shell::taskflip {

namespace {

const zwlr_layer_surface_v1_listener kTaskflipLayerListener = {
    .configure = Host::onConfigure,
    .closed = nullptr,
};

}

Host::Host(DockApp& dock, HostConfig cfg) : dock_(dock), cfg_(cfg) {}

Host::~Host() { destroySurface(); }

void Host::syncFromToplevels() {
  std::vector<Entry> current;
  for (const auto& tl : dock_.toplevels.list()) {
    if (!tl.handle) continue;
    Entry e;
    e.handle = tl.handle;
    e.appId = tl.appId;
    e.title = tl.title;
    e.serial = tl.serial;
    current.push_back(std::move(e));
  }
  model_.rebuild(current);
}

void Host::open() {
  if (open_) return;
  syncFromToplevels();
  if (model_.size() == 0) return;
  model_.resetSelection();
  if (model_.size() > 1) model_.next();
  wl_output* out = nullptr;
  for (const auto& layer : dock_.dockLayers) {
    if (layer && layer->wlOut) {
      out = layer->wlOut;
      break;
    }
  }
  createSurface(out);
  open_ = true;
  paint();
}

void Host::close() {
  if (!open_) return;
  open_ = false;
  destroySurface();
}

void Host::toggle() {
  if (open_)
    close();
  else
    open();
}

void Host::next() {
  if (!open_) {
    open();
    return;
  }
  syncFromToplevels();
  model_.next();
  paint();
}

void Host::prev() {
  if (!open_) {
    open();
    return;
  }
  syncFromToplevels();
  model_.prev();
  paint();
}

void Host::confirm() {
  if (!open_) return;
  const Entry* sel = model_.selected();
  if (sel && sel->handle && dock_.seat) {
    zwlr_foreign_toplevel_handle_v1_activate(sel->handle, dock_.seat);
    if (dock_.display) wl_display_flush(dock_.display);
    model_.noteActivated(sel->handle);
  }
  close();
}

void Host::createSurface(wl_output* output) {
  if (!dock_.compositor || !dock_.layerShell || !output) return;
  const int cardW = cfg_.galleryStyle ? 700 : 900;
  const int cardH = cfg_.galleryStyle ? 380 : 120;
  output_ = output;
  surface_ = wl_compositor_create_surface(dock_.compositor);
  if (!surface_) return;
  layer_ = zwlr_layer_shell_v1_get_layer_surface(
      dock_.layerShell, surface_, output,
      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "taskflip");
  if (!layer_) {
    wl_surface_destroy(surface_);
    surface_ = nullptr;
    return;
  }
  zwlr_layer_surface_v1_set_size(layer_, (uint32_t)cardW, (uint32_t)cardH);
  zwlr_layer_surface_v1_set_anchor(
      layer_, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                   ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_exclusive_zone(layer_, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      layer_, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  zwlr_layer_surface_v1_add_listener(layer_, &kTaskflipLayerListener, this);
  wl_surface_commit(surface_);
  if (dock_.display) wl_display_flush(dock_.display);
}

void Host::destroySurface() {
  if (layer_) {
    zwlr_layer_surface_v1_destroy(layer_);
    layer_ = nullptr;
  }
  if (surface_) {
    wl_surface_destroy(surface_);
    surface_ = nullptr;
  }
  output_ = nullptr;
  configured_ = false;
  width_ = 0;
  height_ = 0;
}

void Host::onConfigure(void* data, zwlr_layer_surface_v1* layer, uint32_t serial,
                       uint32_t width, uint32_t height) {
  auto* self = static_cast<Host*>(data);
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  self->width_ = (int)width;
  self->height_ = (int)height;
  self->configured_ = true;
  self->paint();
}

void Host::paint() {
  if (!surface_ || !configured_ || width_ <= 0 || height_ <= 0) return;
  if (!dock_.shm) return;
  if (!shmBuf_.ensure(dock_.shm, "taskflip", width_, height_)) return;
  cairo_t* cr = shmBuf_.cairo();
  if (!cr) return;
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_restore(cr);
  if (cfg_.galleryStyle)
    paint_gallery(cr, model_.entries(), model_.selectedIndex(), (double)width_,
                  (double)height_, dock_.icons, style_);
  else
    paint_rail(cr, model_.entries(), model_.selectedIndex(), (double)width_,
               (double)height_, dock_.icons, style_);
  cairo_surface_flush(shmBuf_.cairo_surface());
  wl_surface_attach(surface_, shmBuf_.wl(), 0, 0);
  wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
  wl_surface_commit(surface_);
  if (dock_.display) wl_display_flush(dock_.display);
}

}
