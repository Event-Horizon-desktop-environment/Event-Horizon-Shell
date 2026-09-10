#pragma once

#include <cstdlib>
#include <cstring>

inline bool eh_verbose_enabled() noexcept {
    static const bool enabled = []() {
        const char* e = std::getenv("EH_VERBOSE_LOG");
        return e && *e && std::strcmp(e, "0") != 0;
    }();
    return enabled;
}

#define EH_VERBOSE_LOG(expr) do { if (eh_verbose_enabled()) { expr; } } while(false)
