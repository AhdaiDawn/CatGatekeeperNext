#ifndef CAT_GATEKEEPER_REMINDER_H
#define CAT_GATEKEEPER_REMINDER_H

#include "config.h"

#include <stdbool.h>
#include <sys/types.h>

struct cgk_reminder {
    pid_t pid;
    unsigned long long deadline_ms;
    unsigned long long kill_deadline_ms;
    bool term_sent;
};

unsigned long long cgk_monotonic_ms(void);
int cgk_validate_overlay(void);
int cgk_start_reminder(int sleep_seconds, struct cgk_reminder *reminder);
bool cgk_poll_reminder(struct cgk_reminder *reminder);
void cgk_stop_reminder(struct cgk_reminder *reminder);

#endif
