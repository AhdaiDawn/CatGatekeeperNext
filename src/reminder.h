#ifndef CAT_GATEKEEPER_REMINDER_H
#define CAT_GATEKEEPER_REMINDER_H

#include "config.h"

#include <stdbool.h>

#ifdef _WIN32

#include <windows.h>

struct cgk_reminder {
    HANDLE process;
    unsigned long long deadline_ms;
    unsigned long long kill_deadline_ms;
    bool term_sent;
    DWORD pid;
};

#else

#include <sys/types.h>

struct cgk_reminder {
    pid_t pid;
    unsigned long long deadline_ms;
    unsigned long long kill_deadline_ms;
    bool term_sent;
};

#endif

unsigned long long cgk_monotonic_ms(void);
int cgk_validate_overlay(void);
int cgk_start_reminder(int sleep_seconds, int screen_index, struct cgk_reminder *reminder);
bool cgk_poll_reminder(struct cgk_reminder *reminder);
void cgk_stop_reminder(struct cgk_reminder *reminder);

#endif
