#include "log.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

void cgk_log(const char *component, const char *format, ...)
{
#ifdef _WIN32
    SYSTEMTIME local_time;
    GetLocalTime(&local_time);
    fprintf(
        stderr,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u %s: ",
        (unsigned)local_time.wYear,
        (unsigned)local_time.wMonth,
        (unsigned)local_time.wDay,
        (unsigned)local_time.wHour,
        (unsigned)local_time.wMinute,
        (unsigned)local_time.wSecond,
        (unsigned)local_time.wMilliseconds,
        component);
#else
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        now.tv_sec = time(NULL);
        now.tv_nsec = 0;
    }

    struct tm local_time;
    char timestamp[32] = "0000-00-00 00:00:00";
    if (localtime_r(&now.tv_sec, &local_time) != NULL) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);
    }

    fprintf(stderr, "%s.%03ld %s: ", timestamp, now.tv_nsec / 1000000L, component);
#endif

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fflush(stderr);
}
