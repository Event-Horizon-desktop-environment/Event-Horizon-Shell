#pragma once

#include <string>

namespace eh::shell::taskflip {

class Host;

bool is_taskflip_command(const std::string& payload);
void dispatch_taskflip_command(Host& host, const std::string& payload);

}
