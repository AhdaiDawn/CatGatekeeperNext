#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

void cgk_log(const char *component, const char *format, ...)
{
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

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fflush(stderr);
}
