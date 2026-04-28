#ifndef CAT_GATEKEEPER_LOGIND_H
#define CAT_GATEKEEPER_LOGIND_H

#include <stdbool.h>
#include <systemd/sd-bus.h>

struct cgk_logind {
    sd_bus *bus;
    char session_id[128];
    char session_path[256];
};

int cgk_logind_open(struct cgk_logind *logind);
void cgk_logind_close(struct cgk_logind *logind);
int cgk_logind_locked(struct cgk_logind *logind, bool *locked);

#endif
