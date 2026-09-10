#pragma once

#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <mutex>
#include <thread>
#include <pthread.h>

#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>

#define MANGOWM_LOG(level, ...)     do {} while (false)
#define MANGOWM_TRACE(...)          do {} while (false)
#define MANGOWM_DEBUG(...)          do {} while (false)
#define MANGOWM_INFO(...)           do {} while (false)
#define MANGOWM_WARN(...)           do {} while (false)
#define MANGOWM_ERROR(...)          do {} while (false)
#define MANGOWM_CRITICAL(...)       do {} while (false)
#define MANGOWM_SCOPE(name)         do {} while (false)
#define MANGOWM_FN()                do {} while (false)
