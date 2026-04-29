#include "config.h"
#include "control.h"
#include "idle.h"
#include "logind.h"
#include "reminder.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t should_stop = 0;

enum daemon_state {
    STATE_COUNTING,
    STATE_LOCKED,
    STATE_REMINDING,
};

static void handle_signal(int signal_number)
{
    (void)signal_number;
    should_stop = 1;
}

static void install_signal_handlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
}

static bool desktop_contains_kde(const char *desktop)
{
    if (desktop == NULL) {
        return false;
    }
    return strstr(desktop, "KDE") != NULL || strstr(desktop, "kde") != NULL;
}

static int check_wayland_environment(void)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    const char *session_type = getenv("XDG_SESSION_TYPE");
    const char *current_desktop = getenv("XDG_CURRENT_DESKTOP");

    if (runtime_dir == NULL || runtime_dir[0] == '\0') {
        fprintf(stderr, "cat-gatekeeperd: XDG_RUNTIME_DIR is required\n");
        return CGK_EXIT_UNAVAILABLE;
    }
    if (wayland_display == NULL || wayland_display[0] == '\0') {
        fprintf(stderr, "cat-gatekeeperd: WAYLAND_DISPLAY is required\n");
        return CGK_EXIT_UNAVAILABLE;
    }
    if (session_type == NULL || strcmp(session_type, "wayland") != 0) {
        fprintf(stderr, "cat-gatekeeperd: XDG_SESSION_TYPE must be wayland\n");
        return CGK_EXIT_UNAVAILABLE;
    }
    if (!desktop_contains_kde(current_desktop)) {
        fprintf(stderr, "cat-gatekeeperd: XDG_CURRENT_DESKTOP must contain KDE\n");
        return CGK_EXIT_UNAVAILABLE;
    }

    return 0;
}

static void sleep_one_second(void)
{
    struct timespec delay = {
        .tv_sec = 1,
        .tv_nsec = 0,
    };
    nanosleep(&delay, NULL);
}

static enum cgk_control_status_state control_status_state(enum daemon_state state)
{
    switch (state) {
    case STATE_COUNTING:
        return CGK_CONTROL_STATUS_COUNTING;
    case STATE_LOCKED:
        return CGK_CONTROL_STATUS_LOCKED;
    case STATE_REMINDING:
        return CGK_CONTROL_STATUS_REMINDING;
    }
    return CGK_CONTROL_STATUS_COUNTING;
}

static struct cgk_control_status make_control_status(enum daemon_state state, unsigned long long count_started_ms, int interval_minutes)
{
    unsigned long long now_ms = cgk_monotonic_ms();
    unsigned long long interval_seconds = (unsigned long long)interval_minutes * 60ULL;
    unsigned long long interval_ms = interval_seconds * 1000ULL;

    struct cgk_control_status status = {
        .state = control_status_state(state),
        .elapsed_seconds = 0,
        .remaining_seconds = interval_seconds,
        .interval_seconds = interval_seconds,
    };

    if (state == STATE_COUNTING) {
        unsigned long long elapsed_ms = now_ms - count_started_ms;
        status.elapsed_seconds = elapsed_ms / 1000ULL;
        if (elapsed_ms >= interval_ms) {
            status.remaining_seconds = 0;
        } else {
            status.remaining_seconds = (interval_ms - elapsed_ms + 999ULL) / 1000ULL;
        }
    } else if (state == STATE_REMINDING) {
        status.remaining_seconds = 0;
    }

    return status;
}

