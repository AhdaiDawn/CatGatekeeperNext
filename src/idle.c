#include "idle.h"

int cgk_idle_enabled(int idle_reset_seconds)
{
    return idle_reset_seconds > 0;
}
