#include "logind.h"
#include "log.h"

#include "config.h"

#ifdef _WIN32

#include <string.h>
#include <windows.h>

int cgk_logind_open(struct cgk_logind *logind)
{
    memset(logind, 0, sizeof(*logind));
    CGK_DAEMON_LOG("using Windows input desktop lock detection\n");
    return 0;
}

void cgk_logind_close(struct cgk_logind *logind)
{
    (void)logind;
}

int cgk_logind_locked(struct cgk_logind *logind, bool *locked)
{
    (void)logind;

    HDESK input_desktop = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
    if (input_desktop == NULL) {
        *locked = true;
        return 0;
    }

    BOOL can_switch = SwitchDesktop(input_desktop);
    CloseDesktop(input_desktop);

    *locked = can_switch == FALSE;
    return 0;
}

#else

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-login.h>
#include <unistd.h>

static void free_string_array(char **values)
{
    if (values == NULL) {
        return;
    }
    for (char **cursor = values; *cursor != NULL; cursor++) {
        free(*cursor);
    }
    free(values);
}

static bool session_matches(const char *session_id, uid_t uid, bool require_seat0)
{
    uid_t session_uid = 0;
    if (sd_session_get_uid(session_id, &session_uid) < 0 || session_uid != uid) {
        return false;
    }

    int active = sd_session_is_active(session_id);
    if (active <= 0) {
        return false;
    }

    char *type = NULL;
    if (sd_session_get_type(session_id, &type) < 0) {
        return false;
    }
    bool is_wayland = strcmp(type, "wayland") == 0;
    free(type);
    if (!is_wayland) {
        return false;
    }

    if (require_seat0) {
        char *seat = NULL;
        if (sd_session_get_seat(session_id, &seat) < 0) {
            return false;
        }
        bool is_seat0 = strcmp(seat, "seat0") == 0;
        free(seat);
        if (!is_seat0) {
            return false;
        }
    }

    return true;
}

static int find_session_id(char *session_id, size_t session_id_size)
{
    uid_t uid = getuid();
    const char *env_session = getenv("XDG_SESSION_ID");
    if (env_session != NULL && env_session[0] != '\0' && session_matches(env_session, uid, false)) {
        if (strlen(env_session) >= session_id_size) {
            return -ENAMETOOLONG;
        }
        strcpy(session_id, env_session);
        return 0;
    }

    char **sessions = NULL;
    int count = sd_uid_get_sessions(uid, true, &sessions);
    if (count < 0) {
        return count;
    }

    for (int i = 0; i < count; i++) {
        if (session_matches(sessions[i], uid, true)) {
            if (strlen(sessions[i]) >= session_id_size) {
                free_string_array(sessions);
                return -ENAMETOOLONG;
            }
            strcpy(session_id, sessions[i]);
            free_string_array(sessions);
            return 0;
        }
    }

    free_string_array(sessions);
    return -ENOENT;
}

static int get_session_path(sd_bus *bus, const char *session_id, char *path, size_t path_size)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(
        bus,
        "org.freedesktop.login1",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        "GetSession",
        &error,
        &reply,
        "s",
        session_id);
    if (r < 0) {
        CGK_DAEMON_LOG("logind GetSession failed: %s\n", error.message ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return r;
    }

    const char *object_path = NULL;
    r = sd_bus_message_read(reply, "o", &object_path);
    if (r < 0) {
        sd_bus_message_unref(reply);
        return r;
    }

    if (strlen(object_path) >= path_size) {
        sd_bus_message_unref(reply);
        return -ENAMETOOLONG;
    }

    strcpy(path, object_path);
    sd_bus_message_unref(reply);
    return 0;
}

int cgk_logind_open(struct cgk_logind *logind)
{
    memset(logind, 0, sizeof(*logind));

    int r = find_session_id(logind->session_id, sizeof(logind->session_id));
    if (r < 0) {
        CGK_DAEMON_LOG("cannot find active Wayland logind session\n");
        return CGK_EXIT_UNAVAILABLE;
    }

    r = sd_bus_open_system(&logind->bus);
    if (r < 0) {
        CGK_DAEMON_LOG("cannot connect to system bus: %s\n", strerror(-r));
        return CGK_EXIT_UNAVAILABLE;
    }

    r = get_session_path(logind->bus, logind->session_id, logind->session_path, sizeof(logind->session_path));
    if (r < 0) {
        cgk_logind_close(logind);
        return CGK_EXIT_UNAVAILABLE;
    }

    CGK_DAEMON_LOG("using logind session %s\n", logind->session_id);
    return 0;
}

void cgk_logind_close(struct cgk_logind *logind)
{
    if (logind->bus != NULL) {
        sd_bus_unref(logind->bus);
        logind->bus = NULL;
    }
    logind->session_id[0] = '\0';
    logind->session_path[0] = '\0';
}

int cgk_logind_locked(struct cgk_logind *logind, bool *locked)
{
    int value = 0;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_get_property_trivial(
        logind->bus,
        "org.freedesktop.login1",
        logind->session_path,
        "org.freedesktop.login1.Session",
        "LockedHint",
        &error,
        'b',
        &value);
    if (r < 0) {
        CGK_DAEMON_LOG("cannot read LockedHint: %s\n", error.message ? error.message : strerror(-r));
        sd_bus_error_free(&error);
        return CGK_EXIT_UNAVAILABLE;
    }

    *locked = value != 0;
    return 0;
}

#endif
