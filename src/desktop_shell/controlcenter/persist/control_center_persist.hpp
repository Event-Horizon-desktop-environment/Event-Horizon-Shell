#pragma once

#include <string>
#include <utility>

namespace eh::shell::control_center {

void cc_save_sink(const std::string& sink);
void cc_save_source(const std::string& source);
std::pair<std::string, std::string> cc_load_audio_prefs();

}
