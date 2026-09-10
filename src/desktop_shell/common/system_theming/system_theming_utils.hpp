#pragma once

#include <string>
#include <vector>

namespace eh::theming::detail {

std::vector<std::string> icon_base_dirs();

std::string run_cmd_capture(const std::string& cmd);

std::string read_ini_kv(const std::string& path, const std::string& wantSection, const std::string& wantKey);

void update_ini_key(const std::string& path, const std::string& section, const std::string& key,
                    const std::string& value);

} // namespace eh::theming::detail
