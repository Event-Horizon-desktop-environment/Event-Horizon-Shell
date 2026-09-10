#include "core/screenshot/color_info.hpp"
#include "core/screenshot/logging.hpp"

#include "color-management-v1-client-protocol.h"

#include <cstring>
#include <cstdio>

namespace hpv::sc {

OutputColorInfo query_output_color_info(wl_display* display, wp_color_manager_v1* mgr, wl_output* output)
{
  SC_LOG("query_output_color_info display=%p mgr=%p output=%p", (void*)display, (void*)mgr, (void*)output);
  OutputColorInfo result{};
  if (!display || !mgr || !output) { SC_LOG("query_output_color_info: invalid args"); return result; }

  struct Ctx { bool ready = false; bool failed = false; };
  auto* cm_out = wp_color_manager_v1_get_output(mgr, output);
  if (!cm_out) return result;

  auto* desc = wp_color_management_output_v1_get_image_description(cm_out);
  if (!desc) { wp_color_management_output_v1_destroy(cm_out); return result; }

  Ctx ctx{};
  auto desc_listener = wp_image_description_v1_listener{
    .failed = [](void* d, wp_image_description_v1*, uint32_t, const char*) {
      static_cast<Ctx*>(d)->failed = true;
    },
    .ready = [](void* d, wp_image_description_v1*, uint32_t) {
      static_cast<Ctx*>(d)->ready = true;
    },
    .ready2 = [](void* d, wp_image_description_v1*, uint32_t, uint32_t) {
      static_cast<Ctx*>(d)->ready = true;
    },
  };
  wp_image_description_v1_add_listener(desc, &desc_listener, &ctx);
  wl_display_flush(display);

  constexpr int kMax = 65536;
  for (int i = 0; i < kMax; ++i) {
    if (ctx.ready || ctx.failed) break;
    if (wl_display_dispatch(display) < 0) break;
  }

  if (!ctx.ready || ctx.failed) {
    wp_image_description_v1_destroy(desc);
    wp_color_management_output_v1_destroy(cm_out);
    return result;
  }

  struct InfoCtx { bool done = false; bool is_hdr = false; bool is_10bit = false; uint32_t max_lum = 0; };
  InfoCtx info{};
  auto* inf = wp_image_description_v1_get_information(desc);
  if (!inf) {
    wp_image_description_v1_destroy(desc);
    wp_color_management_output_v1_destroy(cm_out);
    return result;
  }

  auto info_listener = wp_image_description_info_v1_listener{
    .done = [](void* d, wp_image_description_info_v1*) { static_cast<InfoCtx*>(d)->done = true; },
    .icc_file = [](void*, wp_image_description_info_v1*, int32_t, uint32_t) {},
    .primaries = [](void*, wp_image_description_info_v1*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {},
    .primaries_named = [](void*, wp_image_description_info_v1*, uint32_t) {},
    .tf_power = [](void*, wp_image_description_info_v1*, uint32_t) {},
    .tf_named = [](void* d, wp_image_description_info_v1*, uint32_t tf) {
      auto* inf = static_cast<InfoCtx*>(d);
      if (tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ ||
          tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG) {
        inf->is_hdr = true;
      }
      if (tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ) {
        inf->is_10bit = true;
      }
    },
    .luminances = [](void* d, wp_image_description_info_v1*, uint32_t, uint32_t max_lum, uint32_t) {
      static_cast<InfoCtx*>(d)->max_lum = max_lum;
    },
    .target_primaries = [](void*, wp_image_description_info_v1*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {},
    .target_luminance = [](void*, wp_image_description_info_v1*, uint32_t, uint32_t) {},
    .target_max_cll = [](void*, wp_image_description_info_v1*, uint32_t) {},
    .target_max_fall = [](void*, wp_image_description_info_v1*, uint32_t) {},
  };
  wp_image_description_info_v1_add_listener(inf, &info_listener, &info);

  for (int i = 0; i < kMax; ++i) {
    if (info.done) break;
    wl_display_dispatch(display);
  }

  result.is_hdr = info.is_hdr;
  result.is_10bit = info.is_10bit;
  result.max_lum = info.is_hdr ? info.max_lum : 0;

  wp_image_description_info_v1_destroy(inf);
  wp_image_description_v1_destroy(desc);
  wp_color_management_output_v1_destroy(cm_out);

  SC_LOG("query_output_color_info: is_hdr=%d is_10bit=%d max_lum=%u", result.is_hdr, result.is_10bit, result.max_lum);
  return result;
}

}
