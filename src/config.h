#ifndef CAT_GATEKEEPER_CONFIG_H
#define CAT_GATEKEEPER_CONFIG_H

#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CGK_EXIT_CONFIG 64
#define CGK_EXIT_NO_INPUT 66
#define CGK_EXIT_UNAVAILABLE 69
#define CGK_EXIT_SOFTWARE 70

struct cgk_config {
    int interval_minutes;
    int sleep_seconds;
    int idle_reset_seconds;
};

void cgk_config_set_defaults(struct cgk_config *config);
int cgk_config_load(struct cgk_config *config);

#endif
