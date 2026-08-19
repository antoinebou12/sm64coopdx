#include <stdio.h>

// Deliberately does NOT include <switch.h>: libnx typedefs u64/s64 as
// (unsigned) long while the project's PR/ultratypes.h - pulled in through
// controller_switch.h below - typedefs them as (unsigned) long long, so the
// two headers cannot coexist in one translation unit. Only the console entry
// points are needed here, so declare them directly.
typedef struct PrintConsole PrintConsole;
PrintConsole* consoleInit(PrintConsole* console);
void consoleExit(PrintConsole* console);

#include "pc/controller/controller_switch.h"
#include "pc/platform/switch/switch_input.h"
#include "pc/platform/switch/switch_platform.h"

static int platform_probe_main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    consoleInit(NULL);

    SwitchPlatformState state;
    if (!switch_platform_init(&state)) {
        puts("switch_platform_init failed");
        consoleExit(NULL);
        return 1;
    }

    if (!switch_input_init()) {
        puts("switch_input_init failed");
        switch_platform_shutdown(&state);
        consoleExit(NULL);
        return 2;
    }

    switch_input_poll();

    printf("application_mode=%d\n", state.application_mode ? 1 : 0);
    printf("docked=%d\n", state.docked ? 1 : 0);
    printf("suspended=%d\n", state.suspended ? 1 : 0);
    printf("hos_version=0x%08x\n", state.hos_version);
    printf("data_root=%s\n", switch_platform_data_root());
    printf("game_stack_bytes=%u\n", (unsigned int)SWITCH_PLATFORM_GAME_STACK_SIZE);
    printf("controllers=%u\n", (unsigned int)controller_switch_connected_count());

    switch_input_rumble_stop_all();
    switch_input_shutdown();
    switch_platform_shutdown(&state);
    consoleExit(NULL);
    return 0;
}

int main(int argc, char **argv) {
    return switch_platform_run_main_on_game_thread(platform_probe_main, argc, argv);
}
