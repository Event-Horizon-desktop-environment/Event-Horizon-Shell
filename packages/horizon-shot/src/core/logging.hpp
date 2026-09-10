#pragma once

#include <cstdarg>

namespace hs::core {

void init_logging();
void hs_log(const char* file, int line, const char* fmt, ...);
void hs_logv(const char* file, int line, const char* fmt, va_list ap);

} // namespace hs::core

#define HS_LOG(...) ::hs::core::hs_log(__FILE__, __LINE__, __VA_ARGS__)
