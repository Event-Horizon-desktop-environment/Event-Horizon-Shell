#pragma once

inline bool eh_verbose_enabled() noexcept { return false; }

#define EH_VERBOSE_LOG(expr) do {} while(false)
