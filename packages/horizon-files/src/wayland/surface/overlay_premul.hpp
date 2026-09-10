#pragma once

namespace eh::wayland {

struct SolidOverlayRectPremul {
  float x0_ndc = 0.f;
  float y0_ndc = 0.f;
  float x1_ndc = 0.f;
  float y1_ndc = 0.f;
  float pr = 0.f;
  float pg = 0.f;
  float pb = 0.f;
  float pa = 0.f;
};

struct SolidOverlayDiscPremul {
  float cx_ndc = 0.f;
  float cy_ndc = 0.f;
  float rx_ndc = 0.f;
  float ry_ndc = 0.f;
  float pr = 0.f;
  float pg = 0.f;
  float pb = 0.f;
  float pa = 0.f;
};

}
