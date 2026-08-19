#include <switch.h>
#include <stdio.h>

#include "pc/platform/switch/switch_platform.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    consoleInit(NULL);

    SwitchPlatformState state;
    if (!switch_platform_init(&state)) {
        puts("switch_platform_init failed");
        consoleExit(NULL);
        return 1;
    }

    printf("application_mode=%d\n", state.application_mode ? 1 : 0);
    printf("docked=%d\n", state.docked ? 1 : 0);
    printf("suspended=%d\n", state.suspended ? 1 : 0);
    printf("hos_version=0x%08x\n", state.hos_version);
    printf("data_root=%s\n", switch_platform_data_root());

    switch_platform_shutdown(&state);
    consoleExit(NULL);
    return 0;
}
