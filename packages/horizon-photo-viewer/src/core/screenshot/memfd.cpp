#include "core/screenshot/memfd.hpp"
#include <sys/syscall.h>
#include <unistd.h>

namespace hpv::sc {

int memfd_create_compat(const char* name, unsigned int flags) {
#ifdef SYS_memfd_create
  return static_cast<int>(syscall(SYS_memfd_create, name, flags));
#else
  (void)name;
  (void)flags;
  return -1;
#endif
}

}
