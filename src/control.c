#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "control.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

static int make_control_paths(char *dir_path, size_t dir_size, char *socket_path, size_t socket_size)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir == NULL || runtime_dir[0] == '\0') {
        return -1;
    }

    int written = snprintf(dir_path, dir_size, "%s/cat-gatekeeper", runtime_dir);
    if (written <= 0 || (size_t)written >= dir_size) {
        return -1;
    }

    written = snprintf(socket_path, socket_size, "%s/control.sock", dir_path);
    return written > 0 && (size_t)written < socket_size ? 0 : -1;
}

static int fill_unix_addr(const char *socket_path, struct sockaddr_un *addr, socklen_t *addr_len)
{
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(addr->sun_path)) {
        return -1;
    }
    strcpy(addr->sun_path, socket_path);
    *addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(addr->sun_path) + 1);
    return 0;
}

static int set_close_on_exec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static bool write_all(int fd, const char *text)
{
    size_t remaining = strlen(text);
    const char *cursor = text;
    while (remaining > 0) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (written == 0) {
            return false;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
    return true;
}

static int ensure_runtime_dir(const char *dir_path)
{
    if (mkdir(dir_path, 0700) != 0 && errno != EEXIST) {
        CGK_DAEMON_LOG("cannot create %s: %s\n", dir_path, strerror(errno));
        return CGK_EXIT_SOFTWARE;
    }

    struct stat st;
    if (stat(dir_path, &st) != 0) {
        CGK_DAEMON_LOG("cannot inspect %s: %s\n", dir_path, strerror(errno));
        return CGK_EXIT_SOFTWARE;
    }
    if (!S_ISDIR(st.st_mode)) {
        CGK_DAEMON_LOG("control path is not a directory: %s\n", dir_path);
        return CGK_EXIT_SOFTWARE;
    }
    if (st.st_uid != getuid()) {
        CGK_DAEMON_LOG("control directory is not owned by current user: %s\n", dir_path);
        return CGK_EXIT_SOFTWARE;
    }
    if ((st.st_mode & 077) != 0 && chmod(dir_path, 0700) != 0) {
        CGK_DAEMON_LOG("cannot restrict permissions on %s: %s\n", dir_path, strerror(errno));
        return CGK_EXIT_SOFTWARE;
    }

    return 0;
}

static int connect_to_socket_path(const char *socket_path)
{
    struct sockaddr_un addr;
    socklen_t addr_len = 0;
    if (fill_unix_addr(socket_path, &addr, &addr_len) != 0) {
        errno = ENAMETOOLONG;
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, addr_len) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    return fd;
}

static int remove_stale_socket_if_needed(const char *socket_path)
{
    struct stat st;
    if (lstat(socket_path, &st) != 0) {
        if (errno == ENOENT) {
            return 0;
        }
        CGK_DAEMON_LOG("cannot inspect %s: %s\n", socket_path, strerror(errno));
        return CGK_EXIT_SOFTWARE;
    }

    if (!S_ISSOCK(st.st_mode)) {
        CGK_DAEMON_LOG("control socket path already exists and is not a socket: %s\n", socket_path);
        return CGK_EXIT_SOFTWARE;
    }

    int existing_fd = connect_to_socket_path(socket_path);
    if (existing_fd >= 0) {
        close(existing_fd);
        CGK_DAEMON_LOG("another instance is already running\n");
        return CGK_EXIT_SOFTWARE;
    }

    if (unlink(socket_path) != 0) {
        CGK_DAEMON_LOG("cannot remove stale control socket %s: %s\n", socket_path, strerror(errno));
        return CGK_EXIT_SOFTWARE;
    }

    return 0;
}

int cgk_control_server_open(struct cgk_control_server *server)
{
    memset(server, 0, sizeof(*server));
    server->fd = -1;

    char dir_path[PATH_MAX];
    if (make_control_paths(dir_path, sizeof(dir_path), server->socket_path, sizeof(server->socket_path)) != 0) {
        CGK_DAEMON_LOG("cannot resolve control socket path\n");
        return CGK_EXIT_SOFTWARE;
    }

    int result = ensure_runtime_dir(dir_path);
    if (result != 0) {
        return result;
    }

    result = remove_stale_socket_if_needed(server->socket_path);
    if (result != 0) {
        return result;
    }

    struct sockaddr_un addr;
    socklen_t addr_len = 0;
    if (fill_unix_addr(server->socket_path, &addr, &addr_len) != 0) {
        CGK_DAEMON_LOG("control socket path is too long: %s\n", server->socket_path);
        return CGK_EXIT_SOFTWARE;
    }

    server->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server->fd < 0) {
        CGK_DAEMON_LOG("cannot create control socket: %s\n", strerror(errno));
        return CGK_EXIT_SOFTWARE;
    }
    if (set_close_on_exec(server->fd) != 0 || set_nonblocking(server->fd) != 0) {
        CGK_DAEMON_LOG("cannot configure control socket: %s\n", strerror(errno));
        cgk_control_server_close(server);
        return CGK_EXIT_SOFTWARE;
    }

    mode_t old_umask = umask(0077);
    if (bind(server->fd, (struct sockaddr *)&addr, addr_len) != 0) {
        int saved_errno = errno;
        umask(old_umask);
        CGK_DAEMON_LOG("cannot bind control socket %s: %s\n", server->socket_path, strerror(saved_errno));
        cgk_control_server_close(server);
        return CGK_EXIT_SOFTWARE;
    }
    umask(old_umask);

    if (listen(server->fd, 8) != 0) {
        CGK_DAEMON_LOG("cannot listen on control socket: %s\n", strerror(errno));
        cgk_control_server_close(server);
        return CGK_EXIT_SOFTWARE;
    }

    CGK_DAEMON_LOG("control socket ready at %s\n", server->socket_path);
    return 0;
}

