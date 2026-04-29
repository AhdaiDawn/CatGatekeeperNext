#include "reminder.h"
#include "log.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

unsigned long long cgk_monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000ULL + (unsigned long long)now.tv_nsec / 1000000ULL;
}

static int resolve_overlay_path(char *path, size_t path_size)
{
    char self_path[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (length < 0) {
        CGK_DAEMON_LOG("cannot resolve daemon path: %s\n", strerror(errno));
        return CGK_EXIT_SOFTWARE;
    }
    self_path[length] = '\0';

    char *last_slash = strrchr(self_path, '/');
    if (last_slash == NULL) {
        CGK_DAEMON_LOG("daemon path has no executable directory: %s\n", self_path);
        return CGK_EXIT_SOFTWARE;
    }

    int written = 0;
    if (last_slash == self_path) {
        written = snprintf(path, path_size, "/cat-gatekeeper-overlay");
    } else {
        *last_slash = '\0';
        written = snprintf(path, path_size, "%s/cat-gatekeeper-overlay", self_path);
    }
    if (written <= 0 || (size_t)written >= path_size) {
        CGK_DAEMON_LOG("overlay path is too long\n");
        return CGK_EXIT_SOFTWARE;
    }
    return 0;
}

int cgk_validate_overlay(void)
{
    char overlay_path[PATH_MAX];
    int result = resolve_overlay_path(overlay_path, sizeof(overlay_path));
    if (result != 0) {
        return result;
    }

    struct stat st;
    if (stat(overlay_path, &st) != 0) {
        CGK_DAEMON_LOG("overlay executable does not exist next to daemon: %s\n", overlay_path);
        return CGK_EXIT_NO_INPUT;
    }
    if (!S_ISREG(st.st_mode) || access(overlay_path, X_OK) != 0) {
        CGK_DAEMON_LOG("overlay executable is not runnable: %s\n", overlay_path);
        return CGK_EXIT_NO_INPUT;
    }
    return 0;
}

int cgk_start_reminder(int sleep_seconds_value, int screen_index_value, struct cgk_reminder *reminder)
{
    char sleep_seconds[32];
    snprintf(sleep_seconds, sizeof(sleep_seconds), "%d", sleep_seconds_value);
    char screen_index[32];
    snprintf(screen_index, sizeof(screen_index), "%d", screen_index_value);

    char overlay_path[PATH_MAX];
    int result = resolve_overlay_path(overlay_path, sizeof(overlay_path));
    if (result != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        CGK_DAEMON_LOG("cannot fork overlay: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execl(
            overlay_path,
            overlay_path,
            "--sleep-seconds",
            sleep_seconds,
            "--screen",
            screen_index,
            (char *)NULL);
        CGK_DAEMON_LOG("cannot exec overlay %s: %s\n", overlay_path, strerror(errno));
        _exit(127);
    }

    memset(reminder, 0, sizeof(*reminder));
    reminder->pid = pid;
    reminder->deadline_ms = cgk_monotonic_ms() + (unsigned long long)(sleep_seconds_value + 30) * 1000ULL;
    reminder->kill_deadline_ms = 0;
    reminder->term_sent = false;

    CGK_DAEMON_LOG("started overlay pid %ld on screen %d\n", (long)pid, screen_index_value);
    return 0;
}

static bool reap_if_finished(struct cgk_reminder *reminder)
{
    if (reminder->pid <= 0) {
        return true;
    }

    int status = 0;
    pid_t result = waitpid(reminder->pid, &status, WNOHANG);
    if (result == 0) {
        return false;
    }
    if (result < 0) {
        if (errno != ECHILD) {
            CGK_DAEMON_LOG("waitpid failed: %s\n", strerror(errno));
        }
        reminder->pid = -1;
        return true;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) {
            CGK_DAEMON_LOG("overlay finished\n");
        } else {
            CGK_DAEMON_LOG("overlay exited with code %d\n", code);
        }
    } else if (WIFSIGNALED(status)) {
        CGK_DAEMON_LOG("overlay terminated by signal %d\n", WTERMSIG(status));
    }

    reminder->pid = -1;
    return true;
}

bool cgk_poll_reminder(struct cgk_reminder *reminder)
{
    if (reap_if_finished(reminder)) {
        return true;
    }

    unsigned long long now = cgk_monotonic_ms();
    if (!reminder->term_sent && now >= reminder->deadline_ms) {
        CGK_DAEMON_LOG("overlay timed out; sending SIGTERM\n");
        kill(reminder->pid, SIGTERM);
        reminder->term_sent = true;
        reminder->kill_deadline_ms = now + 2000ULL;
        return false;
    }

    if (reminder->term_sent && now >= reminder->kill_deadline_ms) {
        CGK_DAEMON_LOG("overlay did not exit after SIGTERM; sending SIGKILL\n");
        kill(reminder->pid, SIGKILL);
        reap_if_finished(reminder);
        return true;
    }

    return false;
}

void cgk_stop_reminder(struct cgk_reminder *reminder)
{
    if (reminder->pid <= 0) {
        return;
    }

    kill(reminder->pid, SIGTERM);
    unsigned long long deadline = cgk_monotonic_ms() + 2000ULL;
    while (cgk_monotonic_ms() < deadline) {
        if (reap_if_finished(reminder)) {
            return;
        }
        struct timespec wait_time = {
            .tv_sec = 0,
            .tv_nsec = 50000000L,
        };
        nanosleep(&wait_time, NULL);
    }

    kill(reminder->pid, SIGKILL);
    while (!reap_if_finished(reminder)) {
        struct timespec wait_time = {
            .tv_sec = 0,
            .tv_nsec = 50000000L,
        };
        nanosleep(&wait_time, NULL);
    }
}