int main(void)
{
    install_signal_handlers();

    int result = check_wayland_environment();
    if (result != 0) {
        return result;
    }

    struct cgk_config config;
    result = cgk_config_load(&config);
    if (result != 0) {
        return result;
    }

    result = cgk_validate_overlay();
    if (result != 0) {
        return result;
    }

    if (cgk_idle_enabled(config.idle_reset_seconds)) {
        fprintf(stderr, "cat-gatekeeperd: idle_reset_seconds is configured but idle detection is reserved for a later version\n");
    }

    struct cgk_logind logind;
    result = cgk_logind_open(&logind);
    if (result != 0) {
        return result;
    }

    struct cgk_control_server control;
    result = cgk_control_server_open(&control);
    if (result != 0) {
        cgk_logind_close(&logind);
        return result;
    }

    bool locked = false;
    result = cgk_logind_locked(&logind, &locked);
    if (result != 0) {
        cgk_control_server_close(&control);
        cgk_logind_close(&logind);
        return result;
    }

    enum daemon_state state = locked ? STATE_LOCKED : STATE_COUNTING;
    unsigned long long count_started_ms = cgk_monotonic_ms();
    struct cgk_reminder reminder = {
        .pid = -1,
    };

    fprintf(stderr, "cat-gatekeeperd: started; interval=%d minutes sleep=%d seconds screen=%d\n", config.interval_minutes, config.sleep_seconds, config.screen_index);
    if (state == STATE_LOCKED) {
        fprintf(stderr, "cat-gatekeeperd: session is locked; timer paused\n");
    }

    while (!should_stop) {
        bool current_locked = false;
        result = cgk_logind_locked(&logind, &current_locked);
        if (result != 0) {
            if (state == STATE_REMINDING) {
                cgk_stop_reminder(&reminder);
            }
            cgk_control_server_close(&control);
            cgk_logind_close(&logind);
            return result;
        }

        if (current_locked && state != STATE_LOCKED) {
            if (state == STATE_REMINDING) {
                fprintf(stderr, "cat-gatekeeperd: session locked; stopping overlay\n");
                cgk_stop_reminder(&reminder);
            } else {
                fprintf(stderr, "cat-gatekeeperd: session locked; timer reset\n");
            }
            state = STATE_LOCKED;
        } else if (!current_locked && state == STATE_LOCKED) {
            fprintf(stderr, "cat-gatekeeperd: session unlocked; timer restarted\n");
            state = STATE_COUNTING;
            count_started_ms = cgk_monotonic_ms();
        }

        enum cgk_control_request control_request = CGK_CONTROL_NONE;
        struct cgk_control_status control_status = make_control_status(state, count_started_ms, config.interval_minutes);
        cgk_control_poll(&control, &control_status, &control_request);
        if (control_request == CGK_CONTROL_DISMISS) {
            if (state == STATE_REMINDING) {
                fprintf(stderr, "cat-gatekeeperd: dismiss requested; stopping overlay\n");
                cgk_stop_reminder(&reminder);
                state = STATE_COUNTING;
                count_started_ms = cgk_monotonic_ms();
            } else {
                fprintf(stderr, "cat-gatekeeperd: dismiss requested; no reminder active\n");
            }
        } else if (control_request == CGK_CONTROL_QUIT) {
            fprintf(stderr, "cat-gatekeeperd: quit requested\n");
            if (state == STATE_REMINDING) {
                cgk_stop_reminder(&reminder);
            }
            should_stop = 1;
            break;
        } else if (control_request == CGK_CONTROL_TRIGGER) {
            if (state == STATE_COUNTING) {
                fprintf(stderr, "cat-gatekeeperd: manual trigger requested; starting overlay\n");
                if (cgk_start_reminder(config.sleep_seconds, config.screen_index, &reminder) == 0) {
                    state = STATE_REMINDING;
                }
                count_started_ms = cgk_monotonic_ms();
            } else if (state == STATE_REMINDING) {
                fprintf(stderr, "cat-gatekeeperd: manual trigger requested; reminder already active\n");
            } else {
                fprintf(stderr, "cat-gatekeeperd: manual trigger requested while locked; ignored\n");
            }
        }

        if (state == STATE_COUNTING) {
            unsigned long long elapsed_ms = cgk_monotonic_ms() - count_started_ms;
            unsigned long long interval_ms = (unsigned long long)config.interval_minutes * 60ULL * 1000ULL;
            if (elapsed_ms >= interval_ms) {
                if (cgk_start_reminder(config.sleep_seconds, config.screen_index, &reminder) == 0) {
                    state = STATE_REMINDING;
                } else {
                    count_started_ms = cgk_monotonic_ms();
                }
            }
        } else if (state == STATE_REMINDING) {
            if (cgk_poll_reminder(&reminder)) {
                state = STATE_COUNTING;
                count_started_ms = cgk_monotonic_ms();
            }
        }

        sleep_one_second();
    }

    if (state == STATE_REMINDING) {
        cgk_stop_reminder(&reminder);
    }
    cgk_control_server_close(&control);
    cgk_logind_close(&logind);
    return 0;
}
