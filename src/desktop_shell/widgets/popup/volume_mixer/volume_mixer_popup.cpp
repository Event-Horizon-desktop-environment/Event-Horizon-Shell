#include "desktop_shell/widgets/popup/volume_mixer/volume_mixer_popup.hpp"

#include "../../../dock/core/dock_app.h"
#include "../../../shared/popup/session/session.hpp"
#include "services/audio/pipewire_service.hpp"

#include <algorithm>
#include <cmath>

namespace eh::shell::dock::popup::volume_mixer {
namespace {

void reset_ui() {
   
  ui() = MixerUiState{};
}

}

void dock_volume_mixer_popup_handle_click(DockApp& app, double x, double y, uint32_t) {
   
  auto& pw = eh::audio::PipeWireService::instance();
  const double W = static_cast<double>(kVolumeMixerPopupW);
  const double H = static_cast<double>(app.popupH);
  const double cw = content_w();
  const double cx = row_start_x();

  // Close if clicked outside card
  if (x < 0 || x >= W || y < 0 || y >= H) {
    popup_close(app);
    return;
  }

  // Header pills hit test.
  const double pillY = 10.0;
  const double outPillW = 28.0 + 8.0 + 100.0;
  if (y >= pillY && y < pillY + kPillH) {
    const bool isViewOutput = ui().viewOutput;

    // Output pill hit
    if (x >= cx && x < cx + outPillW) {
      if (!isViewOutput) {
        ui().viewOutput = true;
        reset_ui();
      }
      return;
    }

    // Input pill hit (if output view and has sources)
    MixerSnapshot snap = take_snapshot();
    if (isViewOutput && !snap.sources.empty()) {
      const double inPillW = 28.0 + 8.0 + 80.0;
      const double inPillX = cx + cw - inPillW;
      if (x >= inPillX && x < inPillX + inPillW) {
        ui().viewOutput = false;
        reset_ui();
        return;
      }
    }
    // In input view, the output pill label says "Input" — clicking it goes back to output
    if (!isViewOutput) {
      ui().viewOutput = true;
      reset_ui();
      return;
    }
  }

  const MixerSnapshot snap = take_snapshot();
  const auto& devices = ui().viewOutput ? snap.sinks : snap.sources;
  const auto& streams = ui().viewOutput ? snap.streams : std::vector<StreamInfo>();

  const double headerBottom = 48.0;

  // Device row hits.
  const int devCount = static_cast<int>(devices.size());
  const double devRowsY = headerBottom + 4.0;
  for (int i = 0; i < devCount; ++i) {
    const auto& dev = devices[static_cast<size_t>(i)];
    const double rowY = devRowsY + static_cast<double>(i) * kRowH;

    // Mute button hit
    const double muteX = cx + kRadioR * 2.0 + 8.0 + kDevIconSz + 8.0 + kNameMaxW + 8.0;
    if (x >= muteX && x < muteX + kMuteBtnSz && y >= rowY && y < rowY + kRowH) {
      pw.set_node_mute(dev.node_id, !dev.muted);
      return;
    }

    // Slider hit
    const auto sg = device_slider_geom(i, devCount, headerBottom);
    const double hitPadV = 15.0;
    const double hitPadH = 6.0;
    if (x >= sg.sx - hitPadH && x <= sg.sx + sg.sw + hitPadH &&
        y >= sg.sy - hitPadV && y <= sg.sy + kSliderTrackH + hitPadV) {
      const double t = std::clamp((x - sg.sx) / std::max(1.0, sg.sw), 0.0, 1.0);
      pw.set_node_volume(dev.node_id, t);
      return;
    }

    // Radio / row click — set as default
    if (x >= cx && x <= cx + cw && y >= rowY && y < rowY + kRowH) {
      if (ui().viewOutput)
        pw.set_default_sink(dev.node_id);
      else
        pw.set_default_source(dev.node_id);
      return;
    }
  }

  // Stream row hits.
  const double devicesH = static_cast<double>(devCount) * kRowH;
  const double appsDividerY = devRowsY + devicesH + 16.0;
  const double appsHeaderH = 28.0;
  const double streamsTop = appsDividerY + appsHeaderH;
  const int streamCount = static_cast<int>(streams.size());

  for (int i = 0; i < streamCount; ++i) {
    const auto& st = streams[static_cast<size_t>(i)];
    const double rowY = streamsTop + static_cast<double>(i) * kRowH;

    // Mute button hit
    const double muteX = cx + kAppIconSz + 8.0 + kAppNameMaxW + 4.0;
    if (x >= muteX && x < muteX + kMuteBtnSz && y >= rowY && y < rowY + kRowH) {
      pw.set_node_mute(st.node_id, !st.muted);
      return;
    }

    // Slider hit
    const auto sg = stream_slider_geom(i, headerBottom, 4.0 + devicesH + 16.0 + 1.0 + appsHeaderH);
    const double hitPadV = 15.0;
    const double hitPadH = 6.0;
    if (x >= sg.sx - hitPadH && x <= sg.sx + sg.sw + hitPadH &&
        y >= sg.sy - hitPadV && y <= sg.sy + kSliderTrackH + hitPadV) {
      const double t = std::clamp((x - sg.sx) / std::max(1.0, sg.sw), 0.0, 1.0);
      pw.set_node_volume(st.node_id, t);
      return;
    }

    // Device picker button hit
    const double pickerX = row_end_x() - kPad - kPickerBtnW - 4.0 - kEqBtnSz - 4.0 - kCloseBtnSz;
    const double pickerY = rowY + (kRowH - kPickerBtnH) * 0.5;
    if (x >= pickerX && x < pickerX + kPickerBtnW && y >= pickerY && y < pickerY + kPickerBtnH) {
      auto& appState = ui().apps[st.node_id];
      appState.routerExpanded = !appState.routerExpanded;
      if (appState.routerExpanded) appState.eqExpanded = false;
      return;
    }

    // EQ button hit
    const double eqBtnX = pickerX + kPickerBtnW + 4.0;
    const double eqBtnY = rowY + (kRowH - kEqBtnSz) * 0.5;
    if (x >= eqBtnX && x < eqBtnX + kEqBtnSz && y >= eqBtnY && y < eqBtnY + kEqBtnSz) {
      auto& appState = ui().apps[st.node_id];
      appState.eqExpanded = !appState.eqExpanded;
      if (appState.eqExpanded) appState.routerExpanded = false;
      return;
    }

    // Close button hit (visible when EQ or router open)
    auto& appState = ui().apps[st.node_id];
    if (appState.eqExpanded || appState.routerExpanded) {
      const double closeX = eqBtnX + kEqBtnSz + 4.0;
      const double closeY = eqBtnY;
      if (x >= closeX && x < closeX + kCloseBtnSz && y >= closeY && y < closeY + kCloseBtnSz) {
        appState.eqExpanded = false;
        appState.routerExpanded = false;
        return;
      }
    }

    // EQ panel preset hit
    if (appState.eqExpanded) {
      const double eqY = rowY + kRowH;
      const double eqX = cx;
      const double eqW = cw;
      const double eqHeaderY = eqY + kEqPanelPad;

      const double presetX = eqX + eqW - kEqPanelPad - kEqPresetBtnW;
      if (x >= presetX && x < presetX + kEqPresetBtnW && y >= eqHeaderY && y < eqHeaderY + kEqPresetBtnH) {
        static const int kPresetValues[6][10] = {
          {0,0,0,0,0,0,0,0,0,0},
          {6,5,3,1,0,0,0,0,0,0},
          {0,0,0,0,0,1,2,3,4,5},
          {5,3,1,0,-2,-2,0,1,3,5},
          {-2,0,2,4,4,4,2,0,-1,-2},
          {4,3,2,1,0,0,1,2,3,3},
        };
        int nextPreset = (appState.eqPresetIdx + 1) % 6;
        appState.eqPresetIdx = nextPreset;
        for (int b = 0; b < kEqBandCount; ++b)
          appState.eqBands[static_cast<size_t>(b)] = kPresetValues[nextPreset][b];
        appState.eqEnabled = true;
        return;
      }
    }

    // Router panel sink list hits
    if (appState.routerExpanded) {
      const MixerSnapshot rs = take_snapshot();
      const double routerY = rowY + kRowH;
      const double sinkRowH = 36.0;
      const int sinkCount = static_cast<int>(rs.sinks.size());
      const int sinkIdx = static_cast<int>((y - routerY - 8.0) / sinkRowH);
      if (sinkIdx >= 0 && sinkIdx < sinkCount) {
        pw.route_playback_stream_to_sink(st.node_id, rs.sinks[static_cast<size_t>(sinkIdx)].node_id);
        appState.routerExpanded = false;
        return;
      }
    }
  }
}

}
