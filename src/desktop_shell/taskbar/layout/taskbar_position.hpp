#pragma once

struct wl_output;
struct TaskbarApp;
struct TaskbarOutputLayer;

namespace eh::shell::taskbar {

struct TaskbarPopupPositionOutput {
  wl_output* wl_out = nullptr;
  int margin_left = 0;
  int margin_bottom = 0;
  int clearance = 0;
};

[[nodiscard]] TaskbarOutputLayer* taskbar_ref_layer(TaskbarApp& app);

[[nodiscard]] int taskbar_layer_w(TaskbarApp& app);

[[nodiscard]] TaskbarPopupPositionOutput taskbar_compute_popup_position(TaskbarApp& app, int anchorX, int popupW, int popupH);

}
