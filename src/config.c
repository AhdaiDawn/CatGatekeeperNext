#include "config.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum config_key {
    KEY_INTERVAL_MINUTES = 0,
    KEY_SLEEP_SECONDS,
    KEY_SCREEN_INDEX,
    KEY_IDLE_RESET_SECONDS,
    KEY_COUNT
};

static char *trim(char *value)
{
    while (isspace((unsigned char)*value)) {
        value++;
    }

    char *end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    return value;
}

static int config_path(char *path, size_t path_size)
{
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata != NULL && appdata[0] != '\0') {
        int written = snprintf(path, path_size, "%s\\CatGatekeeper\\config.conf", appdata);
        return written > 0 && (size_t)written < path_size ? 0 : -1;
    }

    const char *profile = getenv("USERPROFILE");
    if (profile == NULL || profile[0] == '\0') {
        return -1;
    }

    int written = snprintf(path, path_size, "%s\\.config\\cat-gatekeeper\\config.conf", profile);
    return written > 0 && (size_t)written < path_size ? 0 : -1;
#else
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config != NULL && xdg_config[0] != '\0') {
        int written = snprintf(path, path_size, "%s/cat-gatekeeper/config.conf", xdg_config);
        return written > 0 && (size_t)written < path_size ? 0 : -1;
    }

    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return -1;
    }

    int written = snprintf(path, path_size, "%s/.config/cat-gatekeeper/config.conf", home);
    return written > 0 && (size_t)written < path_size ? 0 : -1;
#endif
}

static enum config_key key_from_name(const char *key)
{
    if (strcmp(key, "interval_minutes") == 0) {
        return KEY_INTERVAL_MINUTES;
    }
    if (strcmp(key, "sleep_seconds") == 0) {
        return KEY_SLEEP_SECONDS;
    }
    if (strcmp(key, "screen_index") == 0) {
        return KEY_SCREEN_INDEX;
    }
    if (strcmp(key, "idle_reset_seconds") == 0) {
        return KEY_IDLE_RESET_SECONDS;
    }
    return KEY_COUNT;
}

static int parse_int_range(const char *value, int min_value, int max_value, int *out)
{
    errno = 0;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') {
        return -1;
    }
    if (parsed < min_value || parsed > max_value) {
        return -1;
    }
    *out = (int)parsed;
    return 0;
}

void cgk_config_set_defaults(struct cgk_config *config)
{
    config->interval_minutes = 30;
    config->sleep_seconds = 300;
    config->screen_index = 0;
    config->idle_reset_seconds = 0;
}

int cgk_config_load(struct cgk_config *config)
{
    cgk_config_set_defaults(config);

    char path[PATH_MAX];
    if (config_path(path, sizeof(path)) != 0) {
        CGK_DAEMON_LOG("cannot resolve config path; using defaults\n");
        return 0;
    }

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        CGK_DAEMON_LOG("cannot read %s: %s\n", path, strerror(errno));
        return CGK_EXIT_CONFIG;
    }

    bool seen[KEY_COUNT] = {false};
    char line[4096];
    unsigned long line_number = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        line_number++;
        size_t length = strlen(line);
        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            CGK_DAEMON_LOG("%s:%lu line is too long\n", path, line_number);
            fclose(file);
            return CGK_EXIT_CONFIG;
        }

        char *text = trim(line);
        if (text[0] == '\0' || text[0] == '#') {
            continue;
        }

        char *equals = strchr(text, '=');
        if (equals == NULL) {
            CGK_DAEMON_LOG("%s:%lu expected key=value\n", path, line_number);
            fclose(file);
            return CGK_EXIT_CONFIG;
        }

        *equals = '\0';
        char *key = trim(text);
        char *value = trim(equals + 1);

        if (key[0] == '\0' || value[0] == '\0') {
            CGK_DAEMON_LOG("%s:%lu key and value must be non-empty\n", path, line_number);
            fclose(file);
            return CGK_EXIT_CONFIG;
        }

        enum config_key parsed_key = key_from_name(key);
        if (parsed_key == KEY_COUNT) {
            CGK_DAEMON_LOG("%s:%lu warning: unknown key '%s' ignored\n", path, line_number, key);
            continue;
        }

        if (seen[parsed_key]) {
            CGK_DAEMON_LOG("%s:%lu duplicate key '%s'\n", path, line_number, key);
            fclose(file);
            return CGK_EXIT_CONFIG;
        }
        seen[parsed_key] = true;

        int parsed_int = 0;
        switch (parsed_key) {
        case KEY_INTERVAL_MINUTES:
            if (parse_int_range(value, 1, 1440, &parsed_int) != 0) {
                CGK_DAEMON_LOG("%s:%lu interval_minutes must be in 1..1440\n", path, line_number);
                fclose(file);
                return CGK_EXIT_CONFIG;
            }
            config->interval_minutes = parsed_int;
            break;
        case KEY_SLEEP_SECONDS:
            if (parse_int_range(value, 1, 3600, &parsed_int) != 0) {
                CGK_DAEMON_LOG("%s:%lu sleep_seconds must be in 1..3600\n", path, line_number);
                fclose(file);
                return CGK_EXIT_CONFIG;
            }
            config->sleep_seconds = parsed_int;
            break;
        case KEY_SCREEN_INDEX:
            if (parse_int_range(value, 0, INT_MAX, &parsed_int) != 0) {
                CGK_DAEMON_LOG("%s:%lu screen_index must be a non-negative integer\n", path, line_number);
                fclose(file);
                return CGK_EXIT_CONFIG;
            }
            config->screen_index = parsed_int;
            break;
        case KEY_IDLE_RESET_SECONDS:
            if (parse_int_range(value, 0, 86400, &parsed_int) != 0) {
                CGK_DAEMON_LOG("%s:%lu idle_reset_seconds must be in 0..86400\n", path, line_number);
                fclose(file);
                return CGK_EXIT_CONFIG;
            }
            config->idle_reset_seconds = parsed_int;
            break;
        case KEY_COUNT:
            break;
        }
    }

    if (ferror(file)) {
        CGK_DAEMON_LOG("cannot read %s: %s\n", path, strerror(errno));
        fclose(file);
        return CGK_EXIT_CONFIG;
    }

    fclose(file);
    return 0;
}
