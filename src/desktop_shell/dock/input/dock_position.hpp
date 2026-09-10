#pragma once

struct DockApp;
struct DockOutputLayer;

[[nodiscard]] int dock_compositor_margin_bottom_px(const DockApp& app);

[[nodiscard]] DockOutputLayer* dock_popup_margin_reference_layer(DockApp& app);

[[nodiscard]] bool dock_popup_compute_layer_margins(DockApp& app, DockOutputLayer* L, int anchorLocalX,
                                                    int* marginLeft, int* marginBottom);

void dock_popup_sync_layer_margins_if_open(DockApp& app);
