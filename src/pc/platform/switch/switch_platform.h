#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
    bool socket_initialized;
    bool exit_requested;
    bool suspended;
    bool docked;
    bool application_mode;
    uint32_t hos_version;
    SwitchLifecycleEvent last_event;
} SwitchPlatformState;

bool switch_platform_init(SwitchPlatformState *state);
void switch_platform_shutdown(SwitchPlatformState *state);

/*
 * Pump Horizon applet messages once. Returns false when the application should
 * stop running. The game loop will eventually call this once per rendered frame.
 */
bool switch_platform_main_loop(SwitchPlatformState *state);

const char *switch_platform_data_root(void);

#ifdef __cplusplus
}
#endif