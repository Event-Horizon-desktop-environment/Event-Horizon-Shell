#include "desktop_shell/switcher/taskflip_ipc.hpp"
#include "desktop_shell/switcher/taskflip_host.hpp"

namespace eh::shell::taskflip {

bool is_taskflip_command(const std::string& payload) {
  return payload == "taskflip.toggle" || payload == "taskflip.open" ||
         payload == "taskflip.close" || payload == "taskflip.next" ||
         payload == "taskflip.prev" || payload == "taskflip.confirm" ||
         payload == "taskflip.cancel";
}

void dispatch_taskflip_command(Host& host, const std::string& payload) {
  if (payload == "taskflip.toggle") {
    host.toggle();
  } else if (payload == "taskflip.open") {
    if (!host.is_open()) host.open();
  } else if (payload == "taskflip.close" || payload == "taskflip.cancel") {
    host.close();
  } else if (payload == "taskflip.next") {
    host.next();
  } else if (payload == "taskflip.prev") {
    host.prev();
  } else if (payload == "taskflip.confirm") {
    host.confirm();
  }
}

}
