#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace hpv::sc {

struct HdrData;

bool capture_output_drm(const char* drm_path, const char* connector_name, HdrData& out_hdr);

std::string find_drm_card_for_output(const char* connector_name);

}
