#ifndef CAT_GATEKEEPER_CONTROL_H
#define CAT_GATEKEEPER_CONTROL_H

#include "config.h"

#include <stddef.h>

enum cgk_control_status_state {
    CGK_CONTROL_STATUS_COUNTING = 0,
    CGK_CONTROL_STATUS_LOCKED,
    CGK_CONTROL_STATUS_REMINDING,
};

enum cgk_control_request {
    CGK_CONTROL_NONE = 0,
    CGK_CONTROL_DISMISS,
    CGK_CONTROL_QUIT,
    CGK_CONTROL_TRIGGER,
};

struct cgk_control_status {
    enum cgk_control_status_state state;
    unsigned long long elapsed_seconds;
    unsigned long long remaining_seconds;
    unsigned long long interval_seconds;
};

#ifdef _WIN32

#include <windows.h>

struct cgk_control_server {
    HANDLE pipe;
    char pipe_name[PATH_MAX];
};

#else

struct cgk_control_server {
    int fd;
    char socket_path[PATH_MAX];
};

#endif

int cgk_control_server_open(struct cgk_control_server *server);
int cgk_control_poll(struct cgk_control_server *server, const struct cgk_control_status *status, enum cgk_control_request *request);
void cgk_control_server_close(struct cgk_control_server *server);

int cgk_control_client_send(const char *command, char *response, size_t response_size);

#endif
