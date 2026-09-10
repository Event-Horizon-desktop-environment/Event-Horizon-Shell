#include "bootstrap/entry/app_bootstrap.hpp"

#include "bootstrap/session/unified_shell.hpp"
#include "desktop_shell/common/log/mangowm_logger.hpp"
#include "desktop_shell/common/bench/startup_trace.hpp"

int Application::run(int argc, char** argv) {
   
  MANGOWM_INFO("Application::run argc=%d", argc);
  EH_ST_TRACE(std::cerr << "application::run enter argc=" << argc);
  eh::app::UnifiedShell shell;
  const int r = shell.run(argc, argv);
  EH_ST_TRACE(std::cerr << "application::run exit code=" << r);
  MANGOWM_DEBUG("Application::run exit code=%d", r);
  return r;
}
