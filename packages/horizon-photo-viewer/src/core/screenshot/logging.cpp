#include "core/screenshot/logging.hpp"
#include <cstdio>
#include <cstdarg>
#include <ctime>

namespace hpv::sc {

void sc_log(const char* file, int line, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[64] = {};
    time_t now = time(nullptr);
    struct tm bt;
    localtime_r(&now, &bt);
    strftime(buf, sizeof(buf), "%H:%M:%S", &bt);
    fprintf(stderr, "%s ", buf);
    fprintf(stderr, "[SC] %s:%d ", file, line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void sc_logv(const char* file, int line, const char* fmt, va_list ap) {
    char buf[64] = {};
    time_t now = time(nullptr);
    struct tm bt;
    localtime_r(&now, &bt);
    strftime(buf, sizeof(buf), "%H:%M:%S", &bt);
    fprintf(stderr, "%s ", buf);
    fprintf(stderr, "[SC] %s:%d ", file, line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

} // namespace hpv::sc
