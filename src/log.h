#ifndef CAT_GATEKEEPER_LOG_H
#define CAT_GATEKEEPER_LOG_H

void cgk_log(const char *component, const char *format, ...);

#define CGK_DAEMON_LOG(...) cgk_log("cat-gatekeeperd", __VA_ARGS__)
#define CGK_CTL_LOG(...) cgk_log("cat-gatekeeperctl", __VA_ARGS__)

#endif
