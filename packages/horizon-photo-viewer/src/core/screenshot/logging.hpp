#pragma once

#include <cstdarg>

namespace hpv::sc {

void sc_log(const char* file, int line, const char* fmt, ...);
void sc_logv(const char* file, int line, const char* fmt, va_list ap);

} // namespace hpv::sc

#define SC_LOG(...) ::hpv::sc::sc_log(__FILE__, __LINE__, __VA_ARGS__)
