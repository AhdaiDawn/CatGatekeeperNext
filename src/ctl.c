#include "control.h"

#include <stdio.h>
#include <string.h>

static int usage(void)
{
    fprintf(stderr, "usage: cat-gatekeeperctl dismiss|quit|trigger|status\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        return usage();
    }

    const char *command = argv[1];
    if (strcmp(command, "dismiss") != 0 && strcmp(command, "quit") != 0 && strcmp(command, "trigger") != 0 && strcmp(command, "status") != 0) {
        return usage();
    }

    char response[256];
    int result = cgk_control_client_send(command, response, sizeof(response));
    if (response[0] != '\0') {
        fputs(response, stdout);
    }
    return result;
}
