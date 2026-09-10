#pragma once

namespace eh::app {

namespace detail {
class UnifiedShellSession;
}

class UnifiedShell {
  friend class detail::UnifiedShellSession;

public:
  int run(int argc, char** argv);
};

}