static bool peer_is_current_user(int fd)
{
#ifdef SO_PEERCRED
    struct ucred credentials;
    socklen_t length = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
        return false;
    }
    return credentials.uid == getuid();
#else
    (void)fd;
    return true;
#endif
}

static char *trim_command(char *text)
{
    while (isspace((unsigned char)*text)) {
        text++;
    }

    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static const char *status_state_name(enum cgk_control_status_state state)
{
    switch (state) {
    case CGK_CONTROL_STATUS_COUNTING:
        return "counting";
    case CGK_CONTROL_STATUS_LOCKED:
        return "locked";
    case CGK_CONTROL_STATUS_REMINDING:
        return "reminding";
    }
    return "unknown";
}

static void write_status_response(int client_fd, const struct cgk_control_status *status)
{
    char response[256];
    int written = snprintf(
        response,
        sizeof(response),
        "OK state=%s elapsed_seconds=%llu remaining_seconds=%llu interval_seconds=%llu\n",
        status_state_name(status->state),
        status->elapsed_seconds,
        status->remaining_seconds,
        status->interval_seconds);
    if (written <= 0 || (size_t)written >= sizeof(response)) {
        write_all(client_fd, "ERR status response too long\n");
        return;
    }
    write_all(client_fd, response);
}

int cgk_control_poll(struct cgk_control_server *server, const struct cgk_control_status *status, enum cgk_control_request *request)
{
    *request = CGK_CONTROL_NONE;
    if (server->fd < 0) {
        return 0;
    }

    int client_fd = accept(server->fd, NULL, NULL);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        CGK_DAEMON_LOG("cannot accept control connection: %s\n", strerror(errno));
        return 0;
    }

    set_close_on_exec(client_fd);
    set_nonblocking(client_fd);

    if (!peer_is_current_user(client_fd)) {
        write_all(client_fd, "ERR permission denied\n");
        close(client_fd);
        return 0;
    }

    struct pollfd poll_fd = {
        .fd = client_fd,
        .events = POLLIN,
    };
    int poll_result = 0;
    do {
        poll_result = poll(&poll_fd, 1, 250);
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result <= 0) {
        write_all(client_fd, "ERR empty request\n");
        close(client_fd);
        return 0;
    }

    char buffer[128];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0 && errno == EINTR) {
        bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    }
    if (bytes_read <= 0) {
        write_all(client_fd, "ERR empty request\n");
        close(client_fd);
        return 0;
    }

    buffer[bytes_read] = '\0';
    char *command = trim_command(buffer);
    if (strcmp(command, "dismiss") == 0) {
        *request = CGK_CONTROL_DISMISS;
        write_all(client_fd, "OK dismissed\n");
    } else if (strcmp(command, "quit") == 0) {
        *request = CGK_CONTROL_QUIT;
        write_all(client_fd, "OK quitting\n");
    } else if (strcmp(command, "trigger") == 0) {
        *request = CGK_CONTROL_TRIGGER;
        write_all(client_fd, "OK triggered\n");
    } else if (strcmp(command, "status") == 0) {
        if (status == NULL) {
            write_all(client_fd, "ERR status unavailable\n");
        } else {
            write_status_response(client_fd, status);
        }
    } else {
        write_all(client_fd, "ERR unknown command\n");
    }

    close(client_fd);
    return 0;
}

void cgk_control_server_close(struct cgk_control_server *server)
{
    if (server->fd >= 0) {
        close(server->fd);
        server->fd = -1;
    }
    if (server->socket_path[0] != '\0') {
        unlink(server->socket_path);
        server->socket_path[0] = '\0';
    }
}

int cgk_control_client_send(const char *command, char *response, size_t response_size)
{
    if (response_size == 0) {
        return 1;
    }
    response[0] = '\0';

    char dir_path[PATH_MAX];
    char socket_path[PATH_MAX];
    if (make_control_paths(dir_path, sizeof(dir_path), socket_path, sizeof(socket_path)) != 0) {
        CGK_CTL_LOG("XDG_RUNTIME_DIR is required\n");
        return 1;
    }
    (void)dir_path;

    if (access(socket_path, F_OK) != 0 && errno == ENOENT) {
        CGK_CTL_LOG("daemon is not running\n");
        return 1;
    }

    int fd = connect_to_socket_path(socket_path);
    if (fd < 0) {
        if (errno == ENOENT || errno == ECONNREFUSED) {
            CGK_CTL_LOG("daemon is not running\n");
        } else if (errno == ENAMETOOLONG) {
            CGK_CTL_LOG("control socket path is too long\n");
        } else {
            CGK_CTL_LOG("cannot connect to daemon: %s\n", strerror(errno));
        }
        return 1;
    }

    char request[128];
    int written = snprintf(request, sizeof(request), "%s\n", command);
    if (written <= 0 || (size_t)written >= sizeof(request) || !write_all(fd, request)) {
        CGK_CTL_LOG("cannot send request\n");
        close(fd);
        return 1;
    }
    shutdown(fd, SHUT_WR);

    struct pollfd poll_fd = {
        .fd = fd,
        .events = POLLIN,
    };
    int poll_result = 0;
    do {
        poll_result = poll(&poll_fd, 1, 3000);
    } while (poll_result < 0 && errno == EINTR);

    if (poll_result <= 0) {
        CGK_CTL_LOG("daemon did not reply\n");
        close(fd);
        return 1;
    }

    ssize_t bytes_read = read(fd, response, response_size - 1);
    if (bytes_read < 0) {
        CGK_CTL_LOG("cannot read response: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    response[bytes_read] = '\0';
    close(fd);

    return strncmp(response, "OK ", 3) == 0 ? 0 : 1;
}
