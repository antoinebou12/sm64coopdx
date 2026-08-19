#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWITCH_PLATFORM_GAME_STACK_SIZE (8u * 1024u * 1024u)

typedef enum SwitchLifecycleEvent {
    SWITCH_LIFECYCLE_NONE = 0,
    SWITCH_LIFECYCLE_FOCUS_CHANGED,
    SWITCH_LIFECYCLE_OPERATION_MODE_CHANGED,
    SWITCH_LIFECYCLE_PERFORMANCE_MODE_CHANGED,
    SWITCH_LIFECYCLE_EXIT_REQUESTED,
    SWITCH_LIFECYCLE_RESUMED,
} SwitchLifecycleEvent;

typedef struct SwitchPlatformState {
    bool initialized;
    bool exit_requested;
    bool suspended;
    bool docked;
    bool application_mode;
    uint32_t hos_version;
    SwitchLifecycleEvent last_event;
} SwitchPlatformState;

typedef int (*SwitchPlatformMainFn)(int argc, char **argv);
typedef void (*SwitchPlatformExitFn)(void);

bool switch_platform_init(SwitchPlatformState *state);
void switch_platform_shutdown(SwitchPlatformState *state);

/*
 * Run the game entry point on a dedicated Horizon thread with an 8 MiB stack.
 * hbloader's initial thread is too small for the deepest SM64 + Mesa/GLES call
 * paths and can otherwise die on the first rendered frame. Returns the entry
 * point's return code, or -1 if the dedicated thread could not be created or
 * started. Deliberately do not fall back to the initial thread on failure.
 */
int switch_platform_run_main_on_game_thread(SwitchPlatformMainFn entry, int argc, char **argv);

/* Optional clean-shutdown hook for HOME/OS exit requests. */
void switch_platform_set_exit_callback(SwitchPlatformExitFn callback);

/*
 * Pump Horizon applet messages once. Returns false when the application should
 * stop running. The game loop will eventually call this once per rendered frame.
 */
bool switch_platform_main_loop(SwitchPlatformState *state);

const char *switch_platform_data_root(void);

#ifdef __cplusplus
}
#endif
